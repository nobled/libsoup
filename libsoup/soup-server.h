/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-server.h: Asyncronous Callback-based HTTP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_SERVER_H
#define SOUP_SERVER_H 1

#include <glib.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-method.h>
#include <libsoup/soup-misc.h>
#include <libsoup/soup-socket.h>
#include <libsoup/soup-uri.h>
#include <libsoup/soup-server-auth.h>

typedef struct _SoupServer SoupServer;
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

SoupServer        *soup_server_new           (SoupProtocol           proto,
					      guint                  port);

SoupServer        *soup_server_cgi           (void);

void               soup_server_ref           (SoupServer            *serv);

void               soup_server_unref         (SoupServer            *serv);

SoupProtocol       soup_server_get_protocol  (SoupServer            *serv);

guint              soup_server_get_port      (SoupServer            *serv);

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

char              *soup_server_context_get_client_host    (SoupServerContext *context);


void               soup_server_message_start      (SoupMessage *msg);

void               soup_server_message_add_data   (SoupMessage   *msg,
						   SoupOwnership  owner,
						   char          *body,
						   gulong         length);

void               soup_server_message_finish     (SoupMessage *msg);

#endif /* SOUP_SERVER_H */
