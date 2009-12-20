/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef SOUP_REQUEST_HTTP_H
#define SOUP_REQUEST_HTTP_H 1

#include "soup-request.h"

#define SOUP_TYPE_REQUEST_HTTP            (soup_request_http_get_type ())
#define SOUP_REQUEST_HTTP(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_REQUEST_HTTP, SoupRequestHTTP))
#define SOUP_REQUEST_HTTP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_REQUEST_HTTP, SoupRequestHTTPClass))
#define SOUP_IS_REQUEST_HTTP(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_REQUEST_HTTP))
#define SOUP_IS_REQUEST_HTTP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_REQUEST_HTTP))
#define SOUP_REQUEST_HTTP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_REQUEST_HTTP, SoupRequestHTTPClass))

typedef struct _SoupRequestHTTPPrivate SoupRequestHTTPPrivate;

typedef struct {
	SoupRequest parent;

	SoupRequestHTTPPrivate *priv;
} SoupRequestHTTP;

typedef struct {
	SoupRequestClass parent;

} SoupRequestHTTPClass;

GType soup_request_http_get_type (void);

#endif /* SOUP_REQUEST_HTTP_H */
