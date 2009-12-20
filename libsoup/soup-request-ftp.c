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
static void soup_request_ftp_initable_interface_init (GInitableIface *initable_interface);

G_DEFINE_TYPE_WITH_CODE (SoupRequestFtp, soup_request_ftp, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_REQUEST,
						soup_request_ftp_request_interface_init)
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
						soup_request_ftp_initable_interface_init))

struct _SoupRequestFtpPrivate {
	SoupURI *uri;
	SoupFTPConnection *conn;
};

enum {
	PROP_0,

	PROP_URI
};

static void soup_request_ftp_set_property (GObject *object, guint prop_id,
					    const GValue *value, GParamSpec *pspec);
static void soup_request_ftp_get_property (GObject *object, guint prop_id,
					    GValue *value, GParamSpec *pspec);
static void soup_request_ftp_finalize (GObject *object);

static gboolean soup_request_ftp_initable_init (GInitable     *initable,
						 GCancellable  *cancellable,
						 GError       **error);

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

	g_type_class_add_private (request_ftp_class, sizeof (SoupRequestFtpPrivate));

	object_class->finalize = soup_request_ftp_finalize;
	object_class->set_property = soup_request_ftp_set_property;
	object_class->get_property = soup_request_ftp_get_property;

	g_object_class_override_property (object_class, PROP_URI, "uri");
}

static void
soup_request_ftp_request_interface_init (SoupRequestInterface *request_interface)
{
	request_interface->send = soup_request_ftp_send;
	request_interface->send_async = soup_request_ftp_send_async;
	request_interface->send_finish = soup_request_ftp_send_finish;
}

static void
soup_request_ftp_initable_interface_init (GInitableIface *initable_interface)
{
	initable_interface->init = soup_request_ftp_initable_init;
}

static void
soup_request_ftp_init (SoupRequestFtp *ftp)
{
	ftp->priv = G_TYPE_INSTANCE_GET_PRIVATE (ftp, SOUP_TYPE_REQUEST_FTP, SoupRequestFtpPrivate);
}

static gboolean
soup_request_ftp_initable_init (GInitable     *initable,
				GCancellable  *cancellable,
				GError       **error)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (initable);
	const char *host;

	host = ftp->priv->uri->host;
	if (!host) {
		if (error) {
			char *uri_string = soup_uri_to_string (ftp->priv->uri, FALSE);
			g_set_error (error, SOUP_ERROR, SOUP_ERROR_BAD_URI,
				     _("Invalid 'ftp' URI: %s"), uri_string);
			g_free (uri_string);
		}
		return FALSE;
	}

	ftp->priv->conn = soup_ftp_connection_new ();

	return TRUE;
}

static void
soup_request_ftp_finalize (GObject *object)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (object);

	if (ftp->priv->uri)
		soup_uri_free (ftp->priv->uri);

	G_OBJECT_CLASS (soup_request_ftp_parent_class)->finalize (object);
}

static void
soup_request_ftp_set_property (GObject *object, guint prop_id,
				const GValue *value, GParamSpec *pspec)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (object);

	switch (prop_id) {
	case PROP_URI:
		if (ftp->priv->uri)
			soup_uri_free (ftp->priv->uri);
		ftp->priv->uri = g_value_dup_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
soup_request_ftp_get_property (GObject *object, guint prop_id,
				GValue *value, GParamSpec *pspec)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (object);

	switch (prop_id) {
	case PROP_URI:
		g_value_set_boxed (value, ftp->priv->uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* FIXME: cache SoupFTPConnection objects! */

static GInputStream *
soup_request_ftp_send (SoupRequest          *request,
		       GCancellable         *cancellable,
		       GError              **error)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (request);

	return soup_ftp_connection_load_uri (ftp->priv->conn,
					     ftp->priv->uri,
					     cancellable,
					     error);
}

static void
soup_request_ftp_send_async (SoupRequest          *request,
			     GCancellable         *cancellable,
			     GAsyncReadyCallback   callback,
			     gpointer              user_data)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (request);

	soup_ftp_connection_load_uri_async (ftp->priv->conn,
					    ftp->priv->uri,
					    cancellable,
					    callback,
					    user_data);
}

static GInputStream *
soup_request_ftp_send_finish (SoupRequest          *request,
			      GAsyncResult         *result,
			      GError              **error)
{
	SoupRequestFtp *ftp = SOUP_REQUEST_FTP (request);

	return soup_ftp_connection_load_uri_finish (ftp->priv->conn,
						    result,
						    error);
}
