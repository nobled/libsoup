/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef SOUP_REQUEST_BASE_H
#define SOUP_REQUEST_BASE_H 1

#include <libsoup/soup-request.h>

#define SOUP_TYPE_REQUEST_BASE            (soup_request_base_get_type ())
#define SOUP_REQUEST_BASE(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_REQUEST_BASE, SoupRequestBase))
#define SOUP_REQUEST_BASE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_REQUEST_BASE, SoupRequestBaseClass))
#define SOUP_IS_REQUEST_BASE(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_REQUEST_BASE))
#define SOUP_IS_REQUEST_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_REQUEST_BASE))
#define SOUP_REQUEST_BASE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_REQUEST_BASE, SoupRequestBaseClass))

typedef struct _SoupRequestBasePrivate SoupRequestBasePrivate;

typedef struct {
	GObject parent;

	SoupRequestBasePrivate *priv;
} SoupRequestBase;

typedef struct {
	GObjectClass parent;

	gboolean (*validate_uri) (SoupRequestBase  *req_base,
				  SoupURI          *uri,
				  GError          **error);
} SoupRequestBaseClass;

GType soup_request_base_get_type (void);

SoupURI     *soup_request_base_get_uri     (SoupRequestBase *req_base);
SoupSession *soup_request_base_get_session (SoupRequestBase *req_base);

#endif /* SOUP_REQUEST_BASE_H */
