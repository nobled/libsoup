/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-request-http.c: http: URI request object
 *
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include "soup-request-http.h"
#include "soup-http-input-stream.h"
#include "soup-message.h"
#include "soup-uri.h"

G_DEFINE_TYPE (SoupRequestHTTP, soup_request_http, SOUP_TYPE_REQUEST)

struct _SoupRequestHTTPPrivate {
	SoupMessage *msg;
};

static void
soup_request_http_init (SoupRequestHTTP *http)
{
	http->priv = G_TYPE_INSTANCE_GET_PRIVATE (http, SOUP_TYPE_REQUEST_HTTP, SoupRequestHTTPPrivate);
}

static gboolean
soup_request_http_check_uri (SoupRequest  *request,
			     SoupURI      *uri,
			     GError      **error)
{
	SoupRequestHTTP *http = SOUP_REQUEST_HTTP (request);

	if (!SOUP_URI_VALID_FOR_HTTP (uri))
		return FALSE;

	http->priv->msg = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	return TRUE;
}

static void
soup_request_http_finalize (GObject *object)
{
	SoupRequestHTTP *http = SOUP_REQUEST_HTTP (object);

	if (http->priv->msg)
		g_object_unref (http->priv->msg);

	G_OBJECT_CLASS (soup_request_http_parent_class)->finalize (object);
}

static GInputStream *
soup_request_http_send (SoupRequest          *request,
			GCancellable         *cancellable,
			GError              **error)
{
	SoupRequestHTTP *http = SOUP_REQUEST_HTTP (request);
	SoupHTTPInputStream *httpstream;

	httpstream = soup_http_input_stream_new (soup_request_get_session (request), http->priv->msg);
	if (!soup_http_input_stream_send (httpstream, cancellable, error)) {
		g_object_unref (httpstream);
		return NULL;
	}
	return (GInputStream *)httpstream;
}

static void
sent_async (GObject *source, GAsyncResult *result, gpointer user_data)
{
	SoupHTTPInputStream *httpstream = SOUP_HTTP_INPUT_STREAM (source);
	GSimpleAsyncResult *simple = user_data;
	GError *error = NULL;

	if (soup_http_input_stream_send_finish (httpstream, result, &error)) {
		g_simple_async_result_set_op_res_gpointer (simple, httpstream, g_object_unref);
	} else {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
		g_object_unref (httpstream);
	}
	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}

static void
soup_request_http_send_async (SoupRequest          *request,
			      GCancellable         *cancellable,
			      GAsyncReadyCallback   callback,
			      gpointer              user_data)
{
	SoupRequestHTTP *http = SOUP_REQUEST_HTTP (request);
	SoupHTTPInputStream *httpstream;
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (G_OBJECT (http),
					    callback, user_data,
					    soup_request_http_send_async);
	httpstream = soup_http_input_stream_new (soup_request_get_session (request),
						 http->priv->msg);
	soup_http_input_stream_send_async (httpstream, G_PRIORITY_DEFAULT,
					   cancellable, sent_async, simple);
}

static GInputStream *
soup_request_http_send_finish (SoupRequest          *request,
			      GAsyncResult         *result,
			      GError              **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (request), soup_request_http_send_async), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;
	return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}

static goffset
soup_request_http_get_content_length (SoupRequest *request)
{
	SoupRequestHTTP *http = SOUP_REQUEST_HTTP (request);

	return soup_message_headers_get_content_length (http->priv->msg->response_headers);
}

static const char *
soup_request_http_get_content_type (SoupRequest *request)
{
	SoupRequestHTTP *http = SOUP_REQUEST_HTTP (request);

	return soup_message_headers_get_content_type (http->priv->msg->response_headers, NULL);
}

static void
soup_request_http_class_init (SoupRequestHTTPClass *request_http_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_http_class);
	SoupRequestClass *request_class =
		SOUP_REQUEST_CLASS (request_http_class);

	g_type_class_add_private (request_http_class, sizeof (SoupRequestHTTPPrivate));

	object_class->finalize = soup_request_http_finalize;

	request_class->check_uri = soup_request_http_check_uri;
	request_class->send = soup_request_http_send;
	request_class->send_async = soup_request_http_send_async;
	request_class->send_finish = soup_request_http_send_finish;
	request_class->get_content_length = soup_request_http_get_content_length;
	request_class->get_content_type = soup_request_http_get_content_type;
}
