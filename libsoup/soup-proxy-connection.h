/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_PROXY_CONNECTION_H
#define SOUP_PROXY_CONNECTION_H 1

#include <libsoup/soup-connection.h>

#define SOUP_TYPE_PROXY_CONNECTION            (soup_proxy_connection_get_type ())
#define SOUP_PROXY_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_PROXY_CONNECTION, SoupProxyConnection))
#define SOUP_PROXY_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_PROXY_CONNECTION, SoupProxyConnectionClass))
#define SOUP_IS_PROXY_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_PROXY_CONNECTION))
#define SOUP_IS_PROXY_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_PROXY_CONNECTION))

struct _SoupProxyConnection {
	SoupConnection parent;

	SoupProxyConnectionPrivate *priv;
};

struct _SoupProxyConnectionClass {
	SoupConnectionClass parent_class;

};

GType           soup_proxy_connection_get_type (void);

SoupConnection *soup_proxy_connection_new      (SoupUri         *origin_server,
						SoupAuthContext *origin_ac,
						SoupUri         *proxy_server,
						SoupAuthContext *proxy_ac);

#endif /* SOUP_PROXY_CONNECTION_H */
