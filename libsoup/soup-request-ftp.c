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
#include "soup-session-feature.h"
#include "soup-session.h"
#include "soup-uri.h"

static void soup_request_ftp_request_interface_init (SoupRequestInterface *request_interface);

G_DEFINE_TYPE_WITH_CODE (SoupRequestFtp, soup_request_ftp, SOUP_TYPE_REQUEST_BASE,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_REQUEST,
						soup_request_ftp_request_interface_init))

struct _SoupRequestFtpPrivate {
	SoupFTPConnection *conn;
};

static void soup_request_ftp_finalize (GObject *object);

static gboolean soup_request_ftp_validate_uri (SoupRequestBase  *req_base,
					       SoupURI          *uri,
					       GError          **error);

static GInputStream *soup_request_ftp_send        (SoupRequest          *request,
						   GCancellable         *cancellable,
						   GError              **error);
static void          soup_request_ftp_send_async  (SoupRequest          *request,
						   GCancellable         *cancellable,
						   GAsyncReadyCallback   callback,
						   gpointer              user_data);
static GInputStream *soup_request_ftp_send_finish (SoupRequest          *request,
						   GAsyncResult         *result,
						   GError              **error);

static void
soup_request_ftp_class_init (SoupRequestFtpClass *request_ftp_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_ftp_class);
	SoupRequestBaseClass *request_base_class =
		SOUP_REQUEST_BASE_CLASS (request_ftp_class);

	g_type_class_add_private (request_ftp_class, sizeof (SoupRequestFtpPrivate));

	object_class->finalize = soup_request_ftp_finalize;

	request_base_class->validate_uri = soup_request_ftp_validate_uri;
}

static void
soup_request_ftp_request_interface_init (SoupRequestInterface *request_interface)
{
	request_interface->send = soup_request_ftp_send;
	request_interface->send_async = soup_request_ftp_send_async;
	request_interface->send_finish = soup_request_ftp_send_finish;
}

static void
soup_request_ftp_init (SoupRequestFtp *ftp)
{
	ftp->priv = G_TYPE_INSTANCE_GET_PRIVATE (ftp, SOUP_TYPE_REQUEST_FTP, SoupRequestFtpPrivate);
	ftp->priv->conn = soup_ftp_connection_new ();
}

static gboolean
soup_request_ftp_validate_uri (SoupRequestBase  *req_base,
			       SoupURI          *uri,
			       GError          **error)
{
	return uri->host != NULL;
}

static void
soup_request_ftp_finalize (GObject *object)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (object);

	if (ftp->priv->conn)
		g_object_unref (ftp->priv->conn);

	G_OBJECT_CLASS (soup_request_ftp_parent_class)->finalize (object);
}

/* FIXME: cache SoupFTPConnection objects! */

static GInputStream *
soup_request_ftp_send (SoupRequest          *request,
		       GCancellable         *cancellable,
		       GError              **error)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (request);

	return soup_ftp_connection_load_uri (ftp->priv->conn,
					     soup_request_base_get_uri (SOUP_REQUEST_BASE (request)),
					     cancellable,
					     error);
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
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (request);
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (G_OBJECT (request),
					    callback, user_data,
					    soup_request_ftp_send_async);
	soup_ftp_connection_load_uri_async (ftp->priv->conn,
					    soup_request_base_get_uri (SOUP_REQUEST_BASE (request)),
					    cancellable,
					    sent_async, simple);
}

static GInputStream *
soup_request_ftp_send_finish (SoupRequest          *request,
			      GAsyncResult         *result,
			      GError              **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (request), soup_request_ftp_send_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;
	return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}
