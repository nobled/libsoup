/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-request-ftp.c: ftp: URI request object
 *
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include "soup-request-ftp.h"
#include "soup-ftp-connection.h"
#include "soup-uri.h"

G_DEFINE_TYPE (SoupRequestFTP, soup_request_ftp, SOUP_TYPE_REQUEST)

struct _SoupRequestFTPPrivate {
	SoupFTPConnection *conn;
	GFileInfo *info;
};

static void
soup_request_ftp_init (SoupRequestFTP *ftp)
{
	ftp->priv = G_TYPE_INSTANCE_GET_PRIVATE (ftp, SOUP_TYPE_REQUEST_FTP, SoupRequestFTPPrivate);
	ftp->priv->conn = soup_ftp_connection_new ();
}

static gboolean
soup_request_ftp_check_uri (SoupRequest  *request,
			    SoupURI      *uri,
			    GError      **error)
{
	return uri->host != NULL;
}

static void
soup_request_ftp_finalize (GObject *object)
{
	SoupRequestFTP *ftp = SOUP_REQUEST_FTP (object);

	if (ftp->priv->conn)
		g_object_unref (ftp->priv->conn);
	if (ftp->priv->info)
		g_object_unref (ftp->priv->info);

	G_OBJECT_CLASS (soup_request_ftp_parent_class)->finalize (object);
}

/* FIXME: cache SoupFTPConnection objects! */

static GInputStream *
soup_request_ftp_send (SoupRequest          *request,
		       GCancellable         *cancellable,
		       GError              **error)
{
	SoupRequestFTP *ftp = SOUP_REQUEST_FTP (request);
	GInputStream *stream;

	stream = soup_ftp_connection_load_uri (ftp->priv->conn,
					       soup_request_get_uri (request),
					       cancellable,
					       error);
	if (stream) {
		g_object_get (G_OBJECT (stream),
			      "file-info", &ftp->priv->info,
			      NULL);
	}
	return stream;
}

static void
sent_async (GObject *source, GAsyncResult *result, gpointer user_data)
{
	SoupFTPConnection *ftp = SOUP_FTP_CONNECTION (source);
	GSimpleAsyncResult *simple = user_data;
	GError *error = NULL;
	GInputStream *istream;

	istream = soup_ftp_connection_load_uri_finish (ftp, result, &error);
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
soup_request_ftp_send_async (SoupRequest          *request,
			     GCancellable         *cancellable,
			     GAsyncReadyCallback   callback,
			     gpointer              user_data)
{
	SoupRequestFTP *ftp = SOUP_REQUEST_FTP (request);
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (G_OBJECT (request),
					    callback, user_data,
					    soup_request_ftp_send_async);
	soup_ftp_connection_load_uri_async (ftp->priv->conn,
					    soup_request_get_uri (request),
					    cancellable,
					    sent_async, simple);
}

static GInputStream *
soup_request_ftp_send_finish (SoupRequest          *request,
			      GAsyncResult         *result,
			      GError              **error)
{
	SoupRequestFTP *ftp = SOUP_REQUEST_FTP (request);
	GSimpleAsyncResult *simple;
	GInputStream *stream;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (request), soup_request_ftp_send_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	stream = g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
	if (stream) {
		g_object_get (G_OBJECT (stream),
			      "file-info", &ftp->priv->info,
			      NULL);
	}
	return stream;
}

static goffset
soup_request_ftp_get_content_length (SoupRequest *request)
{
	SoupRequestFTP *ftp = SOUP_REQUEST_FTP (request);

	g_return_val_if_fail (ftp->priv->info != NULL, -1);

	return g_file_info_get_size (ftp->priv->info);
}

static const char *
soup_request_ftp_get_content_type (SoupRequest *request)
{
	SoupRequestFTP *ftp = SOUP_REQUEST_FTP (request);

	g_return_val_if_fail (ftp->priv->info != NULL, NULL);

	return g_file_info_get_content_type (ftp->priv->info);
}

static void
soup_request_ftp_class_init (SoupRequestFTPClass *request_ftp_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_ftp_class);
	SoupRequestClass *request_class =
		SOUP_REQUEST_CLASS (request_ftp_class);

	g_type_class_add_private (request_ftp_class, sizeof (SoupRequestFTPPrivate));

	object_class->finalize = soup_request_ftp_finalize;

	request_class->check_uri = soup_request_ftp_check_uri;
	request_class->send = soup_request_ftp_send;
	request_class->send_async = soup_request_ftp_send_async;
	request_class->send_finish = soup_request_ftp_send_finish;
	request_class->get_content_length = soup_request_ftp_get_content_length;
	request_class->get_content_type = soup_request_ftp_get_content_type;
}
