/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_SOCKET_H
#define SOUP_SOCKET_H

#include <glib-object.h>
#include <libsoup/soup-error.h>
#include <libsoup/soup-types.h>
#include <libsoup/soup-uri.h>

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

	/* signals */
	void (*connected)    (SoupSocket *, SoupKnownErrorCode);
};

GType soup_socket_get_type (void);

#define SOUP_SERVER_ANY_PORT 0


SoupSocket    *soup_socket_client_new         (const SoupUri *uri);

void           soup_socket_client_connect     (SoupSocket    *sock,
					       const SoupUri *uri);

SoupSocket    *soup_socket_server_new         (SoupAddress   *local_addr,
					       guint          local_port,
					       gboolean       ssl);

gboolean       soup_socket_start_ssl          (SoupSocket    *sock);

GIOChannel    *soup_socket_get_iochannel      (SoupSocket    *sock);

SoupAddress   *soup_socket_get_local_address  (SoupSocket    *sock);
guint          soup_socket_get_local_port     (SoupSocket    *sock);
SoupAddress   *soup_socket_get_remote_address (SoupSocket    *sock);
guint          soup_socket_get_remote_port    (SoupSocket    *sock);

const SoupUri *soup_socket_get_uri            (SoupSocket    *sock);

SoupSocket    *soup_socket_server_accept      (SoupSocket    *sock);

SoupSocket    *soup_socket_server_try_accept  (SoupSocket    *sock);

#endif /* SOUP_SOCKET_H */
