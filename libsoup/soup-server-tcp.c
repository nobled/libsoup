/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-server.c: Asynchronous HTTP server
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "soup-server-tcp.h"
#include "soup-address.h"
#include "soup-headers.h"
#include "soup-private.h"
#include "soup-server-message.h"
#include "soup-transfer.h"

typedef struct {
	SoupSocket  *sock;
	SoupServer  *serv;

	guint        new_request_watch;
	SoupMessage *msg;
} SoupServerTCPConnection;

struct _SoupServerTCPPrivate {
	SoupProtocol       proto;
	SoupAddressFamily  family;
	gushort            port;

	guint              accept_tag;
	SoupSocket        *listen_sock;

	GSList            *connections;
};

#define PARENT_TYPE SOUP_TYPE_SERVER
static SoupServerClass *parent_class;

static void run_async (SoupServer *serv);

static void
init (GObject *object)
{
	SoupServerTCP *tcp = SOUP_SERVER_TCP (object);

	tcp->priv = g_new0 (SoupServerTCPPrivate, 1);
}

static void
free_sconn (SoupServerTCPConnection *sconn)
{
	SoupServerTCP *tcp = SOUP_SERVER_TCP (sconn->serv);

	tcp->priv->connections =
		g_slist_remove (tcp->priv->connections, sconn);

	if (sconn->new_request_watch)
		g_source_remove (sconn->new_request_watch);
	if (sconn->msg)
		soup_server_message_free (sconn->msg);

	g_object_unref (sconn->sock);
	g_free (sconn);
}

static void
foreach_free_sconn (gpointer sconn, gpointer user_data)
{
	free_sconn (sconn);
}

static void
finalize (GObject *object)
{
	SoupServerTCP *tcp = SOUP_SERVER_TCP (object);

	if (tcp->priv->accept_tag)
		g_source_remove (tcp->priv->accept_tag);
	if (tcp->priv->listen_sock)
		g_object_unref (tcp->priv->listen_sock);

	g_slist_foreach (tcp->priv->connections, foreach_free_sconn, NULL);
	g_slist_free (tcp->priv->connections);

	g_free (tcp->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	SoupServerClass *server_class = SOUP_SERVER_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	server_class->run_async = run_async;
	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_server_tcp, SoupServerTCP, class_init, init, PARENT_TYPE)


SoupServer *
soup_server_tcp_new (SoupProtocol proto, SoupAddressFamily family, gushort port)
{
	SoupServerTCP *tcp;
	SoupSocket *sock;

	sock = soup_socket_server_new (soup_address_new_any (family),
				       port,
				       proto == SOUP_PROTOCOL_HTTPS);
	if (!sock)
		return NULL;

	tcp = g_object_new (SOUP_TYPE_SERVER_TCP, NULL);
	tcp->priv->proto = proto;
	tcp->priv->family = family;
	tcp->priv->listen_sock = sock;
	tcp->priv->port = soup_socket_get_local_port (sock);

	return (SoupServer *)tcp;
}

SoupProtocol
soup_server_tcp_get_protocol (SoupServerTCP *tcp)
{
	g_return_val_if_fail (SOUP_IS_SERVER_TCP (tcp), 0);

	return tcp->priv->proto;
}

SoupAddressFamily
soup_server_tcp_get_family (SoupServerTCP *tcp)
{
	g_return_val_if_fail (SOUP_IS_SERVER_TCP (tcp), 0);

	return tcp->priv->family;
}

gushort
soup_server_tcp_get_port (SoupServerTCP *tcp)
{
	g_return_val_if_fail (SOUP_IS_SERVER_TCP (tcp), 0);

	return tcp->priv->port;
}

static gboolean new_request (GIOChannel *chan, GIOCondition condition, gpointer user_data);

static gboolean
check_close_connection (SoupMessage *msg)
{
	const char *connection;

	connection = soup_message_get_header (msg->request_headers,
					      "Connection");

	if (msg->priv->http_version == SOUP_HTTP_1_0) {
		/* Close the connection unless there's a
		 * "Connection: keep-alive" header
		 */
		return !connection ||
			g_strcasecmp (connection, "keep-alive") != 0;
	} else {
		/* Close the connection only if there's a
		 * "Connection: close" header
		 */
		return connection &&
			g_strcasecmp (connection, "close") == 0;
	}
}

static void
response_sent (SoupMessage *msg, gpointer user_data)
{
	SoupServerTCPConnection *sconn = user_data;

	sconn->msg = NULL;
	if (check_close_connection (msg))
		free_sconn (sconn);
	else {
		sconn->new_request_watch =
			g_io_add_watch (soup_socket_get_iochannel (sconn->sock),
					G_IO_IN | G_IO_HUP, new_request, sconn);
	}
}

static SoupTransferDone
read_headers_cb (const GString        *headers,
		 SoupTransferEncoding *encoding,
		 int                  *content_len,
		 gpointer              user_data)
{
	SoupServerTCPConnection *sconn = user_data;
	SoupMessage *msg = sconn->msg;
	SoupContext *ctx;
	char *req_path = NULL;
	const char *length, *enc;

	if (!soup_headers_parse_request (headers->str, 
					 headers->len, 
					 msg->request_headers, 
					 (char **) &msg->method, 
					 &req_path,
					 &msg->priv->http_version))
		goto THROW_MALFORMED_HEADER;

	/* Handle request body encoding */
	length = soup_message_get_header (msg->request_headers, 
					  "Content-Length");
	enc = soup_message_get_header (msg->request_headers, 
				       "Transfer-Encoding");

	if (enc) {
		if (g_strcasecmp (enc, "chunked") == 0)
			*encoding = SOUP_TRANSFER_CHUNKED;
		else {
			g_warning ("Unknown encoding type in HTTP request.");
			goto THROW_MALFORMED_HEADER;
		}
	} else if (length) {
		*encoding = SOUP_TRANSFER_CONTENT_LENGTH;
		*content_len = atoi (length);
		if (*content_len < 0) 
			goto THROW_MALFORMED_HEADER;
	} else {
		*encoding = SOUP_TRANSFER_CONTENT_LENGTH;
		*content_len = 0;
	}

	/* Generate correct context for request */
	if (*req_path != '/') {
		/* Should be an absolute URI. (If not, soup_context_get
		 * will fail and return NULL.
		 */
		ctx = soup_context_get (req_path);
	} else {
		SoupServerTCP *tcp = SOUP_SERVER_TCP (sconn->serv);
		const char *req_host;
		char *url;

		req_host = soup_message_get_header (msg->request_headers, "Host");
		if (!req_host) {
			SoupAddress *addr;

			addr = soup_socket_get_local_address (sconn->sock);
			req_host = soup_address_get_physical (addr);
		}

		url = g_strdup_printf ("%s://%s:%d%s",
				       tcp->priv->proto == SOUP_PROTOCOL_HTTPS ? "https" : "http",
				       req_host, 
				       tcp->priv->port,
				       req_path);
		ctx = soup_context_get (url);
		g_free (url);
	}
	if (!ctx)
		goto THROW_MALFORMED_HEADER;

	soup_message_set_context (msg, ctx);
	soup_context_unref (ctx);

	g_free (req_path);

	return SOUP_TRANSFER_CONTINUE;

 THROW_MALFORMED_HEADER:
	g_free (req_path);

	soup_message_set_error (msg, SOUP_ERROR_BAD_REQUEST);
	soup_server_message_respond (msg,
				     soup_socket_get_iochannel (sconn->sock),
				     response_sent, NULL);

	return SOUP_TRANSFER_CONTINUE;
}

static void
error_cb (gboolean body_started, gpointer user_data)
{
	SoupServerTCPConnection *sconn = user_data;

	free_sconn (sconn);
}

static void
read_done_cb (const SoupDataBuffer *data,
	      gpointer              user_data)
{
	SoupServerTCPConnection *sconn = user_data;
	SoupMessage *msg = sconn->msg;

	msg->priv->read_tag = 0;

	msg->request.owner = data->owner;
	msg->request.length = data->length;
	msg->request.body = data->body;

	soup_server_handle_request (sconn->serv, msg,
				    soup_message_get_uri (msg)->path);
	soup_server_message_respond (msg,
				     soup_socket_get_iochannel (sconn->sock),
				     response_sent, sconn);
}

static gboolean
new_request (GIOChannel   *chan,
	     GIOCondition  condition, 
	     gpointer      user_data)
{
	SoupServerTCPConnection *sconn = user_data;

	sconn->new_request_watch = 0;

	if (!(condition & G_IO_IN)) {
		free_sconn (sconn);
		return FALSE;
	}

	sconn->msg = soup_server_message_new (soup_socket_get_remote_address (sconn->sock),
					      SOUP_SERVER_MESSAGE_HTTP);

	sconn->msg->priv->read_tag =
		soup_transfer_read (chan, FALSE, read_headers_cb, NULL,
				    read_done_cb, error_cb, sconn);

	return FALSE;
}

static gboolean 
conn_accept (GIOChannel    *listen_chan,
	     GIOCondition   condition, 
	     gpointer       serv)
{
	SoupServerTCP *tcp = serv;
	SoupServerTCPConnection *sconn;
	SoupSocket *sock;
	GIOChannel *chan;

	sock = soup_socket_server_try_accept (tcp->priv->listen_sock);
	if (!sock)
		return TRUE;

	sconn = g_new0 (SoupServerTCPConnection, 1);
	sconn->serv = serv;
	sconn->sock = sock;

	chan = soup_socket_get_iochannel (sock);
	sconn->new_request_watch =
		g_io_add_watch (chan, G_IO_IN | G_IO_HUP, new_request, sconn);

	tcp->priv->connections = g_slist_prepend (tcp->priv->connections, sconn);
	return TRUE;
}

static void
run_async (SoupServer *serv)
{
	SoupServerTCP *tcp = SOUP_SERVER_TCP (serv);
	GIOChannel *chan;

	/* Listen for new connections (if not already) */
	if (!tcp->priv->accept_tag) {
		chan = soup_socket_get_iochannel (tcp->priv->listen_sock);

		tcp->priv->accept_tag =
			g_io_add_watch (chan, G_IO_IN, conn_accept, serv);
	}

	g_object_ref (serv);
}
