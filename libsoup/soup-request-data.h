/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef SOUP_REQUEST_DATA_H
#define SOUP_REQUEST_DATA_H 1

#include "soup-request.h"

#define SOUP_TYPE_REQUEST_DATA            (soup_request_data_get_type ())
#define SOUP_REQUEST_DATA(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_REQUEST_DATA, SoupRequestData))
#define SOUP_REQUEST_DATA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_REQUEST_DATA, SoupRequestDataClass))
#define SOUP_IS_REQUEST_DATA(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_REQUEST_DATA))
#define SOUP_IS_REQUEST_DATA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_REQUEST_DATA))
#define SOUP_REQUEST_DATA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_REQUEST_DATA, SoupRequestDataClass))

typedef struct _SoupRequestDataPrivate SoupRequestDataPrivate;

typedef struct {
	SoupRequest parent;

	SoupRequestDataPrivate *priv;
} SoupRequestData;

typedef struct {
	SoupRequestClass parent;

} SoupRequestDataClass;

GType soup_request_data_get_type (void);

#endif /* SOUP_REQUEST_DATA_H */
