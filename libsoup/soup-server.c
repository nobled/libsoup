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

#include "soup-address.h"
#include "soup-server.h"
#include "soup-headers.h"
#include "soup-private.h"
#include "soup-server-message.h"
#include "soup-transfer.h"

struct _SoupServerPrivate {
	GMainLoop         *loop;

	GHashTable        *handlers;  /* KEY: path, VALUE: SoupServerHandler */
	SoupServerHandler *default_handler;
};

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

static void
init (GObject *object)
{
	SoupServer *server = SOUP_SERVER (object);

	server->priv = g_new0 (SoupServerPrivate, 1);
}

static void 
free_handler (SoupServer *server, SoupServerHandler *hand)
{
	if (hand->unregister)
		(*hand->unregister) (server, hand, hand->user_data);

	if (hand->auth_ctx) {
		g_free ((char *) hand->auth_ctx->basic_info.realm);
		g_free ((char *) hand->auth_ctx->digest_info.realm);
		g_free (hand->auth_ctx);
	}

	g_free ((char *) hand->path);	
	g_free (hand);
}

static gboolean
free_handler_foreach (gpointer key, gpointer hand, gpointer server)
{
	free_handler (server, hand);
	return TRUE;
}

static void
finalize (GObject *object)
{
	SoupServer *serv = SOUP_SERVER (object);

	if (serv->priv->default_handler)
		free_handler (serv, serv->priv->default_handler);
	if (serv->priv->handlers) {
		g_hash_table_foreach_remove (serv->priv->handlers, 
					     free_handler_foreach, 
					     serv);
		g_hash_table_destroy (serv->priv->handlers);
	}

	if (serv->priv->loop)
		g_main_destroy (serv->priv->loop);

	g_free (serv->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_server, SoupServer, class_init, init, PARENT_TYPE)


void
soup_server_handle_request (SoupServer *serv, SoupMessage *msg, const char *path)
{
	SoupServerHandler *hand;
	SoupServerAuth *auth = NULL;

	msg->status = SOUP_STATUS_FINISHED;

	hand = soup_server_get_handler (serv, path);
	if (!hand) {
		soup_message_set_error (msg, SOUP_ERROR_NOT_FOUND);

		msg->response.owner = SOUP_BUFFER_STATIC;
		msg->response.body = NULL;
		msg->response.length = 0;
		return;
	}

	if (hand->auth_ctx) {
		SoupServerAuthContext *auth_ctx = hand->auth_ctx;
		const GSList *auth_hdrs;

		auth_hdrs = soup_message_get_header_list (msg->request_headers,
							  "Authorization");
		auth = soup_server_auth_new (auth_ctx, auth_hdrs, msg);

		if (auth_ctx->callback) {
			gboolean ret = FALSE;

			ret = (*auth_ctx->callback) (auth_ctx, 
						     auth, 
						     msg, 
						     auth_ctx->user_data);
			if (!ret) {
				soup_server_auth_context_challenge (
					auth_ctx,
					msg,
					"WWW-Authenticate");

				if (!msg->errorcode) {
					soup_message_set_error (
						msg,
						SOUP_ERROR_UNAUTHORIZED);
				}
				return;
			}
		} else if (msg->errorcode) {
			soup_server_auth_context_challenge (
				auth_ctx,
				msg,
				"WWW-Authenticate");
			return;
		}
	}

	if (hand->callback) {
		SoupServerContext servctx = {
			msg,
			soup_message_get_uri (msg)->path,
			soup_method_get_id (msg->method),
			auth,
			serv,
			hand
		};

		/* Call method handler */
		(*hand->callback) (&servctx, msg, hand->user_data);
	}

	if (auth)
		soup_server_auth_free (auth);
}

void
soup_server_run_async (SoupServer *server)
{
	g_return_if_fail (SOUP_IS_SERVER (server));

	SOUP_SERVER_GET_CLASS (server)->run_async (server);
}

void
soup_server_run (SoupServer *server)
{
	g_return_if_fail (SOUP_IS_SERVER (server));

	if (!server->priv->loop) {
		server->priv->loop = g_main_new (TRUE);
		soup_server_run_async (server);
	}

	if (server->priv->loop)
		g_main_run (server->priv->loop);
}

void 
soup_server_quit (SoupServer *server)
{
	g_return_if_fail (SOUP_IS_SERVER (server));

	g_main_quit (server->priv->loop);
	g_object_unref (server);
}

static void
append_handler (gpointer key, gpointer value, gpointer user_data)
{
	GSList **ret = user_data;

	*ret = g_slist_append (*ret, value);
}

GSList *
soup_server_list_handlers (SoupServer *server)
{
	GSList *ret = NULL;

	g_hash_table_foreach (server->priv->handlers, append_handler, &ret);

	return ret;
}

SoupServerHandler *
soup_server_get_handler (SoupServer *server, const char *path)
{
	char *mypath, *dir;
	SoupServerHandler *hand = NULL;

	g_return_val_if_fail (SOUP_IS_SERVER (server), NULL);

	if (!path || !server->priv->handlers)
		return server->priv->default_handler;

	mypath = g_strdup (path);

	dir = strchr (mypath, '?');
	if (dir) *dir = '\0';

	dir = mypath;

	do {
		hand = g_hash_table_lookup (server->priv->handlers, mypath);
		if (hand) {
			g_free (mypath);
			return hand;
		}

		dir = strrchr (mypath, '/');
		if (dir) *dir = '\0';
	} while (dir);

	g_free (mypath);

	return server->priv->default_handler;
}

SoupAddress *
soup_server_context_get_client_address (SoupServerContext *context)
{
	g_return_val_if_fail (context != NULL, NULL);

	return ((SoupServerMessage *)context->msg)->client;
}

const char *
soup_server_context_get_client_host (SoupServerContext *context)
{
	SoupAddress *address;

	address = soup_server_context_get_client_address (context);
	if (address)
		return soup_address_get_physical (address);
	else
		return NULL;
}

static SoupServerAuthContext *
auth_context_copy (SoupServerAuthContext *auth_ctx)
{
	SoupServerAuthContext *new_auth_ctx = NULL;

	new_auth_ctx = g_new0 (SoupServerAuthContext, 1);

	new_auth_ctx->types = auth_ctx->types;
	new_auth_ctx->callback = auth_ctx->callback;
	new_auth_ctx->user_data = auth_ctx->user_data;

	new_auth_ctx->basic_info.realm = 
		g_strdup (auth_ctx->basic_info.realm);

	new_auth_ctx->digest_info.realm = 
		g_strdup (auth_ctx->digest_info.realm);
	new_auth_ctx->digest_info.allow_algorithms = 
		auth_ctx->digest_info.allow_algorithms;
	new_auth_ctx->digest_info.force_integrity = 
		auth_ctx->digest_info.force_integrity;

	return new_auth_ctx;
}

void  
soup_server_register (SoupServer            *server,
		      const char            *path,
		      SoupServerAuthContext *auth_ctx,
		      SoupServerCallbackFn   callback,
		      SoupServerUnregisterFn unregister,
		      gpointer               user_data)
{
	SoupServerHandler *new_hand;
	SoupServerAuthContext *new_auth_ctx = NULL;

	g_return_if_fail (SOUP_IS_SERVER (server));
	g_return_if_fail (callback != NULL);

	if (auth_ctx)
		new_auth_ctx = auth_context_copy (auth_ctx);

	new_hand = g_new0 (SoupServerHandler, 1);
	new_hand->path       = g_strdup (path);
	new_hand->auth_ctx   = new_auth_ctx;
	new_hand->callback   = callback;
	new_hand->unregister = unregister;
	new_hand->user_data  = user_data;

	if (path) {
		if (!server->priv->handlers) {
			server->priv->handlers =
				g_hash_table_new (g_str_hash, g_str_equal);
		} else 
			soup_server_unregister (server, new_hand->path);

		g_hash_table_insert (server->priv->handlers, 
				     (char *) new_hand->path, 
				     new_hand);
	} else {
		soup_server_unregister (server, NULL);
		server->priv->default_handler = new_hand;
	}
}

void  
soup_server_unregister (SoupServer *server, const char *path)
{
	SoupServerHandler *hand;

	g_return_if_fail (SOUP_IS_SERVER (server));

	if (!path) {
		if (server->priv->default_handler) {
			free_handler (server, server->priv->default_handler);
			server->priv->default_handler = NULL;
		}
		return;
	}

	if (!server->priv->handlers) 
		return;

	hand = g_hash_table_lookup (server->priv->handlers, path);
	if (hand) {
		g_hash_table_remove (server->priv->handlers, path);
		free_handler (server, hand);
	}
}
