/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-server-auth.h: Server-side authentication handling
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2001-2002, Ximian, Inc.
 */

#ifndef SOUP_SERVER_AUTH_H
#define SOUP_SERVER_AUTH_H 1

#include <glib.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-misc.h>

typedef union _SoupServerAuth SoupServerAuth;
typedef struct _SoupServerAuthContext SoupServerAuthContext;

typedef gboolean (*SoupServerAuthCallbackFn) (SoupServerAuthContext *auth_ctx,
					      SoupServerAuth        *auth,
					      SoupMessage           *msg, 
					      gpointer               data);

struct _SoupServerAuthContext {
	guint                     types;
	SoupServerAuthCallbackFn  callback;
	gpointer                  user_data;

	struct {
		const gchar *realm;
	} basic_info;

	struct {
		const gchar *realm;
		guint        allow_algorithms;
		gboolean     force_integrity;
	} digest_info;
};

void soup_server_auth_context_challenge (SoupServerAuthContext *auth_ctx,
					 SoupMessage           *msg,
					 gchar                 *header_name);


typedef struct {
	SoupAuthType  type;
	const gchar  *user;
	const gchar  *passwd;
} SoupServerAuthBasic;

typedef enum {
	SOUP_ALGORITHM_MD5      = 1 << 0,
	SOUP_ALGORITHM_MD5_SESS = 1 << 1
} SoupDigestAlgorithm;

typedef struct {
	SoupAuthType          type;
	SoupDigestAlgorithm   algorithm;
	gboolean              integrity;
	const gchar          *realm;
	const gchar          *user;
	const gchar          *nonce;
	gint                  nonce_count;
	const gchar          *cnonce;
	const gchar          *digest_uri;
	const gchar          *digest_response;
	const gchar          *request_method;
} SoupServerAuthDigest;

union _SoupServerAuth {
	SoupAuthType          type;
	SoupServerAuthBasic   basic;
	SoupServerAuthDigest  digest;
};

SoupServerAuth *soup_server_auth_new          (SoupServerAuthContext *auth_ctx, 
				               const GSList          *auth_hdrs,
					       SoupMessage           *msg);

void            soup_server_auth_free         (SoupServerAuth        *auth);

const gchar    *soup_server_auth_get_user     (SoupServerAuth        *auth);

gboolean        soup_server_auth_check_passwd (SoupServerAuth        *auth,
					       gchar                 *passwd);

#endif /* SOUP_SERVER_AUTH_H */
