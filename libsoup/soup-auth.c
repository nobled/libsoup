/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-auth.c: HTTP Authentication scheme framework
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "soup-auth.h"
#include "soup-headers.h"
#include "soup-private.h"

static void parse        (SoupAuth *auth, GHashTable *tokens);
static void authenticate (SoupAuth *auth, const char *username, const char *password);

struct _SoupAuthPrivate {
	char           *realm;
	SoupAuthStatus  status;
};

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

static void
init (GObject *object)
{
	SoupAuth *auth = SOUP_AUTH (object);

	auth->priv = g_new0 (SoupAuthPrivate, 1);
	auth->priv->status = SOUP_AUTH_STATUS_NEW;
}

static void
finalize (GObject *object)
{
	SoupAuth *auth = SOUP_AUTH (object);

	g_free (auth->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	SoupAuthClass *auth_class = SOUP_AUTH_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method definition */
	auth_class->parse = parse;
	auth_class->authenticate = authenticate;

	/* virtual method override */
	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_auth, SoupAuth, class_init, init, PARENT_TYPE)


static void
parse (SoupAuth *auth, GHashTable *tokens)
{
	auth->priv->realm = soup_header_param_copy_token (tokens, "realm");
}

void
soup_auth_parse (SoupAuth *auth, GHashTable *tokens)
{
	g_return_if_fail (SOUP_IS_AUTH (auth));

	SOUP_AUTH_GET_CLASS (auth)->parse (auth, tokens);
}

const char *
soup_auth_get_scheme_name (SoupAuth *auth)
{
	g_return_val_if_fail (SOUP_IS_AUTH (auth), NULL);

	return SOUP_AUTH_GET_CLASS (auth)->scheme_name;
}

const char *
soup_auth_get_realm (SoupAuth *auth)
{
	g_return_val_if_fail (SOUP_IS_AUTH (auth), NULL);

	return auth->priv->realm;
}

GSList *
soup_auth_get_protection_space (SoupAuth *auth, const SoupUri *source_uri)
{
	g_return_val_if_fail (SOUP_IS_AUTH (auth), NULL);
	g_return_val_if_fail (source_uri != NULL, NULL);

	return SOUP_AUTH_GET_CLASS (auth)->get_protection_space (auth, source_uri);
}

void
soup_auth_free_protection_space (SoupAuth *auth, GSList *space)
{
	while (space) {
		soup_uri_free (space->data);
		space = g_slist_remove (space, space->data);
	}
}

static void
authenticate (SoupAuth *auth, const char *username, const char *password)
{
	auth->priv->status = SOUP_AUTH_STATUS_AUTHENTICATED;
}

void
soup_auth_authenticate (SoupAuth *auth,
			const char *username, const char *password)
{
	g_return_if_fail (SOUP_IS_AUTH (auth));

	SOUP_AUTH_GET_CLASS (auth)->authenticate (auth, username, password);
}

void
soup_auth_invalidate (SoupAuth *auth)
{
	g_return_if_fail (SOUP_IS_AUTH (auth));

	auth->priv->status = SOUP_AUTH_STATUS_INVALID;
}

char *
soup_auth_get_authorization (SoupAuth *auth, SoupMessage *msg)
{
	g_return_val_if_fail (SOUP_IS_AUTH (auth), NULL);
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), NULL);

	return SOUP_AUTH_GET_CLASS (auth)->get_authorization (auth, msg);
}

SoupAuthStatus
soup_auth_get_status (SoupAuth *auth)
{
	return auth->priv->status;
}
