/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-content-sniffer.c
 *
 * Copyright (C) 2009 Gustavo Noronha Silva.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gio/gio.h>

#include "soup-content-sniffer.h"
#include "soup-enum-types.h"
#include "soup-message.h"
#include "soup-message-private.h"
#include "soup-session-feature.h"
#include "soup-uri.h"

/**
 * SECTION:soup-content-sniffer
 * @short_description: Content sniffing for #SoupSession
 *
 * A #SoupContentSniffer tries to detect the actual content type of
 * the files that are being downloaded by looking at some of the data
 * before the #SoupMessage emits its #SoupMessage::got-headers signal.
 * #SoupContentSniffer implements #SoupSessionFeature, so you can add
 * content sniffing to a session with soup_session_add_feature() or
 * soup_session_add_feature_by_type().
 *
 * Since: 2.27.3
 **/

static char* sniff (SoupContentSniffer *sniffer, SoupMessage *msg, SoupBuffer *buffer, gboolean *uncertain);
static gsize get_buffer_size (SoupContentSniffer *sniffer);

static void soup_content_sniffer_session_feature_init (SoupSessionFeatureInterface *feature_interface, gpointer interface_data);

static void request_queued (SoupSessionFeature *feature, SoupSession *session, SoupMessage *msg);
static void request_unqueued (SoupSessionFeature *feature, SoupSession *session, SoupMessage *msg);

G_DEFINE_TYPE_WITH_CODE (SoupContentSniffer, soup_content_sniffer, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_SESSION_FEATURE,
						soup_content_sniffer_session_feature_init))

static void
soup_content_sniffer_init (SoupContentSniffer *content_sniffer)
{
}

static void
soup_content_sniffer_class_init (SoupContentSnifferClass *content_sniffer_class)
{
	content_sniffer_class->sniff = sniff;
	content_sniffer_class->get_buffer_size = get_buffer_size;
}

static void
soup_content_sniffer_session_feature_init (SoupSessionFeatureInterface *feature_interface,
					   gpointer interface_data)
{
	feature_interface->request_queued = request_queued;
	feature_interface->request_unqueued = request_unqueued;
}

/**
 * soup_content_sniffer_new:
 *
 * Creates a new #SoupContentSniffer.
 *
 * Returns: a new #SoupContentSniffer
 *
 * Since: 2.27.3
 **/
SoupContentSniffer *
soup_content_sniffer_new ()
{
	return g_object_new (SOUP_TYPE_CONTENT_SNIFFER, NULL);
}

static char*
sniff (SoupContentSniffer *sniffer, SoupMessage *msg, SoupBuffer *buffer, gboolean *uncertain)
{
	SoupURI *uri;
	char *uri_path;
	char *content_type;
	char *mime_type;

	uri = soup_message_get_uri (msg);
	uri_path = soup_uri_to_string (uri, TRUE);

	content_type= g_content_type_guess (uri_path, (const guchar*)buffer->data, buffer->length, uncertain);
	mime_type = g_content_type_get_mime_type (content_type);

	g_free (uri_path);
	g_free (content_type);

	return mime_type;
}

static gsize
get_buffer_size (SoupContentSniffer *sniffer)
{
	return 512;
}

static void
soup_content_sniffer_got_headers_cb (SoupMessage *msg, SoupContentSniffer *sniffer)
{
	SoupMessagePrivate *priv = SOUP_MESSAGE_GET_PRIVATE (msg);
	SoupContentSnifferClass *content_sniffer_class = SOUP_CONTENT_SNIFFER_GET_CLASS (sniffer);
	const char *content_type = soup_message_headers_get_content_type (msg->response_headers, NULL);

	if ((content_type == NULL)
	    || (strcmp (content_type, "application/octet-stream") == 0)
	    || (strcmp (content_type, "text/plain") == 0)) {
		priv->should_sniff_content = TRUE;
		priv->bytes_for_sniffing = content_sniffer_class->get_buffer_size (sniffer);
	}
}

static void
request_queued (SoupSessionFeature *feature, SoupSession *session,
		SoupMessage *msg)
{
	SoupMessagePrivate *priv = SOUP_MESSAGE_GET_PRIVATE (msg);

	priv->sniffer = g_object_ref (feature);
	g_signal_connect (msg, "got-headers",
			  G_CALLBACK (soup_content_sniffer_got_headers_cb),
			  feature);
}

static void
request_unqueued (SoupSessionFeature *feature, SoupSession *session,
		  SoupMessage *msg)
{
	SoupMessagePrivate *priv = SOUP_MESSAGE_GET_PRIVATE (msg);

	g_object_unref (priv->sniffer);
	priv->sniffer = NULL;

	g_signal_handlers_disconnect_by_func (msg, soup_content_sniffer_got_headers_cb, feature);
}
