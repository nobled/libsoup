/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifndef SOUP_AUTH_H
#define SOUP_AUTH_H 1

#include <glib-object.h>
#include <libsoup/soup-types.h>
#include <libsoup/soup-uri.h>

#define SOUP_TYPE_AUTH            (soup_auth_get_type ())
#define SOUP_AUTH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_AUTH, SoupAuth))
#define SOUP_AUTH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_AUTH, SoupAuthClass))
#define SOUP_IS_AUTH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_AUTH))
#define SOUP_IS_AUTH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_AUTH))
#define SOUP_AUTH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_AUTH, SoupAuthClass))

typedef enum {
	/* The auth is freshly-created. */
	SOUP_AUTH_STATUS_NEW,

	/* The auth has been given authentication information. */
	SOUP_AUTH_STATUS_AUTHENTICATED,

	/* The auth has been given authentication information
	 * which has turned out to be bad.
	 */
	SOUP_AUTH_STATUS_INVALID
} SoupAuthStatus;

struct _SoupAuth {
	GObject parent;

	SoupAuthPrivate *priv;
};

struct _SoupAuthClass {
	GObjectClass parent_class;

	const char *scheme_name;

	void     (*parse)                (SoupAuth      *auth,
					  GHashTable    *tokens);
	GSList * (*get_protection_space) (SoupAuth      *auth,
					  const SoupUri *source_uri);
	void     (*authenticate)         (SoupAuth      *auth,
					  const char    *username,
					  const char    *password);
	char *   (*get_authorization)    (SoupAuth      *auth,
					  SoupMessage   *msg);
};

GType           soup_auth_get_type              (void);

void            soup_auth_parse                 (SoupAuth       *auth,
						 GHashTable     *tokens);

const char     *soup_auth_get_scheme_name       (SoupAuth       *auth);

const char     *soup_auth_get_realm             (SoupAuth       *auth);

GSList         *soup_auth_get_protection_space  (SoupAuth      *auth,
						 const SoupUri *source_uri);
void            soup_auth_free_protection_space (SoupAuth      *auth,
						 GSList        *domain);

void            soup_auth_authenticate          (SoupAuth       *auth,
						 const char     *username,
						 const char     *password);

void            soup_auth_invalidate            (SoupAuth       *auth);

char           *soup_auth_get_authorization     (SoupAuth       *auth,
						 SoupMessage    *msg);

SoupAuthStatus  soup_auth_get_status            (SoupAuth       *auth);

#endif /* SOUP_AUTH_H */
