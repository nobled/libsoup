/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-context.c: Asyncronous Callback-based HTTP Request Queue.
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

#include "soup-connection.h"
#include "soup-context.h"
#include "soup-misc.h"
#include "soup-private.h"
#include "soup-proxy-connection.h"
#include "soup-socket.h"
#include "soup-ssl.h"
#include "soup-www-auth-context.h"

GHashTable *soup_hosts;  /* KEY, VALUE: SoupHost */

/**
 * soup_context_get:
 * @uri: the stringified URI.
 *
 * Returns a pointer to the %SoupContext representing @uri. If a context
 * already exists for the URI, it is returned with an added reference.
 * Otherwise, a new context is created with a reference count of one.
 *
 * Return value: a %SoupContext representing @uri.
 */
SoupContext *
soup_context_get (const gchar *uri)
{
	SoupUri *suri;
	SoupContext *con;

	g_return_val_if_fail (uri != NULL, NULL);

	suri = soup_uri_new (uri);
	if (!suri) return NULL;

	con = soup_context_from_uri (suri);
	soup_uri_free (suri);

	return con;
}

/**
 * soup_context_uri_hash:
 * @key: a %SoupUri
 *
 * Return value: Hash value of the user, passwd, and path fields in
 * @key.
 **/
static guint
soup_context_uri_hash (gconstpointer key)
{
	const SoupUri *uri = key;
	guint ret;

	ret = uri->protocol;
	if (uri->path)
		ret += g_str_hash (uri->path);
	if (uri->user)
		ret += g_str_hash (uri->user);
	if (uri->passwd)
		ret += g_str_hash (uri->passwd);

	return ret;
}

static inline gboolean
parts_equal (const char *one, const char *two)
{
	if (!one && !two)
		return TRUE;
	if (!one || !two)
		return FALSE;
	return !strcmp (one, two);
}

/**
 * soup_context_uri_equal:
 * @v1: a %SoupUri
 * @v2: a %SoupUri
 *
 * Return value: TRUE if @v1 and @v2 match in user, passwd, and path.
 * Otherwise, FALSE.
 **/
static gboolean
soup_context_uri_equal (gconstpointer v1, gconstpointer v2)
{
	const SoupUri *one = v1;
	const SoupUri *two = v2;

	if (one->protocol != two->protocol)
		return FALSE;
	if (!parts_equal (one->path, two->path))
		return FALSE;
	if (!parts_equal (one->user, two->user))
		return FALSE;
	if (!parts_equal (one->passwd, two->passwd))
		return FALSE;

	return TRUE;
}

static guint
soup_context_host_hash (gconstpointer key)
{
	const SoupHost *host = key;

	return soup_str_case_hash (host->host) + host->port;
}

static gboolean
soup_context_host_equal (gconstpointer v1, gconstpointer v2)
{
	const SoupHost *one = v1;
	const SoupHost *two = v2;

	return (one->port == two->port) &&
		!g_strcasecmp (one->host, two->host);
}

static gboolean
authenticate (SoupAuthContext *ac, SoupAuth *auth, SoupMessage *msg)
{
	const SoupUri *uri = soup_message_get_uri (msg);

	if (uri->user && uri->passwd) {
		soup_auth_authenticate (auth, uri->user, uri->passwd);
		return TRUE;
	} else
		return FALSE;
}

/**
 * soup_context_from_uri:
 * @suri: a %SoupUri.
 *
 * Returns a pointer to the %SoupContext representing @suri. If a context
 * already exists for the URI, it is returned with an added reference.
 * Otherwise, a new context is created with a reference count of one.
 *
 * Return value: a %SoupContext representing @uri.
 */
SoupContext *
soup_context_from_uri (SoupUri *suri)
{
	SoupHost *serv = NULL;
	SoupContext *ret;

	g_return_val_if_fail (suri != NULL, NULL);
	g_return_val_if_fail (suri->protocol != 0, NULL);

	if (!soup_hosts)
		soup_hosts = g_hash_table_new (soup_context_host_hash,
					       soup_context_host_equal);
	else {
		SoupHost tmp;

		tmp.host = suri->host;
		tmp.port = suri->port;
		serv = g_hash_table_lookup (soup_hosts, &tmp);
	}

	if (!serv) {
		serv = g_new0 (SoupHost, 1);
		serv->host = g_strdup (suri->host);
		serv->port = suri->port;
		serv->contexts = g_hash_table_new (soup_context_uri_hash,
						   soup_context_uri_equal);
		serv->ac = soup_www_auth_context_new (); /* FIXME: proxy */
		g_signal_connect (serv->ac, "authenticate",
				  G_CALLBACK (authenticate), NULL);
		g_hash_table_insert (soup_hosts, serv, serv);
	}

	ret = g_hash_table_lookup (serv->contexts, suri);
	if (!ret) {
		ret = g_new0 (SoupContext, 1);
		ret->server = serv;
		ret->uri = soup_uri_copy (suri);
		ret->refcnt = 0;

		g_hash_table_insert (serv->contexts, ret->uri, ret);
	}

	soup_context_ref (ret);

	return ret;
}

/**
 * soup_context_ref:
 * @ctx: a %SoupContext.
 *
 * Adds a reference to @ctx.
 */
void
soup_context_ref (SoupContext *ctx)
{
	g_return_if_fail (ctx != NULL);

	ctx->refcnt++;
}

/**
 * soup_context_unref:
 * @ctx: a %SoupContext.
 *
 * Decrement the reference count on @ctx. If the reference count reaches
 * zero, the %SoupContext is freed. If this is the last context for a
 * given server address, any open connections are closed.
 */
void
soup_context_unref (SoupContext *ctx)
{
	g_return_if_fail (ctx != NULL);

	--ctx->refcnt;

	if (ctx->refcnt == 0) {
		SoupHost *serv = ctx->server;

		g_hash_table_remove (serv->contexts, ctx->uri);

		if (g_hash_table_size (serv->contexts) == 0) {
			/*
			 * Remove this host from the active hosts hash
			 */
			g_hash_table_remove (soup_hosts, serv);

			g_object_unref (serv->ac);
			g_hash_table_destroy (serv->contexts);
			g_free (serv->host);
			g_free (serv);
		}

		soup_uri_free (ctx->uri);
		g_free (ctx);
	}
}

static void
connection_destroyed (gpointer data, GObject *where_conn_was)
{
	SoupContext *ctx = data;

	ctx->server->connections = g_slist_remove (ctx->server->connections,
						   where_conn_was);
	soup_context_unref (ctx);
}

struct SoupContextConnectData {
	SoupContextConnectFn  cb;
	gpointer              user_data;

	SoupContext          *ctx;
	guint                 timeout_tag;
	SoupConnection       *connection;
};

static gboolean retry_connect_timeout_cb (struct SoupContextConnectData *data);

static void
soup_context_connect_cb (SoupConnection     *conn,
			 SoupKnownErrorCode  status,
			 gpointer            user_data)
{
	struct SoupContextConnectData *data = user_data;
	SoupContext *ctx = data->ctx;

	switch (status) {
	case SOUP_ERROR_OK:
		ctx->server->connections =
			g_slist_prepend (ctx->server->connections, conn);
		g_object_weak_ref (G_OBJECT (conn), connection_destroyed, ctx);
		soup_context_ref (ctx);
		soup_connection_set_in_use (conn, TRUE);
		break;

	case SOUP_ERROR_CANT_RESOLVE:
	case SOUP_ERROR_CANT_RESOLVE_PROXY:
		break;

	default:
		/*
		 * Check if another connection exists to this server
		 * before reporting error. 
		 */
		if (ctx->server->connections) {
			data->timeout_tag =
				g_timeout_add (
					150,
					(GSourceFunc) retry_connect_timeout_cb,
					data);
			return;
		}
		break;
	}

	(*data->cb) (ctx, status, conn, data->user_data);

	soup_context_unref (ctx);
	g_free (data);
}

static gboolean
try_existing_connections (SoupContext          *ctx,
			  SoupContextConnectFn  cb,
			  gpointer              user_data)
{
	GSList *conns = ctx->server->connections;

	while (conns) {
		SoupConnection *conn = conns->data;

		if (!soup_connection_is_in_use (conn)) {
			soup_connection_set_in_use (conn, TRUE);

			/* Issue success callback */
			(*cb) (ctx, SOUP_ERROR_OK, conn, user_data);
			return TRUE;
		}

		conns = conns->next;
	}

	return FALSE;
}

static gboolean
try_create_connection (struct SoupContextConnectData *data)
{
	SoupContext *ctx = data->ctx;
	SoupContext *proxy;

	data->timeout_tag = 0;

	proxy = soup_get_proxy ();
	if (proxy) {
		data->connection =
			soup_proxy_connection_new (ctx->uri,
						   ctx->server->ac,
						   proxy->uri,
						   proxy->server->ac);
	} else {
		data->connection =
			soup_connection_new (ctx->uri, ctx->server->ac);
	}
	g_signal_connect (data->connection, "connected",
			  G_CALLBACK (soup_context_connect_cb), data);

	return TRUE;
}

static gboolean
retry_connect_timeout_cb (struct SoupContextConnectData *data)
{
	if (try_existing_connections (data->ctx, 
				      data->cb, 
				      data->user_data)) {
		soup_context_unref (data->ctx);
		g_free (data);
		return FALSE;
	}

	return try_create_connection (data) == FALSE;
}

/**
 * soup_context_get_connection:
 * @ctx: a %SoupContext.
 * @cb: a %SoupContextConnectFn to be called when a valid connection
 * is available.
 * @user_data: the user_data passed to @cb.
 *
 * Initiates the process of establishing a network connection to the
 * server referenced in @ctx. If an existing connection is available
 * and not in use, @cb is called immediately, and a %SoupConnectId of
 * %NULL is returned. Otherwise, a new connection is established. If
 * the current connection count exceeds that set in
 * soup_set_connection_limit(), the new connection is not created
 * until an existing connection is closed.
 *
 * Once a network connection is successfully established, or an existing
 * connection becomes available for use, @cb is called, passing the
 * allocated %SoupConnection.
 *
 * Return value: a %SoupConnectId which can be used to cancel a connection
 * attempt using %soup_context_cancel_connect.
 */
SoupConnectId
soup_context_get_connection (SoupContext          *ctx,
			     SoupContextConnectFn  cb,
			     gpointer              user_data)
{
	struct SoupContextConnectData *data;

	g_return_val_if_fail (ctx != NULL, NULL);

	/* Look for an existing unused connection */
	if (try_existing_connections (ctx, cb, user_data))
		return NULL;

	data = g_new0 (struct SoupContextConnectData, 1);
	data->cb = cb;
	data->user_data = user_data;

	data->ctx = ctx;
	soup_context_ref (ctx);

	if (!try_create_connection (data))
		data->timeout_tag =
			g_timeout_add (150,
				       (GSourceFunc) retry_connect_timeout_cb,
				       data);

	return data;
}

/**
 * soup_context_cancel_connect:
 * @tag: a %SoupConnectId representing a connection in progress.
 *
 * Cancels the connection attempt represented by @tag. The
 * %SoupContextConnectFn passed in %soup_context_get_connection is not
 * called.
 */
void
soup_context_cancel_connect (SoupConnectId tag)
{
	struct SoupContextConnectData *data = tag;

	g_return_if_fail (data != NULL);

	if (data->timeout_tag)
		g_source_remove (data->timeout_tag);
	else if (data->connection)
		g_object_unref (data->connection);

	g_free (data);
}

/**
 * soup_context_get_uri:
 * @ctx: a %SoupContext.
 *
 * Returns a pointer to the %SoupUri represented by @ctx.
 *
 * Return value: the %SoupUri for @ctx.
 */
const SoupUri *
soup_context_get_uri (SoupContext *ctx)
{
	g_return_val_if_fail (ctx != NULL, NULL);
	return ctx->uri;
}
