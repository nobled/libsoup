/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_WWW_AUTH_CONTEXT_H
#define SOUP_WWW_AUTH_CONTEXT_H 1

#include <libsoup/soup-auth-context.h>

#define SOUP_TYPE_WWW_AUTH_CONTEXT            (soup_www_auth_context_get_type ())
#define SOUP_WWW_AUTH_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_WWW_AUTH_CONTEXT, SoupWWWAuthContext))
#define SOUP_WWW_AUTH_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_WWW_AUTH_CONTEXT, SoupWWWAuthContextClass))
#define SOUP_IS_WWW_AUTH_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_WWW_AUTH_CONTEXT))
#define SOUP_IS_WWW_AUTH_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_WWW_AUTH_CONTEXT))

struct _SoupWWWAuthContext {
	SoupAuthContext parent;

	SoupWWWAuthContextPrivate *priv;
};

struct _SoupWWWAuthContextClass {
	SoupAuthContextClass parent_class;

};

GType            soup_www_auth_context_get_type (void);

SoupAuthContext *soup_www_auth_context_new      (void);


#endif /* SOUP_WWW_AUTH_CONTEXT_H */
