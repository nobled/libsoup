/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-auth-context.c: Authentication-management object
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "soup-auth-context.h"
#include "soup-auth-basic.h"
#include "soup-auth-digest.h"
#include "soup-headers.h"
#include "soup-marshal.h"
#include "soup-private.h"

struct _SoupAuthContextPrivate {
	GHashTable *message_auths;
};

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

enum {
	AUTHENTICATE,
	REAUTHENTICATE,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

static void message_destroyed (gpointer, GObject *);
static void invalidate_auth (SoupAuthContext *ac, SoupAuth *auth);

static void
init (GObject *object)
{
	SoupAuthContext *ac = SOUP_AUTH_CONTEXT (object);

	ac->priv = g_new0 (SoupAuthContextPrivate, 1);
	ac->priv->message_auths = g_hash_table_new (NULL, NULL);
}

static void
free_auth (gpointer msg, gpointer auth, gpointer ac)
{
	g_object_weak_unref (msg, message_destroyed, ac);
	g_object_unref (auth);
}

static void
finalize (GObject *object)
{
	SoupAuthContext *ac = SOUP_AUTH_CONTEXT (object);

	g_hash_table_foreach (ac->priv->message_auths, free_auth, ac);
	g_free (ac->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	SoupAuthContextClass *auth_context_class =
		SOUP_AUTH_CONTEXT_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method definition */
	auth_context_class->invalidate_auth = invalidate_auth;

	/* virtual method override */
	object_class->finalize = finalize;

	/* signals */
	signals[AUTHENTICATE] =
		g_signal_new ("authenticate",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SoupAuthContextClass, authenticate),
			      NULL, NULL,
			      soup_marshal_BOOLEAN__OBJECT_OBJECT,
			      G_TYPE_BOOLEAN, 2,
			      SOUP_TYPE_AUTH,
			      SOUP_TYPE_MESSAGE);
	signals[REAUTHENTICATE] =
		g_signal_new ("reauthenticate",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SoupAuthContextClass, reauthenticate),
			      NULL, NULL,
			      soup_marshal_BOOLEAN__OBJECT_OBJECT,
			      G_TYPE_BOOLEAN, 2,
			      SOUP_TYPE_AUTH,
			      SOUP_TYPE_MESSAGE);
}

SOUP_MAKE_TYPE (soup_auth_context, SoupAuthContext, class_init, init, PARENT_TYPE)


gboolean
soup_auth_context_authenticate (SoupAuthContext *ac, SoupAuth *auth,
				SoupMessage *msg)
{
	gboolean succeeded = FALSE;

	g_signal_emit (ac, signals[AUTHENTICATE], 0, auth, msg, &succeeded);
	return succeeded;
}

gboolean
soup_auth_context_reauthenticate (SoupAuthContext *ac, SoupAuth *auth,
				  SoupMessage *msg)
{
	gboolean succeeded = FALSE;

	g_signal_emit (ac, signals[REAUTHENTICATE], 0, auth, msg, &succeeded);
	return succeeded;
}


void
soup_auth_context_add_auth (SoupAuthContext *ac,
			    const SoupUri *uri,
			    SoupAuth *auth)
{
	g_return_if_fail (SOUP_IS_AUTH_CONTEXT (ac));
	g_return_if_fail (uri != NULL);
	g_return_if_fail (SOUP_IS_AUTH (auth));

	SOUP_AUTH_CONTEXT_GET_CLASS (ac)->add_auth (ac, uri, auth);
}

static void
invalidate_auth (SoupAuthContext *ac, SoupAuth *auth)
{
	soup_auth_invalidate (auth);
}

void
soup_auth_context_invalidate_auth (SoupAuthContext *ac, SoupAuth *auth)
{
	g_return_if_fail (SOUP_IS_AUTH_CONTEXT (ac));
	g_return_if_fail (SOUP_IS_AUTH (auth));

	SOUP_AUTH_CONTEXT_GET_CLASS (ac)->invalidate_auth (ac, auth);
}

SoupAuth *
soup_auth_context_find_auth_for_realm (SoupAuthContext *ac,
				       const char *scheme, const char *realm)
{
	g_return_val_if_fail (SOUP_IS_AUTH_CONTEXT (ac), NULL);
	g_return_val_if_fail (scheme != NULL, NULL);
	g_return_val_if_fail (realm != NULL, NULL);

	return SOUP_AUTH_CONTEXT_GET_CLASS (ac)->find_auth_for_realm (ac, scheme, realm);
}

SoupAuth *
soup_auth_context_find_auth (SoupAuthContext *ac, SoupMessage *msg)
{
	g_return_val_if_fail (SOUP_IS_AUTH_CONTEXT (ac), NULL);
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), NULL);

	return SOUP_AUTH_CONTEXT_GET_CLASS (ac)->find_auth (ac, msg);
}

typedef struct {
	const char  *name;
	GType      (*type_func) (void);
	int          strength;
} SoupAuthContextKnownAuthScheme; 

static SoupAuthContextKnownAuthScheme known_auth_schemes [] = {
	{ "Basic",  soup_auth_basic_get_type,  0 },
	{ "Digest", soup_auth_digest_get_type, 1 },
	{ NULL }
};

static SoupAuth *
pick_auth_from_header_list (SoupAuthContext *ac, const GSList  *vals)
{
	SoupAuthContextKnownAuthScheme *scheme = NULL, *iter;
	char *header = NULL;
	SoupAuth *auth;
	GHashTable *tokens;
	const char *realm;

	while (vals) {
		char *tryheader = vals->data;

		for (iter = known_auth_schemes; iter->name; iter++) {
			if (!g_strncasecmp (tryheader, iter->name, 
					    strlen (iter->name))) {
				if (!scheme || 
				    scheme->strength < iter->strength) {
					header = tryheader;
					scheme = iter;
				}

				break;
			}
		}

		vals = vals->next;
	}

	if (!scheme)
		return NULL;

	tokens = soup_header_param_parse_list (header + strlen (scheme->name));
	realm = soup_header_param_get_token (tokens, "realm");

	auth = soup_auth_context_find_auth_for_realm (ac, scheme->name, realm);
	if (auth)
		return auth;

	auth = g_object_new (scheme->type_func (), NULL);
	soup_auth_parse (auth, tokens);

	if (tokens)
		soup_header_param_destroy_hash (tokens);

	return auth;
}

static void
message_destroyed (gpointer user_data, GObject *where_msg_was)
{
	SoupAuthContext *ac = user_data;
	SoupAuth *auth = g_hash_table_lookup (ac->priv->message_auths,
					      where_msg_was);

	g_hash_table_remove (ac->priv->message_auths, where_msg_was);
	if (auth)
		g_object_unref (auth);
}

/**
 * soup_auth_context_handle_unauthorized:
 * @ac: the auth context
 * @msg: the message that received a 401 or 407 error
 *
 * Checks if @ac would be able to authorize @msg based on the
 * returned WWW-Authenticate or Proxy-Authenticate headers.
 *
 * Return value: %TRUE if @msg should be requeued, %FALSE if @ac
 * can't authorize it.
 **/
gboolean
soup_auth_context_handle_unauthorized (SoupAuthContext *ac,
				       SoupMessage *msg)
{
	const char *authenticate_header;
	const GSList *vals;
	SoupAuth *auth, *prior_auth;
	gboolean invalid_prior_auth;

	/* See if there was already an auth on the message, and
	 * clear it if so. (We also check if the auth is INVALID
	 * before unreffing it, since if it is, unreffing it may
	 * destroy it.)
	 */
	prior_auth = g_hash_table_lookup (ac->priv->message_auths, msg);
	if (prior_auth) {
		invalid_prior_auth = (soup_auth_get_status (prior_auth) == SOUP_AUTH_STATUS_INVALID);
		g_hash_table_remove (ac->priv->message_auths, msg);
		g_object_weak_unref (G_OBJECT (msg), message_destroyed, ac);
		g_object_unref (prior_auth);
	} else
		invalid_prior_auth = FALSE;

	if (invalid_prior_auth) {
		/* This means that while we were waiting for this
		 * response to come in, another message using the same
		 * auth got a 401/407. If that prior 401/407 resulted
		 * in a new auth being created, then we will try this
		 * message again with that new auth. Otherwise, we
		 * don't bother trying to create a new auth, since if
		 * it failed earlier it will most likely fail now too.
		 */
		if (soup_auth_context_find_auth (ac, msg))
			return TRUE;
		else
			return FALSE;
	}

	/* Generate an auth from the Foo-Authenticate header */
	authenticate_header =
		SOUP_AUTH_CONTEXT_GET_CLASS (ac)->authenticate_header;
	vals = soup_message_get_header_list (msg->response_headers,
					     authenticate_header);
	auth = pick_auth_from_header_list (ac, vals);
	if (!auth) {
		soup_message_set_error_full (
			msg, msg->errorcode,
			(msg->errorcode == SOUP_ERROR_PROXY_UNAUTHORIZED ?
			 "Unknown authentication scheme required by proxy" :
			 "Unknown authentication scheme required"));
		/* FIXME: we should record the fact that there is an
		 * unknown protection domain at @uri.
		 */
		return FALSE;
	}

	if (auth == prior_auth) {
		/* The server is telling us to use the auth we already
		 * used. There are two possibilities here: either this
		 * was our first try with the auth and the password
		 * turned out to be wrong, or it had been good for a
		 * while, but then something weird happened like the
		 * user's password being changed on the server. Either
		 * way, the auth is invalid now.
		 */
		soup_auth_context_invalidate_auth (ac, auth);

		/* Create a new auth and see if we can authenticate it
		 * (using "reauthenticate_auth" so we don't just get
		 * the same cached password again). We can't just
		 * reauthenticate the old auth, because there may be
		 * other messages queued with it, and when their
		 * "unauthorized" responses arrive, we need to be able
		 * to tell that they were using the old auth rather
		 * than the new one.
		 */
		auth = pick_auth_from_header_list (ac, vals);
		if (soup_auth_context_reauthenticate (ac, auth, msg)) {
			soup_auth_context_add_auth (ac, soup_message_get_uri (msg), auth);
			return TRUE;
		} else {
			soup_auth_context_invalidate_auth (ac, auth);
			return FALSE;
		}
	}

	/* OK. The server has told us to authenticate, and we have
	 * found an acceptable auth to use (which is not the same auth
	 * we used before, if any). Note this.
	 */
	soup_auth_context_add_auth (ac, soup_message_get_uri (msg), auth);

	/* If the auth we're going to use is newly-created, we need to
	 * try to get a password for it first though, and fail the
	 * request if we can't. (We leave the auth lying around though
	 * since it may be authenticatable later.)
	 */
	if (soup_auth_get_status (auth) == SOUP_AUTH_STATUS_NEW) {
		if (!soup_auth_context_authenticate (ac, auth, msg))
			return FALSE;
	}

	/* We're good. Tell the caller to requeue the message. */
        return TRUE;
}

/**
 * soup_auth_context_authorize_message:
 * @ac: the auth context
 * @msg: the message to authorize
 *
 * If @ac has an authorization for @msg, add an appropriate header.
 * Otherwise, clear the header.
 **/
void
soup_auth_context_authorize_message (SoupAuthContext *ac, SoupMessage *msg)
{
	const char *authorization_header =
		SOUP_AUTH_CONTEXT_GET_CLASS (ac)->authorization_header;
	SoupAuth *auth;
	char *value;

	soup_message_remove_header (msg->request_headers,
				    authorization_header);

	auth = soup_auth_context_find_auth (ac, msg);
	if (!auth)
		return;

	if (soup_auth_get_status (auth) == SOUP_AUTH_STATUS_NEW) {
		if (!soup_auth_context_authenticate (ac, auth, msg))
			return;
	}

	value = soup_auth_get_authorization (auth, msg);
	g_return_if_fail (value != NULL);

	soup_message_add_header (msg->request_headers,
				 authorization_header, value);
	g_free (value);

	g_hash_table_insert (ac->priv->message_auths, msg, auth);
	g_object_weak_ref (G_OBJECT (msg), message_destroyed, ac);
	g_object_ref (auth);
}
