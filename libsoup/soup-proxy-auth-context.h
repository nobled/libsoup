/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_PROXY_AUTH_CONTEXT_H
#define SOUP_PROXY_AUTH_CONTEXT_H 1

#include <libsoup/soup-auth-context.h>

#define SOUP_TYPE_PROXY_AUTH_CONTEXT            (soup_proxy_auth_context_get_type ())
#define SOUP_PROXY_AUTH_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_PROXY_AUTH_CONTEXT, SoupProxyAuthContext))
#define SOUP_PROXY_AUTH_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_PROXY_AUTH_CONTEXT, SoupProxyAuthContextClass))
#define SOUP_IS_PROXY_AUTH_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_PROXY_AUTH_CONTEXT))
#define SOUP_IS_PROXY_AUTH_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_PROXY_AUTH_CONTEXT))

struct _SoupProxyAuthContext {
	SoupAuthContext parent;

	SoupProxyAuthContextPrivate *priv;
};

struct _SoupProxyAuthContextClass {
	SoupAuthContextClass parent_class;

};

GType            soup_proxy_auth_context_get_type (void);

SoupAuthContext *soup_proxy_auth_context_new      (void);


#endif /* SOUP_PROXY_AUTH_CONTEXT_H */
