/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-auth-digest.c: HTTP Digest Authentication
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "soup-auth-digest.h"
#include "soup-headers.h"
#include "soup-md5-utils.h"
#include "soup-message.h"
#include "soup-misc.h"
#include "soup-uri.h"

static void construct (SoupAuth *auth, const char *header);
static GSList *get_protection_space (SoupAuth *auth, const SoupUri *source_uri);
static const char *get_realm (SoupAuth *auth);
static void authenticate (SoupAuth *auth, const char *username, const char *password);
static gboolean is_authenticated (SoupAuth *auth);
static char *get_authorization (SoupAuth *auth, SoupMessage *msg);

typedef enum {
	QOP_NONE     = 0,
	QOP_AUTH     = 1 << 0,
	QOP_AUTH_INT = 1 << 1
} QOPType;

typedef enum {
	ALGORITHM_MD5      = 1 << 0,
	ALGORITHM_MD5_SESS = 1 << 1
} AlgorithmType;

typedef struct {
	char          *user;
	char           hex_a1[33];

	/* These are provided by the server */
	char          *realm;
	char          *nonce;
	QOPType        qop_options;
	AlgorithmType  algorithm;
	char          *domain;

	/* These are generated by the client */
	char          *cnonce;
	int            nc;
	QOPType        qop;
} SoupAuthDigestPrivate;
#define SOUP_AUTH_DIGEST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SOUP_TYPE_AUTH_DIGEST, SoupAuthDigestPrivate))

G_DEFINE_TYPE (SoupAuthDigest, soup_auth_digest, SOUP_TYPE_AUTH)

static void
soup_auth_digest_init (SoupAuthDigest *digest)
{
}

static void
finalize (GObject *object)
{
	SoupAuthDigestPrivate *priv = SOUP_AUTH_DIGEST_GET_PRIVATE (object);

	if (priv->user)
		g_free (priv->user);
	if (priv->realm)
		g_free (priv->realm);
	if (priv->nonce)
		g_free (priv->nonce);
	if (priv->domain)
		g_free (priv->domain);
	if (priv->cnonce)
		g_free (priv->cnonce);

	G_OBJECT_CLASS (soup_auth_digest_parent_class)->finalize (object);
}

static void
soup_auth_digest_class_init (SoupAuthDigestClass *auth_digest_class)
{
	SoupAuthClass *auth_class = SOUP_AUTH_CLASS (auth_digest_class);
	GObjectClass *object_class = G_OBJECT_CLASS (auth_digest_class);

	g_type_class_add_private (auth_digest_class, sizeof (SoupAuthDigestPrivate));

	auth_class->scheme_name = "Digest";

	auth_class->get_protection_space = get_protection_space;
	auth_class->get_realm = get_realm;
	auth_class->construct = construct;
	auth_class->authenticate = authenticate;
	auth_class->is_authenticated = is_authenticated;
	auth_class->get_authorization = get_authorization;

	object_class->finalize = finalize;
}

typedef struct {
	char *name;
	guint type;
} DataType;

static DataType qop_types[] = {
	{ "auth",     QOP_AUTH     },
	{ "auth-int", QOP_AUTH_INT }
};

static DataType algorithm_types[] = {
	{ "MD5",      ALGORITHM_MD5      },
	{ "MD5-sess", ALGORITHM_MD5_SESS }
};

static guint
decode_data_type (DataType *dtype, const char *name)
{
        int i;

	if (!name)
		return 0;

        for (i = 0; dtype[i].name; i++) {
                if (!g_ascii_strcasecmp (dtype[i].name, name))
			return dtype[i].type;
        }

	return 0;
}

static inline guint
decode_qop (const char *name)
{
	return decode_data_type (qop_types, name);
}

static inline guint
decode_algorithm (const char *name)
{
	return decode_data_type (algorithm_types, name);
}

static void
construct (SoupAuth *auth, const char *header)
{
	SoupAuthDigestPrivate *priv = SOUP_AUTH_DIGEST_GET_PRIVATE (auth);
	GHashTable *tokens;
	const char *ptr;
	char *tmp;

	header += sizeof ("Digest");

	tokens = soup_header_param_parse_list (header);
	if (!tokens)
		return;

	priv->nc = 1;
	/* We're just going to do qop=auth for now */
	priv->qop = QOP_AUTH;

	priv->realm = soup_header_param_copy_token (tokens, "realm");
	priv->domain = soup_header_param_copy_token (tokens, "domain");
	priv->nonce = soup_header_param_copy_token (tokens, "nonce");

	tmp = soup_header_param_copy_token (tokens, "qop");
	ptr = tmp;

	while (ptr && *ptr) {
		char *token;

		token = soup_header_param_decode_token (&ptr);
		if (token)
			priv->qop_options |= decode_qop (token);
		g_free (token);

		if (*ptr == ',')
			ptr++;
	}
	g_free (tmp);

	tmp = soup_header_param_copy_token (tokens, "algorithm");
	priv->algorithm = decode_algorithm (tmp);
	g_free (tmp);

	soup_header_param_destroy_hash (tokens);
}

static GSList *
get_protection_space (SoupAuth *auth, const SoupUri *source_uri)
{
	SoupAuthDigestPrivate *priv = SOUP_AUTH_DIGEST_GET_PRIVATE (auth);
	GSList *space = NULL;
	SoupUri *uri;
	char **dvec, *d, *dir, *slash;
	int dix;

	if (!priv->domain || !*priv->domain) {
		/* If no domain directive, the protection space is the
		 * whole server.
		 */
		return g_slist_prepend (NULL, g_strdup (""));
	}

	dvec = g_strsplit (priv->domain, " ", 0);
	for (dix = 0; dvec[dix] != NULL; dix++) {
		d = dvec[dix];
		if (*d == '/')
			dir = g_strdup (d);
		else {
			uri = soup_uri_new (d);
			if (uri && uri->protocol == source_uri->protocol &&
			    uri->port == source_uri->port &&
			    !strcmp (uri->host, source_uri->host))
				dir = g_strdup (uri->path);
			else
				dir = NULL;
			if (uri)
				soup_uri_free (uri);
		}

		if (dir) {
			slash = strrchr (dir, '/');
			if (slash && !slash[1])
				*slash = '\0';

			space = g_slist_prepend (space, dir);
		}
	}
	g_strfreev (dvec);

	return space;
}

static const char *
get_realm (SoupAuth *auth)
{
	return SOUP_AUTH_DIGEST_GET_PRIVATE (auth)->realm;
}

static void
authenticate (SoupAuth *auth, const char *username, const char *password)
{
	SoupAuthDigestPrivate *priv = SOUP_AUTH_DIGEST_GET_PRIVATE (auth);
	SoupMD5Context ctx;
	guchar d[16];
	char *bgen;

	g_return_if_fail (username != NULL);

	bgen = g_strdup_printf ("%p:%lu:%lu",
				auth,
				(unsigned long) getpid (),
				(unsigned long) time (0));
	priv->cnonce = soup_base64_encode (bgen, strlen (bgen));
	g_free (bgen);

	priv->user = g_strdup (username);

	/* compute A1 */
	soup_md5_init (&ctx);

	soup_md5_update (&ctx, username, strlen (username));

	soup_md5_update (&ctx, ":", 1);
	if (priv->realm) {
		soup_md5_update (&ctx, priv->realm,
				 strlen (priv->realm));
	}

	soup_md5_update (&ctx, ":", 1);
	if (password)
		soup_md5_update (&ctx, password, strlen (password));

	if (priv->algorithm == ALGORITHM_MD5_SESS) {
		soup_md5_final (&ctx, d);

		soup_md5_init (&ctx);
		soup_md5_update (&ctx, d, 16);
		soup_md5_update (&ctx, ":", 1);
		soup_md5_update (&ctx, priv->nonce,
				 strlen (priv->nonce));
		soup_md5_update (&ctx, ":", 1);
		soup_md5_update (&ctx, priv->cnonce,
				 strlen (priv->cnonce));
	}

	/* hexify A1 */
	soup_md5_final_hex (&ctx, priv->hex_a1);
}

static gboolean
is_authenticated (SoupAuth *auth)
{
	return SOUP_AUTH_DIGEST_GET_PRIVATE (auth)->cnonce != NULL;
}

static char *
compute_response (SoupAuthDigestPrivate *priv, SoupMessage *msg)
{
	char hex_a2[33], o[33];
	SoupMD5Context md5;
	char *url;
	const SoupUri *uri;

	uri = soup_message_get_uri (msg);
	g_return_val_if_fail (uri != NULL, NULL);
	url = soup_uri_to_string (uri, TRUE);

	/* compute A2 */
	soup_md5_init (&md5);
	soup_md5_update (&md5, msg->method, strlen (msg->method));
	soup_md5_update (&md5, ":", 1);
	soup_md5_update (&md5, url, strlen (url));

	g_free (url);

	if (priv->qop == QOP_AUTH_INT) {
		/* FIXME: Actually implement. Ugh. */
		soup_md5_update (&md5, ":", 1);
		soup_md5_update (&md5, "00000000000000000000000000000000", 32);
	}

	/* now hexify A2 */
	soup_md5_final_hex (&md5, hex_a2);

	/* compute KD */
	soup_md5_init (&md5);
	soup_md5_update (&md5, priv->hex_a1, 32);
	soup_md5_update (&md5, ":", 1);
	soup_md5_update (&md5, priv->nonce,
			 strlen (priv->nonce));
	soup_md5_update (&md5, ":", 1);

	if (priv->qop) {
		char *tmp;

		tmp = g_strdup_printf ("%.8x", priv->nc);

		soup_md5_update (&md5, tmp, strlen (tmp));
		g_free (tmp);
		soup_md5_update (&md5, ":", 1);
		soup_md5_update (&md5, priv->cnonce,
				 strlen (priv->cnonce));
		soup_md5_update (&md5, ":", 1);

		if (priv->qop == QOP_AUTH)
			tmp = "auth";
		else if (priv->qop == QOP_AUTH_INT)
			tmp = "auth-int";
		else
			g_assert_not_reached ();

		soup_md5_update (&md5, tmp, strlen (tmp));
		soup_md5_update (&md5, ":", 1);
	}

	soup_md5_update (&md5, hex_a2, 32);
	soup_md5_final_hex (&md5, o);

	return g_strdup (o);
}

static char *
get_authorization (SoupAuth *auth, SoupMessage *msg)
{
	SoupAuthDigestPrivate *priv = SOUP_AUTH_DIGEST_GET_PRIVATE (auth);
	char *response;
	char *qop = NULL;
	char *nc;
	char *url;
	char *out;
	const SoupUri *uri;

	uri = soup_message_get_uri (msg);
	g_return_val_if_fail (uri != NULL, NULL);
	url = soup_uri_to_string (uri, TRUE);

	response = compute_response (priv, msg);

	if (priv->qop == QOP_AUTH)
		qop = "auth";
	else if (priv->qop == QOP_AUTH_INT)
		qop = "auth-int";
	else
		g_assert_not_reached ();

	nc = g_strdup_printf ("%.8x", priv->nc);

	out = g_strdup_printf (
		"Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", %s%s%s "
		"%s%s%s %s%s%s uri=\"%s\", response=\"%s\"",
		priv->user,
		priv->realm,
		priv->nonce,

		priv->qop ? "cnonce=\"" : "",
		priv->qop ? priv->cnonce : "",
		priv->qop ? "\"," : "",

		priv->qop ? "nc=" : "",
		priv->qop ? nc : "",
		priv->qop ? "," : "",

		priv->qop ? "qop=" : "",
		priv->qop ? qop : "",
		priv->qop ? "," : "",

		url,
		response);

	g_free (response);
	g_free (url);
	g_free (nc);

	priv->nc++;

	return out;
}
