/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef  SOUP_FTP_INPUT_STREAM_H
#define  SOUP_FTP_INPUT_STREAM_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define SOUP_TYPE_FTP_INPUT_STREAM            (soup_ftp_input_stream_get_type ())
#define SOUP_FTP_INPUT_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_FTP_INPUT_STREAM, SoupFTPInputStream))
#define SOUP_FTP_INPUT_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_FTP_INPUT_STREAM, SoupFTPInputStreamClass))
#define SOUP_IS_FTP_INPUT_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_FTP_INPUT_STREAM))
#define SOUP_IS_FTP_INPUT_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_FTP_INPUT_STREAM))
#define SOUP_FTP_INPUT_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_FTP_INPUT_STREAM, SoupFTPInputStreamClass))

typedef struct _SoupFTPInputStream        SoupFTPInputStream;
typedef struct _SoupFTPInputStreamClass   SoupFTPInputStreamClass;
typedef struct _SoupFTPInputStreamPrivate SoupFTPInputStreamPrivate;

struct _SoupFTPInputStream {
	GFilterInputStream parent;

	SoupFTPInputStreamPrivate *priv;
};

struct _SoupFTPInputStreamClass {
	GFilterInputStreamClass parent;

};

GType         soup_ftp_input_stream_get_type (void);

GInputStream *soup_ftp_input_stream_new      (GInputStream *base_stream,
					      GFileInfo    *file_info,
					      GList        *children);

G_END_DECLS

#endif /*SOUP_FTP_INPUT_STREAM_H*/
