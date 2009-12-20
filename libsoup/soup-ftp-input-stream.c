/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-ftp-input-stream.c: ftp GInputStream wrapper
 *
 * Copyright (C) 2009 FIXME
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-ftp-input-stream.h"

G_DEFINE_TYPE (SoupFTPInputStream, soup_ftp_input_stream, G_TYPE_FILTER_INPUT_STREAM);

enum {
	PROP_0,

	PROP_FILE_INFO,
	PROP_CHILDREN
};

enum {
	EOF,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct _SoupFTPInputStreamPrivate {
	GFileInfo *info;
	GList *children;
};

static gssize
soup_ftp_input_stream_read (GInputStream  *stream,
			    void          *buffer,
			    gsize          count,
			    GCancellable  *cancellable,
			    GError       **error)
{
	gssize nread = 0;

	nread = G_INPUT_STREAM_CLASS (soup_ftp_input_stream_parent_class)->
		read_fn (stream, buffer, count, cancellable, error);

	if (nread == 0)
		g_signal_emit (stream, signals[EOF], 0);

	return nread;
}

static gboolean
soup_ftp_input_stream_close (GInputStream  *stream,
			     GCancellable  *cancellable,
			     GError       **error)
{
	g_signal_emit (stream, signals[EOF], 0);

	return G_INPUT_STREAM_CLASS (soup_ftp_input_stream_parent_class)->
		close_fn (stream, cancellable, error);
}

static void
soup_ftp_input_stream_set_property (GObject         *object,
				    guint            prop_id,
				    const GValue    *value,
				    GParamSpec      *pspec)
{
	SoupFTPInputStream *sfstream = SOUP_FTP_INPUT_STREAM (object);

	switch (prop_id) {
		case PROP_FILE_INFO:
			sfstream->priv->info = g_value_dup_object (value);
			break;
		case PROP_CHILDREN:
			sfstream->priv->children = g_value_get_pointer (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
soup_ftp_input_stream_get_property (GObject    *object,
				    guint       prop_id,
				    GValue     *value,
				    GParamSpec *pspec)
{
	SoupFTPInputStream *sfstream = SOUP_FTP_INPUT_STREAM (object);

	switch (prop_id) {
		case PROP_FILE_INFO:
			g_value_set_object (value, sfstream->priv->info);
			break;
		case PROP_CHILDREN:
			g_value_set_pointer (value, sfstream->priv->children);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
soup_ftp_input_stream_finalize (GObject *object)
{
	SoupFTPInputStream *sfstream = SOUP_FTP_INPUT_STREAM (object);

	if (sfstream->priv->info)
		g_object_unref (sfstream->priv->info);

	G_OBJECT_CLASS (soup_ftp_input_stream_parent_class)->finalize (object);
}

static void
soup_ftp_input_stream_class_init (SoupFTPInputStreamClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GInputStreamClass *input_stream_class = G_INPUT_STREAM_CLASS (klass);

	g_type_class_add_private (klass, sizeof (SoupFTPInputStreamPrivate));

	gobject_class->set_property = soup_ftp_input_stream_set_property;
	gobject_class->get_property = soup_ftp_input_stream_get_property;
	gobject_class->finalize = soup_ftp_input_stream_finalize;

	input_stream_class->read_fn = soup_ftp_input_stream_read;
	input_stream_class->close_fn = soup_ftp_input_stream_close;

	signals[EOF] =
		g_signal_new ("eof",
			      SOUP_TYPE_FTP_INPUT_STREAM,
			      G_SIGNAL_RUN_FIRST,
			      0,
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_object_class_install_property (gobject_class,
					 PROP_FILE_INFO,
					 g_param_spec_object ("file-info",
							      "File Information",
							      "Stores information about a file referenced by a SoupFTPInputStream.",
							      G_TYPE_FILE_INFO,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class,
					 PROP_CHILDREN,
					 g_param_spec_pointer ("children",
							       "Directory's Children",
							       "Stores a list of GFileInfo",
							       G_PARAM_READWRITE |
							       G_PARAM_STATIC_STRINGS));
}

static void
soup_ftp_input_stream_init (SoupFTPInputStream *sfstream)
{
	sfstream->priv = G_TYPE_INSTANCE_GET_PRIVATE (sfstream, SOUP_TYPE_FTP_INPUT_STREAM, SoupFTPInputStreamPrivate);
}

GInputStream *
soup_ftp_input_stream_new (GInputStream *base_stream,
			   GFileInfo    *file_info,
			   GList        *children)
{
	return g_object_new (SOUP_TYPE_FTP_INPUT_STREAM,
			     "base-stream", base_stream,
			     "file-info", file_info,
			     "children", children,
			     NULL);
}
