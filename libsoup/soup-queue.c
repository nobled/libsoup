/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-queue.c: Asyncronous Callback-based HTTP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-queue.h"
#include "soup-connection.h"
#include "soup-misc.h"
#include "soup-private.h"

static GSList *soup_active_requests = NULL, *soup_active_request_next = NULL;

static guint soup_queue_idle_tag = 0;

void
soup_queue_connect_cb (SoupContext        *ctx,
		       SoupKnownErrorCode  err,
		       SoupConnection     *conn,
		       gpointer            user_data)
{
	SoupMessage *req = user_data;

	req->priv->connect_tag = NULL;

	if (err != SOUP_ERROR_OK) {
		soup_message_set_error (req, err);
		soup_message_issue_callback (req);
	} else
		soup_connection_start_request (conn, req);

	return;
}

void
soup_queue_add_request (SoupMessage *req)
{
	soup_active_requests = g_slist_prepend (soup_active_requests, req);
}

void
soup_queue_remove_request (SoupMessage *req)
{
	if (soup_active_request_next && soup_active_request_next->data == req)
		soup_queue_next_request ();
	soup_active_requests = g_slist_remove (soup_active_requests, req);
}

SoupMessage *
soup_queue_first_request (void)
{
	if (!soup_active_requests)
		return NULL;

	soup_active_request_next = soup_active_requests->next;
	return soup_active_requests->data;
}

SoupMessage *
soup_queue_next_request (void)
{
	SoupMessage *ret;

	if (!soup_active_request_next)
		return NULL;
	ret = soup_active_request_next->data;
	soup_active_request_next = soup_active_request_next->next;
	return ret;
}

static gboolean
request_in_progress (SoupMessage *req)
{
	if (!soup_active_requests)
		return FALSE;

	return g_slist_index (soup_active_requests, req) != -1;
}

static gboolean 
soup_idle_handle_new_requests (gpointer unused)
{
	SoupMessage *req = soup_queue_first_request ();

	for (; req; req = soup_queue_next_request ()) {
		if (req->status != SOUP_STATUS_QUEUED)
			continue;

		req->status = SOUP_STATUS_CONNECTING;

		if (req->connection)
			soup_connection_start_request (req->connection, req);
		else {
			gpointer connect_tag;

			connect_tag = 
				soup_context_get_connection (
					req->priv->context,
					soup_queue_connect_cb, 
					req);

			if (connect_tag && request_in_progress (req))
				req->priv->connect_tag = connect_tag;
		}
	}

	soup_queue_idle_tag = 0;
	return FALSE;
}

static void
soup_queue_initialize (void)
{
	if (!soup_initialized)
		soup_load_config (NULL);

	if (!soup_queue_idle_tag)
		soup_queue_idle_tag = 
			g_idle_add (soup_idle_handle_new_requests, NULL);
}

void 
soup_queue_message (SoupMessage *req)
{
	g_return_if_fail (SOUP_IS_MESSAGE (req));

	if (!req->priv->context) {
		soup_message_set_error_full (req, 
					     SOUP_ERROR_CANCELLED,
					     "Attempted to queue a message "
					     "with no destination context");
		soup_message_issue_callback (req);
		return;
	}

	if (req->status != SOUP_STATUS_IDLE)
		soup_message_cleanup (req);

	switch (req->response.owner) {
	case SOUP_BUFFER_USER_OWNED:
		soup_message_set_error_full (req, 
					     SOUP_ERROR_CANCELLED,
					     "Attempted to queue a message "
					     "with a user owned response "
					     "buffer.");
		soup_message_issue_callback (req);
		return;
	case SOUP_BUFFER_SYSTEM_OWNED:
		g_free (req->response.body);
		break;
	case SOUP_BUFFER_STATIC:
		break;
	}

	req->response.owner = 0;
	req->response.body = NULL;
	req->response.length = 0;

	soup_message_clear_headers (req->response_headers);

	req->errorcode = 0;
	req->errorclass = 0;

	if (req->errorphrase) {
		g_free ((gchar *) req->errorphrase);
		req->errorphrase = NULL;
	}

	req->status = SOUP_STATUS_QUEUED;

	soup_queue_add_request (req);

	soup_queue_initialize ();
}

/**
 * soup_queue_shutdown:
 * 
 * Shut down the message queue by calling %soup_message_cancel on all active
 * requests.
 */
void 
soup_queue_shutdown (void)
{
	SoupMessage *req;

	soup_initialized = FALSE;

	if (soup_queue_idle_tag) {
		g_source_remove (soup_queue_idle_tag);
		soup_queue_idle_tag = 0;
	}

	req = soup_queue_first_request ();
	for (; req; req = soup_queue_next_request ())
		soup_message_cancel (req);
}
