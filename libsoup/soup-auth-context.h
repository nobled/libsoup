/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_AUTH_CONTEXT_H
#define SOUP_AUTH_CONTEXT_H 1

#include <glib-object.h>
#include <libsoup/soup-error.h>
#include <libsoup/soup-types.h>
#include <libsoup/soup-uri.h>

#define SOUP_TYPE_AUTH_CONTEXT            (soup_auth_context_get_type ())
#define SOUP_AUTH_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_AUTH_CONTEXT, SoupAuthContext))
#define SOUP_AUTH_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_AUTH_CONTEXT, SoupAuthContextClass))
#define SOUP_IS_AUTH_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_AUTH_CONTEXT))
#define SOUP_IS_AUTH_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_AUTH_CONTEXT))
#define SOUP_AUTH_CONTEXT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_AUTH_CONTEXT, SoupAuthContextClass))

struct _SoupAuthContext {
	GObject parent;

	SoupAuthContextPrivate *priv;
};

struct _SoupAuthContextClass {
	GObjectClass parent_class;

	const char *authenticate_header;
	const char *authorization_header;

	/* signals */
	gboolean (*authenticate)   (SoupAuthContext *, SoupAuth *,
				    SoupMessage *);
				    
	gboolean (*reauthenticate) (SoupAuthContext *, SoupAuth *,
				    SoupMessage *);

	/* methods */
        void           (*add_auth)                 (SoupAuthContext *ac,
						    const SoupUri   *uri,
						    SoupAuth        *auth);
	void           (*invalidate_auth)          (SoupAuthContext *ac,
						    SoupAuth        *auth);
	SoupAuth *     (*find_auth_for_realm)      (SoupAuthContext *ac,
						    const char      *scheme,
						    const char      *realm);
	SoupAuth *     (*find_auth)                (SoupAuthContext *ac,
						    SoupMessage     *msg);

};

GType            soup_auth_context_get_type            (void);

gboolean         soup_auth_context_handle_unauthorized (SoupAuthContext *ac,
							SoupMessage     *msg);

void             soup_auth_context_authorize_message   (SoupAuthContext *ac,
							SoupMessage     *msg);

/* The rest of this is mostly for authcontext-internal use */
gboolean         soup_auth_context_authenticate        (SoupAuthContext *ac,
							SoupAuth        *auth,
							SoupMessage     *msg);
gboolean         soup_auth_context_reauthenticate      (SoupAuthContext *ac,
							SoupAuth        *auth,
							SoupMessage     *msg);

void             soup_auth_context_add_auth            (SoupAuthContext *ac,
							const SoupUri   *uri,
							SoupAuth        *auth);
void             soup_auth_context_invalidate_auth     (SoupAuthContext *ac,
							SoupAuth        *auth);
SoupAuth *       soup_auth_context_find_auth_for_realm (SoupAuthContext *ac,
							const char      *schm,
							const char      *rlm);
SoupAuth *       soup_auth_context_find_auth           (SoupAuthContext *ac,
							SoupMessage     *msg);

#endif /* SOUP_AUTH_CONTEXT_H */
