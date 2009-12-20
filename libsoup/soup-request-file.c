/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-request-file.c: file: URI request object
 *
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include "soup-request-file.h"
#include "soup-uri.h"

G_DEFINE_TYPE (SoupRequestFile, soup_request_file, SOUP_TYPE_REQUEST)

struct _SoupRequestFilePrivate {
	GFile *gfile;
	GFileInfo *info;

	GSimpleAsyncResult *simple;
	GCancellable *cancellable;
};

static void
soup_request_file_init (SoupRequestFile *file)
{
	file->priv = G_TYPE_INSTANCE_GET_PRIVATE (file, SOUP_TYPE_REQUEST_FILE, SoupRequestFilePrivate);
}

static void
soup_request_file_finalize (GObject *object)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (object);

	if (file->priv->gfile)
		g_object_unref (file->priv->gfile);
	if (file->priv->info)
		g_object_unref (file->priv->info);

	G_OBJECT_CLASS (soup_request_file_parent_class)->finalize (object);
}

static gboolean
soup_request_file_check_uri (SoupRequest  *request,
			     SoupURI      *uri,
			     GError      **error)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);
	char *path_decoded;

	/* "file:/foo" is not valid */
	if (!uri->host)
		return FALSE;

	/* but it must be "file:///..." or "file://localhost/..." */
	if (*uri->host && g_ascii_strcasecmp (uri->host, "localhost") != 0)
		return FALSE;

	path_decoded = soup_uri_decode (uri->path);
	file->priv->gfile = g_file_new_for_path (path_decoded);
	g_free (path_decoded);

	return TRUE;
}

static GInputStream *
soup_request_file_send (SoupRequest          *request,
			GCancellable         *cancellable,
			GError              **error)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	file->priv->info = g_file_query_info (file->priv->gfile,
					      G_FILE_ATTRIBUTE_STANDARD_TYPE ","
					      G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
					      G_FILE_ATTRIBUTE_STANDARD_SIZE,
					      0, cancellable, error);
	if (!file->priv->info)
		return NULL;

	return (GInputStream *)g_file_read (file->priv->gfile, cancellable, error);
}

static void
sent_async (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GFile *gfile = G_FILE (source);
	SoupRequestFile *file = user_data;
	GSimpleAsyncResult *simple;
	GError *error = NULL;
	GFileInputStream *istream;

	simple = file->priv->simple;
	file->priv->simple = NULL;
	if (file->priv->cancellable) {
		g_object_unref (file->priv->cancellable);
		file->priv->cancellable = NULL;
	}

	istream = g_file_read_finish (gfile, result, &error);
	if (istream)
		g_simple_async_result_set_op_res_gpointer (simple, istream, g_object_unref);
	else {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}

static void
queried_info_async (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GFile *gfile = G_FILE (source);
	SoupRequestFile *file = user_data;
	GError *error = NULL;

	file->priv->info = g_file_query_info_finish (gfile, result, &error);
	if (!file->priv->info) {
		GSimpleAsyncResult *simple;

		simple = file->priv->simple;
		file->priv->simple = NULL;
		if (file->priv->cancellable) {
			g_object_unref (file->priv->cancellable);
			file->priv->cancellable = NULL;
		}

		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		return;
	}
	
	g_file_read_async (gfile, G_PRIORITY_DEFAULT,
			   file->priv->cancellable, sent_async, file);
}

static void
soup_request_file_send_async (SoupRequest          *request,
			      GCancellable         *cancellable,
			      GAsyncReadyCallback   callback,
			      gpointer              user_data)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	file->priv->simple = g_simple_async_result_new (G_OBJECT (request),
							callback, user_data,
							soup_request_file_send_async);
	file->priv->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	g_file_query_info_async (file->priv->gfile,
				 G_FILE_ATTRIBUTE_STANDARD_TYPE ","
				 G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
				 G_FILE_ATTRIBUTE_STANDARD_SIZE,
				 0, G_PRIORITY_DEFAULT, cancellable,
				 queried_info_async, file);
}

static GInputStream *
soup_request_file_send_finish (SoupRequest          *request,
			       GAsyncResult         *result,
			       GError              **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (request), soup_request_file_send_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;
	return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}

static goffset
soup_request_file_get_content_length (SoupRequest *request)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	g_return_val_if_fail (file->priv->info != NULL, -1);

	return g_file_info_get_size (file->priv->info);
}

static const char *
soup_request_file_get_content_type (SoupRequest *request)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	g_return_val_if_fail (file->priv->info != NULL, NULL);

	return g_file_info_get_content_type (file->priv->info);
}

static void
soup_request_file_class_init (SoupRequestFileClass *request_file_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_file_class);
	SoupRequestClass *request_class =
		SOUP_REQUEST_CLASS (request_file_class);

	g_type_class_add_private (request_file_class, sizeof (SoupRequestFilePrivate));

	object_class->finalize = soup_request_file_finalize;

	request_class->check_uri = soup_request_file_check_uri;
	request_class->send = soup_request_file_send;
	request_class->send_async = soup_request_file_send_async;
	request_class->send_finish = soup_request_file_send_finish;
	request_class->get_content_length = soup_request_file_get_content_length;
	request_class->get_content_type = soup_request_file_get_content_type;
}
