/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-www-auth-context.c: WWW authentication-management object
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "soup-www-auth-context.h"
#include "soup-private.h"

struct _SoupWWWAuthContextPrivate {
	GHashTable *auth_realms;
	GHashTable *auths;
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
	SoupWWWAuthContext *wac = SOUP_WWW_AUTH_CONTEXT (object);

	wac->priv = g_new0 (SoupWWWAuthContextPrivate, 1);
}

static void
free_path (gpointer path, gpointer realm, gpointer unused)
{
	g_free (path);
}

static void
free_auth (gpointer realm, gpointer auth, gpointer unused)
{
	g_object_unref (auth);
}

static void
finalize (GObject *object)
{
	SoupWWWAuthContext *wac = SOUP_WWW_AUTH_CONTEXT (object);

	if (wac->priv->auth_realms) {
		g_hash_table_foreach (wac->priv->auth_realms, free_path, NULL);
		g_hash_table_destroy (wac->priv->auth_realms);
	}
	if (wac->priv->auths) {
		g_hash_table_foreach (wac->priv->auths, free_auth, NULL);
		g_hash_table_destroy (wac->priv->auths);
	}

	g_free (wac->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	SoupAuthContextClass *auth_context_class =
		SOUP_AUTH_CONTEXT_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	auth_context_class->authorization_header = "Authorization";
	auth_context_class->authenticate_header = "WWW-Authenticate";

	/* virtual method override */
	auth_context_class->add_auth = add_auth;
	auth_context_class->invalidate_auth = invalidate_auth;
	auth_context_class->find_auth_for_realm = find_auth_for_realm;
	auth_context_class->find_auth = find_auth;

	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_www_auth_context, SoupWWWAuthContext, class_init, init, PARENT_TYPE)

SoupAuthContext *
soup_www_auth_context_new (void)
{
	SoupWWWAuthContext *wac;

	wac = g_object_new (SOUP_TYPE_WWW_AUTH_CONTEXT, NULL);

	return (SoupAuthContext *)wac;
}

#define SAME_SERVER(uri1, uri2) (!strcmp (uri1->host, uri2->host) && (uri1->port == uri2->port) && (uri1->protocol == uri2->protocol))

static void
add_auth (SoupAuthContext *ac, const SoupUri *uri, SoupAuth *auth)
{
	SoupWWWAuthContext *wac = SOUP_WWW_AUTH_CONTEXT (ac);
	SoupAuth *old_auth;
	gpointer old_path, old_scheme_realm;
	char *dir_path, *p, *scheme_realm;
	GSList *protection_space, *ps;
	SoupUri *psuri;

	if (!soup_auth_get_realm (auth))
		return;

	if (!wac->priv->auths) {
		wac->priv->auths = g_hash_table_new (g_str_hash, g_str_equal);
		wac->priv->auth_realms = g_hash_table_new (g_str_hash, g_str_equal);
	}

	scheme_realm = g_strdup_printf ("%s:%s",
					soup_auth_get_scheme_name (auth),
					soup_auth_get_realm (auth));

	old_auth = g_hash_table_lookup (wac->priv->auths, scheme_realm);
	if (old_auth)
		auth = old_auth;
	else {
		g_hash_table_insert (wac->priv->auths,
				     g_strdup (scheme_realm), auth);
		g_object_ref (auth);
	}

	protection_space = soup_auth_get_protection_space (auth, uri);
	for (ps = protection_space; ps; ps = ps->next) {
		psuri = ps->data;

		if (!SAME_SERVER (uri, psuri))
			continue;

		dir_path = g_strdup (psuri->path);
		p = strrchr (dir_path, '/');
		if (p && p != dir_path && !p[1])
			*p = '\0';

		if (g_hash_table_lookup_extended (wac->priv->auth_realms,
						  dir_path, &old_path,
						  &old_scheme_realm)) {
			if (!strcmp (old_scheme_realm, scheme_realm)) {
				g_free (dir_path);
				continue;
			}

			g_hash_table_remove (wac->priv->auth_realms, old_path);
			g_free (old_path);
		}

		g_hash_table_insert (wac->priv->auth_realms, dir_path,
				     g_strdup (scheme_realm));
	}

	soup_auth_free_protection_space (auth, protection_space);
	g_free (scheme_realm);
}

static void
invalidate_auth (SoupAuthContext *ac, SoupAuth *auth)
{
	SoupWWWAuthContext *wac = SOUP_WWW_AUTH_CONTEXT (ac);
	char *scheme_realm;
	gpointer key, value;

	SOUP_AUTH_CONTEXT_CLASS (parent_class)->invalidate_auth (ac, auth);

	scheme_realm = g_strdup_printf ("%s:%s",
					soup_auth_get_scheme_name (auth),
					soup_auth_get_realm (auth));

	if (g_hash_table_lookup_extended (wac->priv->auths, scheme_realm,
					  &key, &value) &&
	    auth == (SoupAuth *)value) {
		g_hash_table_remove (wac->priv->auths, scheme_realm);
		g_free (key);
		g_object_unref (auth);
	}
	g_free (scheme_realm);
}

static SoupAuth *
find_auth_for_realm (SoupAuthContext *ac,
		     const char *scheme, const char *realm)
{
	SoupWWWAuthContext *wac = SOUP_WWW_AUTH_CONTEXT (ac);
	char *scheme_realm;
	SoupAuth *auth;

	if (!wac->priv->auths)
		return NULL;

	scheme_realm = g_strdup_printf ("%s:%s", scheme, realm);
	auth = g_hash_table_lookup (wac->priv->auths, scheme_realm);
	g_free (scheme_realm);

	return auth;
}

static SoupAuth *
find_auth (SoupAuthContext *ac, SoupMessage *msg)
{
	SoupWWWAuthContext *wac = SOUP_WWW_AUTH_CONTEXT (ac);
	const SoupUri *uri = soup_message_get_uri (msg);
	char *path, *dir;
	const char *scheme_realm;

	if (!wac->priv->auths)
		return NULL;

	path = g_strdup (uri->path);
	dir = path;

        do {
                scheme_realm = g_hash_table_lookup (wac->priv->auth_realms,
						    path);
		if (scheme_realm)
			break;

                dir = strrchr (path, '/');
                if (dir)
			*dir = '\0';
        } while (dir);

	g_free (path);

	if (!scheme_realm)
		return NULL;
	return g_hash_table_lookup (wac->priv->auths, scheme_realm);
}
