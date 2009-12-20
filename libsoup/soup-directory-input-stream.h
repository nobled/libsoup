/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
 */

#ifndef SOUP_DIRECTORY_INPUT_STREAM_H
#define SOUP_DIRECTORY_INPUT_STREAM_H 1

#include <gio/gio.h>
#include <libsoup/soup-types.h>
#include <libsoup/soup-message-body.h>

G_BEGIN_DECLS

#define SOUP_TYPE_DIRECTORY_INPUT_STREAM            (soup_directory_input_stream_get_type ())
#define SOUP_DIRECTORY_INPUT_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_DIRECTORY_INPUT_STREAM, SoupDirectoryInputStream))
#define SOUP_DIRECTORY_INPUT_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_DIRECTORY_INPUT_STREAM, SoupDirectoryInputStreamClass))
#define SOUP_IS_DIRECTORY_INPUT_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_DIRECTORY_INPUT_STREAM))
#define SOUP_IS_DIRECTORY_INPUT_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_DIRECTORY_INPUT_STREAM))
#define SOUP_DIRECTORY_INPUT_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_DIRECTORY_INPUT_STREAM, SoupDirectoryInputStreamClass))

typedef struct _SoupDirectoryInputStream SoupDirectoryInputStream;
typedef struct _SoupDirectoryInputStreamClass SoupDirectoryInputStreamClass;

struct _SoupDirectoryInputStream {
	GInputStream parent;

        GFileEnumerator *enumerator;
        char *uri;
        SoupBuffer *buffer;
        gboolean done;
};

struct _SoupDirectoryInputStreamClass {
	GInputStreamClass parent_class;
};

GType          soup_directory_input_stream_get_type      (void);

GInputStream * soup_directory_input_stream_new           (GFileEnumerator *enumerator,
                                                          SoupURI         *uri);


G_END_DECLS

#endif /* SOUP_DIRECTORY_INPUT_STREAM_H */
