/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-proxy-auth-context.c: Proxy authentication-management object
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "soup-proxy-auth-context.h"
#include "soup-private.h"

struct _SoupProxyAuthContextPrivate {
	SoupAuth *auth;
};

#define PARENT_TYPE SOUP_TYPE_AUTH_CONTEXT
static SoupAuthContext *parent_class;

static void add_auth (SoupAuthContext *ac, const SoupUri *uri, SoupAuth *auth);
static void invalidate_auth (SoupAuthContext *ac, SoupAuth *auth);
static SoupAuth *find_auth_for_realm (SoupAuthContext *ac, const char *scheme, const char *realm);
static SoupAuth *find_auth (SoupAuthContext *ac, SoupMessage *msg);

static void
init (GObject *object)
{
	SoupProxyAuthContext *pac = SOUP_PROXY_AUTH_CONTEXT (object);

	pac->priv = g_new0 (SoupProxyAuthContextPrivate, 1);
}

static void
finalize (GObject *object)
{
	SoupProxyAuthContext *pac = SOUP_PROXY_AUTH_CONTEXT (object);

	if (pac->priv->auth)
		g_object_unref (pac->priv->auth);
	g_free (pac->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	SoupAuthContextClass *auth_context_class =
		SOUP_AUTH_CONTEXT_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	auth_context_class->authorization_header = "Proxy-Authorization";
	auth_context_class->authenticate_header = "Proxy-Authenticate";

	/* virtual method override */
	auth_context_class->add_auth = add_auth;
	auth_context_class->invalidate_auth = invalidate_auth;
	auth_context_class->find_auth_for_realm = find_auth_for_realm;
	auth_context_class->find_auth = find_auth;

	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_proxy_auth_context, SoupProxyAuthContext, class_init, init, PARENT_TYPE)

SoupAuthContext *
soup_proxy_auth_context_new (void)
{
	return g_object_new (SOUP_TYPE_PROXY_AUTH_CONTEXT, NULL);
}

static void
add_auth (SoupAuthContext *ac, const SoupUri *uri, SoupAuth *auth)
{
	SoupProxyAuthContext *pac = SOUP_PROXY_AUTH_CONTEXT (ac);

	if (pac->priv->auth)
		soup_auth_context_invalidate_auth (ac, pac->priv->auth);
	pac->priv->auth = auth;
	g_object_ref (auth);
}

static void
invalidate_auth (SoupAuthContext *ac, SoupAuth *auth)
{
	SoupProxyAuthContext *pac = SOUP_PROXY_AUTH_CONTEXT (ac);

	SOUP_AUTH_CONTEXT_CLASS (parent_class)->invalidate_auth (ac, auth);

	if (auth == pac->priv->auth) {
		g_object_unref (auth);
		pac->priv->auth = NULL;
	}
}

static SoupAuth *
find_auth_for_realm (SoupAuthContext *ac,
		     const char *scheme, const char *realm)
{
	SoupProxyAuthContext *pac = SOUP_PROXY_AUTH_CONTEXT (ac);

	if (!pac->priv->auth)
		return NULL;

	if (strcmp (soup_auth_get_scheme_name (pac->priv->auth), scheme) != 0)
		return NULL;
	if (strcmp (soup_auth_get_realm (pac->priv->auth), realm) != 0)
		return NULL;
	return pac->priv->auth;
}

static SoupAuth *
find_auth (SoupAuthContext *ac, SoupMessage *msg)
{
	SoupProxyAuthContext *pac = SOUP_PROXY_AUTH_CONTEXT (ac);

	return pac->priv->auth;
}

