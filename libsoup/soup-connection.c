/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-connection.c: Connection-handling code.
 *
 * Copyright (C) 2000-2003, Ximian, Inc.
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
#include "soup-connection.h"
#include "soup-context.h"
#include "soup-private.h"
#include "soup-misc.h"
#include "soup-socket.h"
#include "soup-socks.h"
#include "soup-ssl.h"

struct _SoupConnectionPrivate {
	guint watch_tag;
	gboolean in_use;

	SoupConnectionCallbackFn connect_func;
	gpointer                 connect_data;
	SoupUri *dest_uri, *proxy_uri;
};

#define PARENT_TYPE SOUP_TYPE_SOCKET
static SoupSocketClass *parent_class;

static void
init (GObject *object)
{
	SoupConnection *conn = SOUP_CONNECTION (object);

	conn->priv = g_new0 (SoupConnectionPrivate, 1);
}

static void
finalize (GObject *object)
{
	SoupConnection *conn = SOUP_CONNECTION (object);

	if (conn->priv->watch_tag)
		g_source_remove (conn->priv->watch_tag);

	if (conn->priv->dest_uri)
		soup_uri_free (conn->priv->dest_uri);
	if (conn->priv->proxy_uri)
		soup_uri_free (conn->priv->proxy_uri);

	g_free (conn->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_connection, SoupConnection, class_init, init, PARENT_TYPE)


static gboolean 
connection_watch (GIOChannel *iochannel, GIOCondition condition,
		  SoupConnection *conn)
{
	if (!conn->priv->in_use) {
		g_object_unref (conn);
		return FALSE;
	}

	return TRUE;
}

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
socket_connected (SoupSocket *socket, SoupKnownErrorCode status, gpointer data)
{
	SoupConnection *conn = SOUP_CONNECTION (socket);
	GIOChannel *chan;
	SoupConnectionCallbackFn connect_func;
	gpointer connect_data;

	connect_func = conn->priv->connect_func;
	conn->priv->connect_func = NULL;
	connect_data = conn->priv->connect_data;
	conn->priv->connect_data = NULL;

	if (status == SOUP_ERROR_CANT_RESOLVE) {
		connect_func (conn, (conn->priv->proxy_uri ?
				     SOUP_ERROR_CANT_RESOLVE_PROXY :
				     SOUP_ERROR_CANT_RESOLVE), 
			      connect_data);
		return;
	} else if (status != SOUP_ERROR_OK) {
		connect_func (conn, (conn->priv->proxy_uri ?
				     SOUP_ERROR_CANT_CONNECT_PROXY :
				     SOUP_ERROR_CANT_CONNECT), 
			      connect_data);
		return;
	}

	/* Handle SOCKS proxy negotiation */
	if (conn->priv->proxy_uri &&
	    (conn->priv->proxy_uri->protocol == SOUP_PROTOCOL_SOCKS4 ||
	     conn->priv->proxy_uri->protocol == SOUP_PROTOCOL_SOCKS5)) {
		SoupUri *proxy_uri;

		proxy_uri = conn->priv->proxy_uri;
		conn->priv->proxy_uri = NULL;

		soup_socks_proxy_connect (socket,
					  proxy_uri,
					  conn->priv->dest_uri,
					  socket_connected,
					  conn);
		soup_uri_free (proxy_uri);
		return;
	}

	chan = soup_socket_get_iochannel (socket);
	conn->priv->watch_tag = g_io_add_watch (chan,
						G_IO_ERR | G_IO_HUP | G_IO_NVAL,
						(GIOFunc) connection_watch,
						conn);
	g_io_channel_unref (chan);

	/* Handle HTTPS tunnel setup via proxy CONNECT request. */
	if (conn->priv->proxy_uri &&
	    conn->priv->dest_uri->protocol == SOUP_PROTOCOL_HTTPS) {
		/* Synchronously send CONNECT request */
		if (proxy_https_connect (conn, conn->priv->dest_uri)) {
			soup_socket_start_ssl (socket);
		} else {
			connect_func (conn,
				      SOUP_ERROR_CANT_CONNECT, 
				      connect_data);
			return;
		}
	}

	connect_func (conn, SOUP_ERROR_OK, connect_data);
}

/**
 * soup_connection_new_via_proxy:
 * @uri: URI of the origin server (final destination)
 * @proxy_uri: URI of proxy to use to connect to @uri
 * @func: a #SoupConnectionCallbackFn to be called when a valid
 * connection is available.
 * @data: the user_data passed to @func.
 *
 * Initiates the process of establishing a connection to the server
 * referenced by @uri via @proxy_uri.
 *
 * Return value: the new #SoupConnection.
 */
SoupConnection *
soup_connection_new_via_proxy (SoupUri *uri, SoupUri *proxy_uri,
			       SoupConnectionCallbackFn func, gpointer data)
{
	SoupConnection *conn;

	g_return_val_if_fail (uri != NULL, NULL);

	conn = g_object_new (SOUP_TYPE_CONNECTION, NULL);

	conn->priv->connect_func = func;
	conn->priv->connect_data = data;

	conn->priv->dest_uri = soup_uri_copy (uri);
	if (proxy_uri) {
		conn->priv->proxy_uri = soup_uri_copy (proxy_uri);
		uri = proxy_uri;
	}

	soup_socket_client_connect (SOUP_SOCKET (conn),
				    uri->host, uri->port,
				    uri->protocol == SOUP_PROTOCOL_HTTPS,
				    socket_connected, NULL);
	return conn;
}

/**
 * soup_connection_new:
 * @uri: URI of the server to connect to
 * @func: a #SoupConnectionCallbackFn to be called when a valid
 * connection is available.
 * @data: the user_data passed to @func.
 *
 * Initiates the process of establishing a connection to the server
 * referenced by @uri.
 *
 * Return value: the new #SoupConnection.
 */
SoupConnection *
soup_connection_new (SoupUri *uri,
		     SoupConnectionCallbackFn func, gpointer data)
{
	return soup_connection_new_via_proxy (uri, NULL, func, data);
}

/**
 * soup_connection_get_iochannel:
 * @conn: a #SoupConnection.
 *
 * Returns a GIOChannel used for IO operations on the network connection
 * represented by @conn.
 *
 * Return value: a pointer to the GIOChannel used for IO on @conn.
 */
GIOChannel *
soup_connection_get_iochannel (SoupConnection *conn)
{
	g_return_val_if_fail (SOUP_IS_CONNECTION (conn), NULL);

	/* Don't return the channel if we're still connecting. */
	if (conn->priv->connect_func)
		return NULL;

	return soup_socket_get_iochannel (SOUP_SOCKET (conn));
}

/**
 * soup_connection_set_in_use:
 * @conn: a #SoupConnection.
 * @in_use: whether or not @conn is in use
 *
 * Sets the in-use flag on the #SoupConnection pointed to by @conn.
 */
void
soup_connection_set_in_use (SoupConnection *conn, gboolean in_use)
{
	g_return_if_fail (SOUP_IS_CONNECTION (conn));

	if (conn->priv->in_use == in_use)
		return;

	conn->priv->in_use = in_use;
	if (in_use)
		g_object_ref (conn);
	else
		g_object_unref (conn);
}

/**
 * soup_connection_is_in_use:
 * @conn: a #SoupConnection.
 *
 * Return value: the in-use flag for @conn.
 */
gboolean
soup_connection_is_in_use (SoupConnection *conn)
{
	g_return_val_if_fail (SOUP_IS_CONNECTION (conn), FALSE);

	return conn->priv->in_use;
}

/**
 * soup_connection_close:
 * @conn: a #SoupConnection
 *
 * Marks @conn not in use and then unrefs it, which should cause
 * the connection to be destroyed.
 */
void
soup_connection_close (SoupConnection *conn)
{
	g_return_if_fail (SOUP_IS_CONNECTION (conn));

	soup_connection_set_in_use (conn, FALSE);
	g_object_unref (conn);
}
