/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_SERVER_TCP_H
#define SOUP_SERVER_TCP_H 1

#include <libsoup/soup-server.h>

#define SOUP_TYPE_SERVER_TCP            (soup_server_tcp_get_type ())
#define SOUP_SERVER_TCP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_SERVER_TCP, SoupServerTCP))
#define SOUP_SERVER_TCP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_SERVER_TCP, SoupServerTCPClass))
#define SOUP_IS_SERVER_TCP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_SERVER_TCP))
#define SOUP_IS_SERVER_TCP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_SERVER_TCP))

struct _SoupServerTCP {
	SoupServer parent;

	SoupServerTCPPrivate *priv;
};

struct _SoupServerTCPClass {
	SoupServerClass parent_class;

};

GType              soup_server_tcp_get_type     (void);

SoupServer        *soup_server_tcp_new          (SoupProtocol       proto,
						 SoupAddressFamily  family,
						 gushort            port);

SoupProtocol       soup_server_tcp_get_protocol (SoupServerTCP     *tcp);
SoupAddressFamily  soup_server_tcp_get_family   (SoupServerTCP     *tcp);
gushort            soup_server_tcp_get_port     (SoupServerTCP     *tcp);

#endif /* SOUP_SERVER_TCP_H */
