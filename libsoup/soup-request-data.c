/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-request-data.c: data: URI request object
 *
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include "soup-request-data.h"
#include "soup-session-feature.h"
#include "soup-session.h"
#include "soup-uri.h"

static void soup_request_data_request_interface_init (SoupRequestInterface *request_interface);
static void soup_request_data_initable_interface_init (GInitableIface *initable_interface);

G_DEFINE_TYPE_WITH_CODE (SoupRequestData, soup_request_data, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_REQUEST,
						soup_request_data_request_interface_init)
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
						soup_request_data_initable_interface_init))

struct _SoupRequestDataPrivate {
	SoupURI *uri;

};

enum {
	PROP_0,

	PROP_URI
};

static void soup_request_data_set_property (GObject *object, guint prop_id,
					    const GValue *value, GParamSpec *pspec);
static void soup_request_data_get_property (GObject *object, guint prop_id,
					    GValue *value, GParamSpec *pspec);
static void soup_request_data_finalize (GObject *object);

static gboolean soup_request_data_initable_init (GInitable     *initable,
						 GCancellable  *cancellable,
						 GError       **error);

static GInputStream *soup_request_data_send        (SoupRequest          *request,
						    GCancellable         *cancellable,
						    GError              **error);

static void
soup_request_data_class_init (SoupRequestDataClass *request_data_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_data_class);

	g_type_class_add_private (request_data_class, sizeof (SoupRequestDataPrivate));

	object_class->finalize = soup_request_data_finalize;
	object_class->set_property = soup_request_data_set_property;
	object_class->get_property = soup_request_data_get_property;

	g_object_class_override_property (object_class, PROP_URI, "uri");
}

static void
soup_request_data_request_interface_init (SoupRequestInterface *request_interface)
{
	request_interface->send = soup_request_data_send;
}

static void
soup_request_data_initable_interface_init (GInitableIface *initable_interface)
{
	initable_interface->init = soup_request_data_initable_init;
}

static void
soup_request_data_init (SoupRequestData *data)
{
	data->priv = G_TYPE_INSTANCE_GET_PRIVATE (data, SOUP_TYPE_REQUEST_DATA, SoupRequestDataPrivate);
}

static gboolean
soup_request_data_initable_init (GInitable     *initable,
				 GCancellable  *cancellable,
				 GError       **error)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (initable);

	if (data->priv->uri->host) {
		if (error) {
			char *uri_string = soup_uri_to_string (data->priv->uri, FALSE);
			g_set_error (error, SOUP_ERROR, SOUP_ERROR_BAD_URI,
				     _("Invalid 'data' URI: %s"), uri_string);
			g_free (uri_string);
		}
		return FALSE;
	}

	return TRUE;
}

static void
soup_request_data_finalize (GObject *object)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (object);

	if (data->priv->uri)
		soup_uri_free (data->priv->uri);

	G_OBJECT_CLASS (soup_request_data_parent_class)->finalize (object);
}

static void
soup_request_data_set_property (GObject *object, guint prop_id,
				const GValue *value, GParamSpec *pspec)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (object);

	switch (prop_id) {
	case PROP_URI:
		if (data->priv->uri)
			soup_uri_free (data->priv->uri);
		data->priv->uri = g_value_dup_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
soup_request_data_get_property (GObject *object, guint prop_id,
				GValue *value, GParamSpec *pspec)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (object);

	switch (prop_id) {
	case PROP_URI:
		g_value_set_boxed (value, data->priv->uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GInputStream *
data_uri_decode (const char  *uri_data,
		 char       **mime_type)
{
	GInputStream *memstream;
	const char *comma, *semi, *start, *end;
	gboolean base64 = FALSE;

	if (mime_type)
		*mime_type = NULL;

	comma = strchr (uri_data, ',');
	if (comma && comma != uri_data) {
		/* Deal with MIME type / params */
		semi = memchr (uri_data, ';', comma - uri_data);
		end = semi ? semi : comma;

		if (mime_type && (end != uri_data)) {
			char *encoded = g_strndup (uri_data, end - uri_data);
			*mime_type = soup_uri_decode (encoded);
			g_free (encoded);
		}

		if (semi && !g_ascii_strncasecmp (semi, ";base64", MAX (comma - semi, strlen (";base64"))))
			base64 = TRUE;
	}

	memstream = g_memory_input_stream_new ();

	start = comma ? comma + 1 : uri_data;
	if (*start) {
		guchar *buf;
		gsize len;

		if (base64) {
			int inlen, state = 0;
			guint save = 0;
			char *unescaped;

			if (strchr (start, '%')) {
				start = unescaped = soup_uri_decode (start);
				if (!unescaped)
					goto fail;
			} else
				unescaped = NULL;

			inlen = strlen (start);
			buf = g_malloc (inlen * 3 / 4);  
			len = g_base64_decode_step (start, inlen, buf,
						    &state, &save);
			g_free (unescaped);
			if (state != 0) {
				g_free (buf);
				goto fail;
			}
		} else {
			buf = (guchar *)g_uri_unescape_string (start, NULL);
			if (!buf)
				goto fail;
			len = strlen ((char *)buf);
		}

		g_memory_input_stream_add_data (G_MEMORY_INPUT_STREAM (memstream),
						buf, len, g_free);
	}

	return memstream;

fail:
	g_object_unref (memstream);
	if (mime_type && *mime_type) {
		g_free (*mime_type);
		*mime_type = NULL;
	}
	return NULL;
}

static GInputStream *
soup_request_data_send (SoupRequest          *request,
			GCancellable         *cancellable,
			GError              **error)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (request);

	return data_uri_decode (data->priv->uri->path, NULL);
}
