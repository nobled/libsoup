/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_SOCKET_H
#define SOUP_SOCKET_H

#include <glib-object.h>
#include <libsoup/soup-error.h>
#include <libsoup/soup-types.h>

#define SOUP_TYPE_SOCKET            (soup_socket_get_type ())
#define SOUP_SOCKET(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_SOCKET, SoupSocket))
#define SOUP_SOCKET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_SOCKET, SoupSocketClass))
#define SOUP_IS_SOCKET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_SOCKET))
#define SOUP_IS_SOCKET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_SOCKET))

struct _SoupSocket {
	GObject parent;

	SoupSocketPrivate *priv;
};

struct _SoupSocketClass {
	GObjectClass parent_class;

};

GType soup_socket_get_type (void);


typedef void (*SoupSocketConnectedFn)       (SoupSocket            *socket,
					     SoupKnownErrorCode     status,
					     gpointer               data);

SoupSocket  *soup_socket_client_new         (const char            *hostname,
					     guint                  port,
					     gboolean               ssl,
					     SoupSocketConnectedFn  func,
					     gpointer               data);

void         soup_socket_client_connect     (SoupSocket            *socket,
					     const char            *hostname,
					     guint                  port,
					     gboolean               ssl,
					     SoupSocketConnectedFn  func,
					     gpointer               data);

GIOChannel  *soup_socket_get_iochannel      (SoupSocket            *socket);

SoupAddress *soup_socket_get_local_address  (SoupSocket            *socket);
guint        soup_socket_get_local_port     (SoupSocket            *socket);
SoupAddress *soup_socket_get_remote_address (SoupSocket            *socket);
guint        soup_socket_get_remote_port    (SoupSocket            *socket);

gboolean     soup_socket_start_ssl          (SoupSocket            *socket);

#define SOUP_SERVER_ANY_PORT 0

SoupSocket  *soup_socket_server_new         (SoupAddress           *local_addr,
					     guint                  local_port,
					     gboolean               ssl);

SoupSocket  *soup_socket_server_accept      (SoupSocket            *socket);

SoupSocket  *soup_socket_server_try_accept  (SoupSocket            *socket);

#endif /* SOUP_SOCKET_H */
