/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-connection.c: Connection-handling code.
 *
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-auth-context.h"
#include "soup-connection.h"
#include "soup-private.h"
#include "soup-socket.h"
#include "soup-ssl.h"

struct _SoupConnectionPrivate {
	SoupAuthContext *ac;

	guint watch_tag;
	gboolean in_use;
};

#define PARENT_TYPE SOUP_TYPE_SOCKET
static SoupSocketClass *parent_class;

enum {
	PROP_0,
	PROP_AUTH_CONTEXT
};

static void connected (SoupSocket *sock, SoupKnownErrorCode status);
static SoupAddress *get_remote_address (SoupConnection *conn);
static void start_request (SoupConnection *conn, SoupMessage *msg);

static void set_property (GObject *object, guint prop_id,
			  const GValue *value, GParamSpec *pspec);

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

	if (conn->priv->ac)
		g_object_unref (conn->priv->ac);

	if (conn->priv->watch_tag)
		g_source_remove (conn->priv->watch_tag);

	g_free (conn->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	SoupSocketClass *socket_class =
		SOUP_SOCKET_CLASS (object_class);
	SoupConnectionClass *connection_class =
		SOUP_CONNECTION_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method definition */
	connection_class->get_remote_address = get_remote_address;
	connection_class->start_request = start_request;

	/* virtual method override */
	socket_class->connected = connected;
	object_class->finalize = finalize;
	object_class->set_property = set_property;

	/* properties */
	g_object_class_install_property (
		object_class, PROP_AUTH_CONTEXT,
		g_param_spec_object ("auth_context",
				     "Authentication context",
				     "Object that manages authentication for th e connection",
				     SOUP_TYPE_AUTH_CONTEXT,
				     G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

SOUP_MAKE_TYPE (soup_connection, SoupConnection, class_init, init, PARENT_TYPE)

static void
set_property (GObject *object, guint prop_id,
	      const GValue *value, GParamSpec *pspec)
{
	SoupConnection *conn = SOUP_CONNECTION (object);

	switch (prop_id) {
	case PROP_AUTH_CONTEXT:
		conn->priv->ac =
			g_object_ref (g_value_get_object (value));
		break;
	default:
		break;
	}
}


static gboolean 
connection_watch (GIOChannel *iochannel, GIOCondition condition,
		  gpointer user_data)
{
	SoupConnection *conn = user_data;

	if (!conn->priv->in_use) {
		g_object_unref (conn);
		return FALSE;
	}

	return TRUE;
}

static void
connected (SoupSocket *sock, SoupKnownErrorCode status)
{
	SoupConnection *conn = SOUP_CONNECTION (sock);
	GIOChannel *chan;

	if (status != SOUP_ERROR_OK)
		return;

	chan = soup_socket_get_iochannel (sock);
	conn->priv->watch_tag =
		g_io_add_watch (chan, G_IO_ERR | G_IO_HUP | G_IO_NVAL,
				connection_watch, conn);
}

/**
 * soup_connection_new:
 * @uri: URI of the server to connect to
 * @ac: auth context for that server
 *
 * Initiates the process of establishing a connection to the server
 * referenced by @uri.
 *
 * Return value: the new #SoupConnection.
 */
SoupConnection *
soup_connection_new (SoupUri *uri, SoupAuthContext *ac)
{
	SoupConnection *conn;

	g_return_val_if_fail (uri != NULL, NULL);

	conn = g_object_new (SOUP_TYPE_CONNECTION,
			     "auth_context", ac,
			     NULL);

	soup_socket_client_connect (SOUP_SOCKET (conn), uri);
	return conn;
}

/**
 * soup_connection_get_iochannel:
 * @conn: a #SoupConnection.
 *
 * Returns a GIOChannel used for IO operations on the network connection
 * represented by @conn. The caller should not ref or unref the channel;
 * it belongs to the connection.
 *
 * Return value: a pointer to the GIOChannel used for IO on @conn.
 */
GIOChannel *
soup_connection_get_iochannel (SoupConnection *conn)
{
	g_return_val_if_fail (SOUP_IS_CONNECTION (conn), NULL);

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

static SoupAddress *
get_remote_address (SoupConnection *conn)
{
	return soup_socket_get_remote_address (SOUP_SOCKET (conn));
}

/**
 * soup_connection_get_remote_address:
 * @conn: a #SoupConnection
 *
 * Return value: the address of the remote server @conn is connected
 * to, or %NULL if it is connected to a proxy that can talk to any
 * server (or if @conn is invalid or not connected).
 **/
SoupAddress *
soup_connection_get_remote_address (SoupConnection *conn)
{
	g_return_val_if_fail (SOUP_IS_CONNECTION (conn), NULL);

	return SOUP_CONNECTION_GET_CLASS (conn)->get_remote_address (conn);
}

static void 
authorize_handler (SoupMessage *msg, gpointer ac)
{
	if (soup_auth_context_handle_unauthorized (ac, msg))
		soup_message_requeue (msg);
}

static void
start_request (SoupConnection *conn, SoupMessage *msg)
{
	msg->connection = conn;

	/* Handle authorization */
	if (conn->priv->ac) {
		soup_auth_context_authorize_message (conn->priv->ac, msg);
		soup_message_remove_handler (
			msg, SOUP_HANDLER_PRE_BODY,
			authorize_handler, conn->priv->ac);
		soup_message_add_error_code_handler (
			msg, SOUP_ERROR_UNAUTHORIZED,
			SOUP_HANDLER_PRE_BODY,
			authorize_handler, conn->priv->ac);
	}

	soup_message_send_request (msg);
}

/**
 * soup_connection_start_request:
 * @conn: a #SoupConnection
 * @msg: a #SoupMessage
 *
 * Queues @msg on @conn.
 **/
void
soup_connection_start_request (SoupConnection *conn, SoupMessage *msg)
{
	g_return_if_fail (SOUP_IS_CONNECTION (conn));
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	return SOUP_CONNECTION_GET_CLASS (conn)->start_request (conn, msg);
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
