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
#include "soup-uri.h"

G_DEFINE_TYPE (SoupRequestData, soup_request_data, SOUP_TYPE_REQUEST)

struct _SoupRequestDataPrivate {
	gsize content_length;
	char *content_type;
};

static void
soup_request_data_init (SoupRequestData *data)
{
	data->priv = G_TYPE_INSTANCE_GET_PRIVATE (data, SOUP_TYPE_REQUEST_DATA, SoupRequestDataPrivate);
}

static void
soup_request_data_finalize (GObject *object)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (object);

	g_free (data->priv->content_type);

	G_OBJECT_CLASS (soup_request_data_parent_class)->finalize (object);
}

static gboolean
soup_request_data_check_uri (SoupRequest  *request,
			     SoupURI      *uri,
			     GError      **error)
{
	return uri->host == NULL;
}

static GInputStream *
soup_request_data_send (SoupRequest   *request,
			GCancellable  *cancellable,
			GError       **error)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (request);
	SoupURI *uri = soup_request_get_uri (request);
	GInputStream *memstream;
	const char *comma, *semi, *start, *end;
	gboolean base64 = FALSE;

	comma = strchr (uri->path, ',');
	if (comma && comma != uri->path) {
		/* Deal with MIME type / params */
		semi = memchr (uri->path, ';', comma - uri->path);
		end = semi ? semi : comma;

		if (end != uri->path) {
			char *encoded = g_strndup (uri->path, end - uri->path);
			data->priv->content_type = soup_uri_decode (encoded);
			g_free (encoded);
		}

		if (semi && !g_ascii_strncasecmp (semi, ";base64", MAX (comma - semi, strlen (";base64"))))
			base64 = TRUE;
	}

	memstream = g_memory_input_stream_new ();

	start = comma ? comma + 1 : uri->path;
	if (*start) {
		guchar *buf;

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
			data->priv->content_length =
				g_base64_decode_step (start, inlen, buf,
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
			data->priv->content_length = strlen ((char *)buf);
		}

		g_memory_input_stream_add_data (G_MEMORY_INPUT_STREAM (memstream),
						buf, data->priv->content_length,
						g_free);
	}

	return memstream;

fail:
	g_object_unref (memstream);
	return NULL;
}

static goffset
soup_request_data_get_content_length (SoupRequest *request)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (request);

	return data->priv->content_length;
}

static const char *
soup_request_data_get_content_type (SoupRequest *request)
{
	SoupRequestData *data = SOUP_REQUEST_DATA (request);

	return data->priv->content_type;
}

static void
soup_request_data_class_init (SoupRequestDataClass *request_data_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_data_class);
	SoupRequestClass *request_class =
		SOUP_REQUEST_CLASS (request_data_class);

	g_type_class_add_private (request_data_class, sizeof (SoupRequestDataPrivate));

	object_class->finalize = soup_request_data_finalize;

	request_class->check_uri = soup_request_data_check_uri;
	request_class->send = soup_request_data_send;
	request_class->get_content_length = soup_request_data_get_content_length;
	request_class->get_content_type = soup_request_data_get_content_type;
}

