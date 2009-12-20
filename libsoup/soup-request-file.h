/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef SOUP_REQUEST_FILE_H
#define SOUP_REQUEST_FILE_H 1

#include "soup-request.h"

#define SOUP_TYPE_REQUEST_FILE            (soup_request_file_get_type ())
#define SOUP_REQUEST_FILE(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_REQUEST_FILE, SoupRequestFile))
#define SOUP_REQUEST_FILE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_REQUEST_FILE, SoupRequestFileInterface))
#define SOUP_IS_REQUEST_FILE(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_REQUEST_FILE))
#define SOUP_IS_REQUEST_FILE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_REQUEST_FILE))
#define SOUP_REQUEST_FILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SOUP_TYPE_REQUEST_FILE, SoupRequestFileInterface))

typedef struct _SoupRequestFilePrivate SoupRequestFilePrivate;

typedef struct {
	GObject parent;

	SoupRequestFilePrivate *priv;
} SoupRequestFile;

typedef struct {
	GObjectClass parent;

} SoupRequestFileClass;

GType soup_request_file_get_type (void);

#endif /* SOUP_REQUEST_FILE_H */
