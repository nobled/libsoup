/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-auth-basic.c: HTTP Basic Authentication
 *
 * Copyright (C) 2001-2002, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "soup-auth-basic.h"
#include "soup-headers.h"
#include "soup-message.h"
#include "soup-misc.h"
#include "soup-private.h"
#include "soup-uri.h"

static GSList *get_protection_space (SoupAuth *auth, const SoupUri *source_uri);
static void authenticate (SoupAuth *auth, const char *username, const char *password);
static char *get_authorization (SoupAuth *auth, SoupMessage *msg);

struct _SoupAuthBasicPrivate {
	char *token;
};

#define PARENT_TYPE SOUP_TYPE_AUTH
static SoupAuthClass *parent_class;

static void
init (GObject *object)
{
	SoupAuthBasic *basic = SOUP_AUTH_BASIC (object);

	basic->priv = g_new0 (SoupAuthBasicPrivate, 1);
}

static void
finalize (GObject *object)
{
	SoupAuthBasic *basic = SOUP_AUTH_BASIC (object);

	g_free (basic->priv->token);
	g_free (basic->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	SoupAuthClass *auth_class = SOUP_AUTH_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	auth_class->scheme_name = "Basic";

	auth_class->get_protection_space = get_protection_space;
	auth_class->authenticate = authenticate;
	auth_class->get_authorization = get_authorization;

	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_auth_basic, SoupAuthBasic, class_init, init, PARENT_TYPE)


static GSList *
get_protection_space (SoupAuth *auth, const SoupUri *source_uri)
{
	SoupUri *psuri;
	char *p;

	psuri = soup_uri_copy (source_uri);

	/* Strip query and filename component */
	g_free (psuri->query);
	psuri->query = NULL;

	p = strrchr (psuri->path, '/');
	if (p && p != psuri->path && p[1])
		*p = '\0';

	return g_slist_prepend (NULL, psuri);
}

static void
authenticate (SoupAuth *auth, const char *username, const char *password)
{
	SoupAuthBasic *basic = SOUP_AUTH_BASIC (auth);
	char *user_pass;
	int len;

	g_return_if_fail (username != NULL);
	g_return_if_fail (password != NULL);

	SOUP_AUTH_CLASS (parent_class)->authenticate (auth, username, password);

	user_pass = g_strdup_printf ("%s:%s", username, password);
	len = strlen (user_pass);

	basic->priv->token = soup_base64_encode (user_pass, len);

	memset (user_pass, 0, len);
	g_free (user_pass);
}

static char *
get_authorization (SoupAuth *auth, SoupMessage *msg)
{
	SoupAuthBasic *basic = SOUP_AUTH_BASIC (auth);

	return g_strdup_printf ("Basic %s", basic->priv->token);
}
