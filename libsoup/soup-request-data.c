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

G_DEFINE_TYPE_WITH_CODE (SoupRequestData, soup_request_data, SOUP_TYPE_REQUEST_BASE,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_REQUEST,
						soup_request_data_request_interface_init))

static gboolean soup_request_data_validate_uri (SoupRequestBase  *req_base,
						SoupURI          *uri,
						GError          **error);

static GInputStream *soup_request_data_send (SoupRequest   *request,
					     GCancellable  *cancellable,
					     GError       **error);

static void
soup_request_data_class_init (SoupRequestDataClass *request_data_class)
{
	SoupRequestBaseClass *request_base_class =
		SOUP_REQUEST_BASE_CLASS (request_data_class);

	request_base_class->validate_uri = soup_request_data_validate_uri;
}

static void
soup_request_data_request_interface_init (SoupRequestInterface *request_interface)
{
	request_interface->send = soup_request_data_send;
}

static void
soup_request_data_init (SoupRequestData *data)
{
}

static gboolean
soup_request_data_validate_uri (SoupRequestBase  *req_base,
				SoupURI          *uri,
				GError          **error)
{
	return uri->host == NULL;
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
	SoupURI *uri = soup_request_base_get_uri (SOUP_REQUEST_BASE (request));

	return data_uri_decode (uri->path, NULL);
}
