/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-connection.c: Connection-handling code.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>

#include <fcntl.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>

#include "soup-auth.h"
#include "soup-context.h"
#include "soup-private.h"
#include "soup-misc.h"
#include "soup-socket.h"
#include "soup-socks.h"
#include "soup-ssl.h"

static guint most_recently_used_id = 0;

static void
connection_free (SoupConnection *conn)
{
	g_return_if_fail (conn != NULL);

	soup_socket_unref (conn->socket);
	g_source_remove (conn->death_tag);
	g_free (conn);
}

static gboolean 
connection_death (GIOChannel*     iochannel,
		  GIOCondition    condition,
		  SoupConnection *conn)
{
	if (!conn->in_use) {
		connection_free (conn);
		return FALSE;
	}

	return TRUE;
}

static SoupConnection *
soup_connection_from_socket (SoupSocket *socket, SoupProtocol protocol)
{
	SoupConnection *conn;
	GIOChannel *chan;
	int yes = 1, flags = 0, fd;

	conn = g_new0 (SoupConnection, 1);
	conn->socket = socket;
	conn->keep_alive = TRUE;
	conn->in_use = TRUE;
	conn->last_used_id = 0;

	chan = soup_socket_get_iochannel (socket);
	fd = g_io_channel_unix_get_fd (chan);
	setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
	flags = fcntl (fd, F_GETFL, 0);
	fcntl (fd, F_SETFL, flags | O_NONBLOCK);

	conn->death_tag = g_io_add_watch (chan,
					  G_IO_ERR | G_IO_HUP | G_IO_NVAL,
					  (GIOFunc) connection_death,
					  conn);

	return conn;
}

struct SoupConnectData {
	SoupConnectCallbackFn  cb;
	gpointer               user_data;

	gpointer               connect_tag;
	SoupUri               *proxy_uri;
	SoupUri               *dest_uri;
};

static gboolean
proxy_https_connect (SoupConnection *conn, 
		     SoupUri        *dest_uri)
{
	SoupContext *ctx;
	SoupMessage *connect_msg;
	gboolean ret = FALSE;

	ctx = soup_context_from_uri (dest_uri);
	connect_msg = soup_message_new (ctx, SOUP_METHOD_CONNECT);
	soup_context_unref (ctx);
	connect_msg->connection = conn;
	soup_message_send (connect_msg);

	if (!SOUP_MESSAGE_IS_ERROR (connect_msg)) {
		/*
		 * Avoid releasing the connection on message free
		 */
		connect_msg->connection = NULL;
		ret = TRUE;
	}
	soup_message_free (connect_msg);

	return ret;
}

static void
soup_connection_new_cb (SoupSocket         *socket,
			SoupKnownErrorCode  status,
			gpointer            user_data)
{
	struct SoupConnectData *data = user_data;
	SoupConnection *conn;

	if (status == SOUP_ERROR_CANT_RESOLVE) {
		(*data->cb) (NULL,
			     (data->proxy_uri ?
			      SOUP_ERROR_CANT_RESOLVE_PROXY :
			      SOUP_ERROR_CANT_RESOLVE), 
			     data->user_data);
		goto DONE;
	} else if (status != SOUP_ERROR_OK) {
		(*data->cb) (NULL,
			     (data->proxy_uri ?
			      SOUP_ERROR_CANT_CONNECT_PROXY :
			      SOUP_ERROR_CANT_CONNECT), 
			     data->user_data);
		goto DONE;
	}

	/* Handle SOCKS proxy negotiation */
	if (data->proxy_uri &&
	    (data->proxy_uri->protocol == SOUP_PROTOCOL_SOCKS4 ||
	     data->proxy_uri->protocol == SOUP_PROTOCOL_SOCKS5)) {
		SoupUri *proxy_uri;

		proxy_uri = data->proxy_uri;
		data->proxy_uri = NULL;

		soup_socks_proxy_connect (socket,
					  proxy_uri,
					  data->dest_uri,
					  soup_connection_new_cb,
					  data);
		soup_uri_free (proxy_uri);
		return;
	} 
	
	/* Handle HTTPS tunnel setup via proxy CONNECT request. */
	if (data->proxy_uri &&
	    data->dest_uri->protocol == SOUP_PROTOCOL_HTTPS) {
		/* Synchronously send CONNECT request */
		conn = soup_connection_from_socket (socket, data->proxy_uri->protocol);
		if (proxy_https_connect (conn, data->dest_uri)) {
			soup_socket_start_ssl (socket);
			(*data->cb) (conn,
				     SOUP_ERROR_OK,
				     data->user_data);
		} else {
			connection_free (conn);
			(*data->cb) (NULL,
				     SOUP_ERROR_CANT_CONNECT, 
				     data->user_data);
		}
		goto DONE;
	}

	conn = soup_connection_from_socket (socket, data->dest_uri->protocol);
	(*data->cb) (conn, SOUP_ERROR_OK, data->user_data);

 DONE:
	if (data->dest_uri)
		soup_uri_free (data->dest_uri);
	if (data->proxy_uri)
		soup_uri_free (data->proxy_uri);
	g_free (data);
}

/**
 * soup_connection_new_via_proxy:
 * @uri: URI of the origin server (final destination)
 * @proxy_uri: URI of proxy to use to connect to @uri (or %NULL for a
 * direct connection).
 * @cb: a %SoupConnectCallbackFn to be called when a valid connection is
 * available.
 * @user_data: the user_data passed to @cb.
 *
 * Initiates the process of establishing a connection to the server
 * referenced by @uri. If @proxy_uri is non-%NULL, the actual network
 * connection will be made to the server it identifies, which will
 * then be asked to connect to @uri on our behalf.
 *
 * Return value: a %SoupConnectId which can be used to cancel a
 * connection attempt using %soup_connection_cancel(), or %NULL if
 * the connect attempt fails immediately.
 */
SoupConnectId
soup_connection_new_via_proxy (SoupUri *uri, SoupUri *proxy_uri,
			       SoupConnectCallbackFn cb, gpointer user_data)
{
	struct SoupConnectData *data;
	gpointer connect_tag;

	g_return_val_if_fail (uri != NULL, NULL);

	data = g_new0 (struct SoupConnectData, 1);
	data->cb = cb;
	data->user_data = user_data;

	data->dest_uri = soup_uri_copy (uri);
	if (proxy_uri) {
		data->proxy_uri = soup_uri_copy (proxy_uri);
		uri = proxy_uri;
	}

	connect_tag = soup_socket_connect (uri->host, uri->port,
					   uri->protocol == SOUP_PROTOCOL_HTTPS,
					   soup_connection_new_cb, data);
	/* 
	 * NOTE: soup_socket_connect can fail immediately and call our
	 * callback which will delete the state.  
	 */
	if (connect_tag) {
		data->connect_tag = connect_tag;
		return data;
	} else
		return NULL;
}

/**
 * soup_connection_cancel_connect:
 * @tag: a %SoupConnectId representing a connection in progress.
 *
 * Cancels the connection attempt represented by @tag. The
 * %SoupConnectCallbackFn passed in %soup_context_get_connection is not
 * called.
 */
void
soup_connection_cancel_connect (SoupConnectId tag)
{
	struct SoupConnectData *data = tag;

	g_return_if_fail (data != NULL);

	soup_socket_connect_cancel (data->connect_tag);

	if (data->dest_uri)
		soup_uri_free (data->dest_uri);
	if (data->proxy_uri)
		soup_uri_free (data->proxy_uri);
	g_free (data);
}

/**
 * soup_connection_release:
 * @conn: a %SoupConnection currently in use.
 *
 * Mark the connection represented by @conn as being unused. If the
 * keep-alive flag is not set on the connection, the connection is closed
 * and its resources freed, otherwise the connection is returned to the
 * unused connection pool for the server.
 */
void
soup_connection_release (SoupConnection *conn)
{
	g_return_if_fail (conn != NULL);

	if (conn->keep_alive) {
		conn->last_used_id = ++most_recently_used_id;
		conn->in_use = FALSE;		
	} else
		connection_free (conn);
}

/**
 * soup_connection_get_iochannel:
 * @conn: a %SoupConnection.
 *
 * Returns a GIOChannel used for IO operations on the network connection
 * represented by @conn.
 *
 * Return value: a pointer to the GIOChannel used for IO on %conn.
 */
GIOChannel *
soup_connection_get_iochannel (SoupConnection *conn)
{
	g_return_val_if_fail (conn != NULL, NULL);

	return soup_socket_get_iochannel (conn->socket);
}

/**
 * soup_connection_set_keep_alive:
 * @conn: a %SoupConnection.
 * @keep_alive: boolean keep-alive value.
 *
 * Sets the keep-alive flag on the %SoupConnection pointed to by %conn.
 */
void
soup_connection_set_keep_alive (SoupConnection *conn, gboolean keep_alive)
{
	g_return_if_fail (conn != NULL);
	conn->keep_alive = keep_alive;
}

/**
 * soup_connection_is_keep_alive:
 * @conn: a %SoupConnection.
 *
 * Returns the keep-alive flag for the %SoupConnection pointed to by
 * %conn. If this flag is TRUE, the connection will be returned to the pool
 * of unused connections when next %soup_connection_release is called,
 * otherwise the connection will be closed and resources freed.
 *
 * Return value: the keep-alive flag for @conn.
 */
gboolean
soup_connection_is_keep_alive (SoupConnection *conn)
{
	g_return_val_if_fail (conn != NULL, FALSE);
	return conn->keep_alive;
}
