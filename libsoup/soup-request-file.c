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
#include "soup-session-feature.h"
#include "soup-session.h"
#include "soup-uri.h"

static void soup_request_file_request_interface_init (SoupRequestInterface *request_interface);
static void soup_request_file_initable_interface_init (GInitableIface *initable_interface);

G_DEFINE_TYPE_WITH_CODE (SoupRequestFile, soup_request_file, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_REQUEST,
						soup_request_file_request_interface_init)
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
						soup_request_file_initable_interface_init))

struct _SoupRequestFilePrivate {
	SoupURI *uri;
	GFile *gfile;
};

enum {
	PROP_0,

	PROP_URI
};

static void soup_request_file_set_property (GObject *object, guint prop_id,
					    const GValue *value, GParamSpec *pspec);
static void soup_request_file_get_property (GObject *object, guint prop_id,
					    GValue *value, GParamSpec *pspec);
static void soup_request_file_finalize (GObject *object);

static gboolean soup_request_file_initable_init (GInitable     *initable,
						 GCancellable  *cancellable,
						 GError       **error);

static GInputStream *soup_request_file_send        (SoupRequest          *request,
						    GCancellable         *cancellable,
						    GError              **error);
static void          soup_request_file_send_async  (SoupRequest          *request,
						    GCancellable         *cancellable,
						    GAsyncReadyCallback   callback,
						    gpointer              user_data);
static GInputStream *soup_request_file_send_finish (SoupRequest          *request,
						    GAsyncResult         *result,
						    GError              **error);

static void
soup_request_file_class_init (SoupRequestFileClass *request_file_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_file_class);

	g_type_class_add_private (request_file_class, sizeof (SoupRequestFilePrivate));

	object_class->finalize = soup_request_file_finalize;
	object_class->set_property = soup_request_file_set_property;
	object_class->get_property = soup_request_file_get_property;

	g_object_class_override_property (object_class, PROP_URI, "uri");
}

static void
soup_request_file_request_interface_init (SoupRequestInterface *request_interface)
{
	request_interface->send = soup_request_file_send;
	request_interface->send_async = soup_request_file_send_async;
	request_interface->send_finish = soup_request_file_send_finish;
}

static void
soup_request_file_initable_interface_init (GInitableIface *initable_interface)
{
	initable_interface->init = soup_request_file_initable_init;
}

static void
soup_request_file_init (SoupRequestFile *file)
{
	file->priv = G_TYPE_INSTANCE_GET_PRIVATE (file, SOUP_TYPE_REQUEST_FILE, SoupRequestFilePrivate);
}

static gboolean
soup_request_file_initable_init (GInitable     *initable,
				 GCancellable  *cancellable,
				 GError       **error)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (initable);
	const char *host;
	char *path_decoded;

	host = file->priv->uri->host;
	if (!host || (*host && strcmp (host, "localhost") != 0)) {
		if (error) {
			char *uri_string = soup_uri_to_string (file->priv->uri, FALSE);
			g_set_error (error, SOUP_ERROR, SOUP_ERROR_BAD_URI,
				     _("Invalid 'file' URI: %s"), uri_string);
			g_free (uri_string);
		}
		return FALSE;
	}

	path_decoded = soup_uri_decode (file->priv->uri->path);
	file->priv->gfile = g_file_new_for_path (path_decoded);
	g_free (path_decoded);

	return TRUE;
}

static void
soup_request_file_finalize (GObject *object)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (object);

	if (file->priv->uri)
		soup_uri_free (file->priv->uri);
	if (file->priv->gfile)
		g_object_unref (file->priv->gfile);

	G_OBJECT_CLASS (soup_request_file_parent_class)->finalize (object);
}

static void
soup_request_file_set_property (GObject *object, guint prop_id,
				const GValue *value, GParamSpec *pspec)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (object);

	switch (prop_id) {
	case PROP_URI:
		if (file->priv->uri)
			soup_uri_free (file->priv->uri);
		file->priv->uri = g_value_dup_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
soup_request_file_get_property (GObject *object, guint prop_id,
				GValue *value, GParamSpec *pspec)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (object);

	switch (prop_id) {
	case PROP_URI:
		g_value_set_boxed (value, file->priv->uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GInputStream *
soup_request_file_send (SoupRequest          *request,
			GCancellable         *cancellable,
			GError              **error)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	return (GInputStream *)g_file_read (file->priv->gfile,
					    cancellable, error);
}

static void
soup_request_file_send_async (SoupRequest          *request,
			      GCancellable         *cancellable,
			      GAsyncReadyCallback   callback,
			      gpointer              user_data)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	g_file_read_async (file->priv->gfile, G_PRIORITY_DEFAULT,
			   cancellable, callback, user_data);
}

static GInputStream *
soup_request_file_send_finish (SoupRequest          *request,
			       GAsyncResult         *result,
			       GError              **error)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (request);

	return (GInputStream *)g_file_read_finish (file->priv->gfile,
						   result, error);
}
