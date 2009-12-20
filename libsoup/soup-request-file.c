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

G_DEFINE_TYPE_WITH_CODE (SoupRequestFile, soup_request_file, SOUP_TYPE_REQUEST_BASE,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_REQUEST,
						soup_request_file_request_interface_init))

struct _SoupRequestFilePrivate {
	GFile *gfile;
};

static void soup_request_file_finalize (GObject *object);

static gboolean soup_request_file_validate_uri (SoupRequestBase  *req_base,
						SoupURI          *uri,
						GError          **error);

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
	SoupRequestBaseClass *request_base_class =
		SOUP_REQUEST_BASE_CLASS (request_file_class);

	g_type_class_add_private (request_file_class, sizeof (SoupRequestFilePrivate));

	object_class->finalize = soup_request_file_finalize;

	request_base_class->validate_uri = soup_request_file_validate_uri;
}

static void
soup_request_file_request_interface_init (SoupRequestInterface *request_interface)
{
	request_interface->send = soup_request_file_send;
	request_interface->send_async = soup_request_file_send_async;
	request_interface->send_finish = soup_request_file_send_finish;
}

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

	G_OBJECT_CLASS (soup_request_file_parent_class)->finalize (object);
}

static gboolean
soup_request_file_validate_uri (SoupRequestBase  *req_base,
				SoupURI          *uri,
				GError          **error)
{
	SoupRequestFile *file = SOUP_REQUEST_FILE (req_base);
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

	return (GInputStream *)g_file_read (file->priv->gfile, cancellable, error);
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
