/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-proxy-connection.c: HTTP/HTTPS proxy connection object
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "soup-proxy-connection.h"
#include "soup-auth-context.h"
#include "soup-private.h"

struct _SoupProxyConnectionPrivate {
	SoupAuthContext *proxy_ac;

	SoupUri *tunnel_dest;
	gboolean tunneling;
};

#define PARENT_TYPE SOUP_TYPE_CONNECTION
static SoupConnection *parent_class;

enum {
	PROP_0,
	PROP_PROXY_AUTH_CONTEXT
};

static void connected (SoupSocket *sock, SoupKnownErrorCode err);
static SoupAddress *get_remote_address (SoupConnection *conn);
static void start_request (SoupConnection *conn, SoupMessage *msg);

static void set_property (GObject *object, guint prop_id,
			  const GValue *value, GParamSpec *pspec);

static void
init (GObject *object)
{
	SoupProxyConnection *pconn = SOUP_PROXY_CONNECTION (object);

	pconn->priv = g_new0 (SoupProxyConnectionPrivate, 1);
}

static void
finalize (GObject *object)
{
	SoupProxyConnection *pconn = SOUP_PROXY_CONNECTION (object);

	if (pconn->priv->proxy_ac)
		g_object_unref (pconn->priv->proxy_ac);

	if (pconn->priv->tunnel_dest)
		soup_uri_free (pconn->priv->tunnel_dest);

	g_free (pconn->priv);

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

	/* virtual method override */
	socket_class->connected = connected;
	connection_class->get_remote_address = get_remote_address;
	connection_class->start_request = start_request;

	object_class->finalize = finalize;
	object_class->set_property = set_property;

	/* properties */
	g_object_class_install_property (
		object_class, PROP_PROXY_AUTH_CONTEXT,
		g_param_spec_object ("proxy_auth_context",
				     "Proxy authentication context",
				     "Object that manages authentication for th e proxy",
				     SOUP_TYPE_AUTH_CONTEXT,
				     G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
}

SOUP_MAKE_TYPE (soup_proxy_connection, SoupProxyConnection, class_init, init, PARENT_TYPE)

static void
set_property (GObject *object, guint prop_id,
	      const GValue *value, GParamSpec *pspec)
{
	SoupProxyConnection *pconn = SOUP_PROXY_CONNECTION (object);

	switch (prop_id) {
	case PROP_PROXY_AUTH_CONTEXT:
		pconn->priv->proxy_ac =
			g_object_ref (g_value_get_object (value));
		break;
	default:
		break;
	}
}


SoupConnection *
soup_proxy_connection_new (const SoupUri *origin_server,
			   SoupAuthContext *origin_ac,
			   const SoupUri *proxy_server,
			   SoupAuthContext *proxy_ac)
{
	SoupProxyConnection *pconn;

	g_return_val_if_fail (origin_server != NULL, NULL);
	g_return_val_if_fail (proxy_server != NULL, NULL);

	pconn = g_object_new (SOUP_TYPE_PROXY_CONNECTION,
			      "auth_context", origin_ac,
			      "proxy_auth_context", proxy_ac,
			      NULL);

	if (origin_server->protocol == SOUP_PROTOCOL_HTTPS)
		pconn->priv->tunnel_dest = soup_uri_copy (origin_server);

	soup_socket_client_connect (SOUP_SOCKET (pconn), proxy_server);
	return (SoupConnection *)pconn;
}


static void
connect_cb (SoupMessage *msg, gpointer user_data)
{
	SoupProxyConnection *pconn = user_data;
	SoupSocket *sock = user_data;
	SoupKnownErrorCode err;

	if (SOUP_MESSAGE_IS_ERROR (msg))
		err = SOUP_ERROR_CANT_CONNECT;
	else if (!soup_socket_start_ssl (SOUP_SOCKET (pconn)))
		err = SOUP_ERROR_CANT_CONNECT;
	else {
		/* Avoid releasing the connection on message free */
		msg->connection = NULL;
		err = SOUP_ERROR_OK;
	}

	pconn->priv->tunneling = TRUE;
	g_signal_emit_by_name (sock, "connected", err);
}

static void
connected (SoupSocket *sock, SoupKnownErrorCode err)
{
	SoupProxyConnection *pconn = SOUP_PROXY_CONNECTION (sock);
	SoupConnection *conn = SOUP_CONNECTION (sock);
	SoupMessage *connect_msg;

	/* Let the signal through if it's an error, or we don't need
	 * to create a tunnel, or we already did create a tunnel.
	 */
	if (err != SOUP_ERROR_OK ||
	    !pconn->priv->tunnel_dest ||
	    pconn->priv->tunneling)
		return;

	g_signal_stop_emission_by_name (sock, "connected");

	/* Handle HTTPS tunnel setup via proxy CONNECT request. */
	connect_msg = soup_message_new (pconn->priv->tunnel_dest,
					SOUP_METHOD_CONNECT);
	connect_msg->connection = conn;
	soup_message_queue (connect_msg, connect_cb, conn);
}


static SoupAddress *
get_remote_address (SoupConnection *conn)
{
	SoupProxyConnection *pconn = SOUP_PROXY_CONNECTION (conn);

	/* A proxy connection only has a dedicated remote address if
	 * it's tunneling. Otherwise it can send requests anywhere.
	 */
	if (pconn->priv->tunnel_dest)
		return SOUP_CONNECTION_CLASS (parent_class)->get_remote_address (conn);
	else
		return NULL;
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
	SoupProxyConnection *pconn = SOUP_PROXY_CONNECTION (conn);

	if (pconn->priv->proxy_ac) {
		soup_auth_context_authorize_message (pconn->priv->proxy_ac, msg);
		soup_message_remove_handler (
			msg, SOUP_HANDLER_PRE_BODY,
			authorize_handler, pconn->priv->proxy_ac);
		soup_message_add_error_code_handler (
			msg, SOUP_ERROR_PROXY_UNAUTHORIZED,
			SOUP_HANDLER_PRE_BODY,
			authorize_handler, pconn->priv->proxy_ac);
	}

	SOUP_CONNECTION_CLASS (parent_class)->start_request (conn, msg);
}
