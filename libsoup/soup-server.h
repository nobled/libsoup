/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_SERVER_H
#define SOUP_SERVER_H 1

#include <glib-object.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-method.h>
#include <libsoup/soup-misc.h>
#include <libsoup/soup-server-auth.h>
#include <libsoup/soup-socket.h>

#define SOUP_TYPE_SERVER            (soup_server_get_type ())
#define SOUP_SERVER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_SERVER, SoupServer))
#define SOUP_SERVER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_SERVER, SoupServerClass))
#define SOUP_IS_SERVER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_SERVER))
#define SOUP_IS_SERVER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_SERVER))
#define SOUP_SERVER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_SERVER, SoupServerClass))

struct _SoupServer {
	GObject parent;

	SoupServerPrivate *priv;
};

struct _SoupServerClass {
	GObjectClass parent_class;

	void (*run_async) (SoupServer *serv);

};

GType soup_server_get_type (void);


typedef struct _SoupServerHandler SoupServerHandler;

typedef struct {
	SoupMessage       *msg;
	char              *path;
	SoupMethodId       method_id;
	SoupServerAuth    *auth;
	SoupServer        *server;
	SoupServerHandler *handler;
} SoupServerContext;

typedef void (*SoupServerCallbackFn) (SoupServerContext    *context,
				      SoupMessage          *msg, 
				      gpointer              user_data);

typedef void (*SoupServerUnregisterFn) (SoupServer        *server,
					SoupServerHandler *handler,
					gpointer           user_data);

struct _SoupServerHandler {
	const gchar            *path;

	SoupServerAuthContext  *auth_ctx;

	SoupServerCallbackFn    callback;
	SoupServerUnregisterFn  unregister;
	gpointer                user_data;
};

void               soup_server_run           (SoupServer            *serv);

void               soup_server_run_async     (SoupServer            *serv);

void               soup_server_quit          (SoupServer            *serv);

void               soup_server_register      (SoupServer            *serv,
					      const char            *path,
					      SoupServerAuthContext *auth_ctx,
					      SoupServerCallbackFn   callback,
					      SoupServerUnregisterFn unregister,
					      gpointer               user_data);

void               soup_server_unregister    (SoupServer            *serv,
					      const char            *path);

SoupServerHandler *soup_server_get_handler   (SoupServer            *serv,
					      const char            *path);

GSList            *soup_server_list_handlers (SoupServer            *serv);

/* Functions for accessing information about the specific connection */

SoupAddress       *soup_server_context_get_client_address (SoupServerContext *context);

const char        *soup_server_context_get_client_host    (SoupServerContext *context);

/* Protected, for server subclasses */
void               soup_server_handle_request             (SoupServer        *serv,
							   SoupMessage       *msg,
							   const char        *path);


#endif /* SOUP_SERVER_H */
