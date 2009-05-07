/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-session-async.c
 *
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-address.h"
#include "soup-session-async.h"
#include "soup-session-private.h"
#include "soup-address.h"
#include "soup-message-private.h"
#include "soup-misc.h"

/**
 * SECTION:soup-session-async
 * @short_description: Soup session for asynchronous (main-loop-based) I/O.
 *
 * #SoupSessionAsync is an implementation of #SoupSession that uses
 * non-blocking I/O via the glib main loop. It is intended for use in
 * single-threaded programs.
 **/

static void run_queue (SoupSessionAsync *sa);
static void do_idle_run_queue (SoupSession *session);

static void  queue_message   (SoupSession *session, SoupMessage *req,
			      SoupSessionCallback callback, gpointer user_data);
static guint send_message    (SoupSession *session, SoupMessage *req);

G_DEFINE_TYPE (SoupSessionAsync, soup_session_async, SOUP_TYPE_SESSION)

typedef struct {
	GSource *idle_run_queue_source;
} SoupSessionAsyncPrivate;
#define SOUP_SESSION_ASYNC_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SOUP_TYPE_SESSION_ASYNC, SoupSessionAsyncPrivate))

static void
soup_session_async_init (SoupSessionAsync *sa)
{
}

static void
finalize (GObject *object)
{
	SoupSessionAsyncPrivate *priv = SOUP_SESSION_ASYNC_GET_PRIVATE (object);

	if (priv->idle_run_queue_source)
		g_source_destroy (priv->idle_run_queue_source);

	G_OBJECT_CLASS (soup_session_async_parent_class)->finalize (object);
}

static void
soup_session_async_class_init (SoupSessionAsyncClass *soup_session_async_class)
{
	SoupSessionClass *session_class = SOUP_SESSION_CLASS (soup_session_async_class);
	GObjectClass *object_class = G_OBJECT_CLASS (session_class);

	g_type_class_add_private (soup_session_async_class,
				  sizeof (SoupSessionAsyncPrivate));

	/* virtual method override */
	session_class->queue_message = queue_message;
	session_class->send_message = send_message;

	object_class->finalize = finalize;
}


/**
 * soup_session_async_new:
 *
 * Creates an asynchronous #SoupSession with the default options.
 *
 * Return value: the new session.
 **/
SoupSession *
soup_session_async_new (void)
{
	return g_object_new (SOUP_TYPE_SESSION_ASYNC, NULL);
}

/**
 * soup_session_async_new_with_options:
 * @optname1: name of first property to set
 * @...: value of @optname1, followed by additional property/value pairs
 *
 * Creates an asynchronous #SoupSession with the specified options.
 *
 * Return value: the new session.
 **/
SoupSession *
soup_session_async_new_with_options (const char *optname1, ...)
{
	SoupSession *session;
	va_list ap;

	va_start (ap, optname1);
	session = (SoupSession *)g_object_new_valist (SOUP_TYPE_SESSION_ASYNC,
						      optname1, ap);
	va_end (ap);

	return session;
}


static void
resolved_msg_addr (SoupAddress *addr, guint status, gpointer user_data)
{
	SoupMessageQueueItem *item = user_data;
	SoupSession *session = item->session;

	if (item->removed) {
		/* Message was cancelled before its address resolved */
		soup_message_queue_item_unref (item);
		return;
	}

	if (!SOUP_STATUS_IS_SUCCESSFUL (status)) {
		soup_session_cancel_message (session, item->msg, status);
		soup_message_queue_item_unref (item);
		return;
	}

	item->msg_addr = g_object_ref (addr);
	item->resolving_msg_addr = FALSE;

	soup_message_queue_item_unref (item);

	/* If we got here we know session still exists */
	run_queue ((SoupSessionAsync *)session);
}

static void
resolve_msg_addr (SoupMessageQueueItem *item)
{
	if (item->resolving_msg_addr)
		return;
	item->resolving_msg_addr = TRUE;

	soup_message_queue_item_ref (item);
	soup_address_resolve_async (soup_message_get_address (item->msg),
				    soup_session_get_async_context (item->session),
				    item->cancellable,
				    resolved_msg_addr, item);
}

static void
resolved_proxy_addr (SoupProxyResolver *proxy_resolver, SoupMessage *msg,
		     guint status, SoupAddress *proxy_addr, gpointer user_data)
{
	SoupMessageQueueItem *item = user_data;
	SoupSession *session = item->session;

	if (item->removed) {
		/* Message was cancelled before its proxy addr resolved */
		soup_message_queue_item_unref (item);
		return;
	}

	if (!SOUP_STATUS_IS_SUCCESSFUL (status)) {
		soup_session_cancel_message (session, item->msg, status);
		soup_message_queue_item_unref (item);
		return;
	}

	item->resolving_proxy_addr = FALSE;
	item->resolved_proxy_addr = TRUE;
	item->proxy_addr = proxy_addr ? g_object_ref (proxy_addr) : NULL;

	soup_message_queue_item_unref (item);

	/* If we got here we know session still exists */
	run_queue ((SoupSessionAsync *)session);
}

static void
resolve_proxy_addr (SoupMessageQueueItem *item,
		    SoupProxyResolver *proxy_resolver)
{
	if (item->resolving_proxy_addr)
		return;
	item->resolving_proxy_addr = TRUE;

	soup_message_queue_item_ref (item);
	soup_proxy_resolver_get_proxy_async (proxy_resolver, item->msg,
					     soup_session_get_async_context (item->session),
					     item->cancellable,
					     resolved_proxy_addr, item);
}

static void
connection_closed (SoupConnection *conn, gpointer session)
{
	/* Run the queue in case anyone was waiting for a connection
	 * to be closed.
	 */
	do_idle_run_queue (session);
}

static void
got_connection (SoupConnection *conn, guint status, gpointer user_data)
{
	SoupSession *session = user_data;

	if (status == SOUP_STATUS_OK) {
		g_signal_connect (conn, "disconnected",
				  G_CALLBACK (connection_closed), session);

		/* @conn has been marked reserved by SoupSession, but
		 * we don't actually have any specific message in mind
		 * for it. (In particular, the message we were
		 * originally planning to queue on it may have already
		 * been queued on some other connection that became
		 * available while we were waiting for this one to
		 * connect.) So we release the connection into the
		 * idle pool and then just run the queue and see what
		 * happens.
		 */
		soup_connection_release (conn);
	}

	/* Even if the connection failed, we run the queue, since
	 * there may have been messages waiting for the connection
	 * count to go down.
	 */
	do_idle_run_queue (session);
	g_object_unref (session);
}

static void
run_queue (SoupSessionAsync *sa)
{
	SoupSession *session = SOUP_SESSION (sa);
	SoupMessageQueue *queue = soup_session_get_queue (session);
	SoupMessageQueueItem *item;
	SoupProxyResolver *proxy_resolver =
		soup_session_get_proxy_resolver (session);
	SoupMessage *msg;
	SoupMessageIOStatus cur_io_status = SOUP_MESSAGE_IO_STATUS_CONNECTING;
	SoupConnection *conn;
	gboolean try_pruning = TRUE, should_prune = FALSE;
	gboolean is_new;

 try_again:
	for (item = soup_message_queue_first (queue);
	     item && !should_prune;
	     item = soup_message_queue_next (queue, item)) {
		msg = item->msg;

		if (soup_message_get_io_status (msg) != cur_io_status ||
		    soup_message_io_in_progress (msg))
			continue;

		if (!item->msg_addr) {
			resolve_msg_addr (item);
			continue;
		}
		if (proxy_resolver && !item->resolved_proxy_addr) {
			resolve_proxy_addr (item, proxy_resolver);
			continue;
		}

		conn = soup_session_get_connection (session, msg,
						    item->proxy_addr,
						    &should_prune, &is_new);
		if (!conn)
			continue;

		if (is_new) {
			soup_connection_connect_async (conn, got_connection,
						       g_object_ref (session));
		} else
			soup_connection_send_request (conn, msg);
	}
	if (item)
		soup_message_queue_item_unref (item);

	if (cur_io_status == SOUP_MESSAGE_IO_STATUS_CONNECTING) {
		cur_io_status = SOUP_MESSAGE_IO_STATUS_QUEUED;
		goto try_again;
	}

	if (try_pruning && should_prune) {
		/* There is at least one message in the queue that
		 * could be sent if we pruned an idle connection from
		 * some other server.
		 */
		if (soup_session_try_prune_connection (session)) {
			try_pruning = should_prune = FALSE;
			goto try_again;
		}
	}
}

static void
request_restarted (SoupMessage *req, gpointer user_data)
{
	SoupMessageQueueItem *item = user_data;

	if (item->msg_addr &&
	    item->msg_addr != soup_message_get_address (item->msg)) {
		g_object_unref (item->msg_addr);
		item->msg_addr = NULL;
	}
	if (item->proxy_addr) {
		g_object_unref (item->proxy_addr);
		item->proxy_addr = NULL;
	}
	item->resolved_proxy_addr = FALSE;

	run_queue ((SoupSessionAsync *)item->session);
}

static void
final_finished (SoupMessage *req, gpointer user_data)
{
	SoupMessageQueueItem *item = user_data;
	SoupSession *session = item->session;

	g_object_ref (session);

	if (!SOUP_MESSAGE_IS_STARTING (req)) {
		g_signal_handlers_disconnect_by_func (req, final_finished, item);
		g_signal_handlers_disconnect_by_func (req, request_restarted, item);
		if (item->callback)
			item->callback (session, req, item->callback_data);

		g_object_unref (req);
		soup_message_queue_item_unref (item);
	}

	do_idle_run_queue (session);
	g_object_unref (session);
}

static gboolean
idle_run_queue (gpointer sa)
{
	SoupSessionAsyncPrivate *priv = SOUP_SESSION_ASYNC_GET_PRIVATE (sa);

	priv->idle_run_queue_source = NULL;
	run_queue (sa);
	return FALSE;
}

static void
do_idle_run_queue (SoupSession *session)
{
	SoupSessionAsyncPrivate *priv = SOUP_SESSION_ASYNC_GET_PRIVATE (session);

	if (!priv->idle_run_queue_source) {
		priv->idle_run_queue_source = soup_add_completion (
			soup_session_get_async_context (session),
			idle_run_queue, session);
	}
}

static gboolean
had_cache (SoupMessageQueueItem *item)
{
	SoupCache *cache;

	cache = soup_session_get_cache (item->session);
	soup_cache_send_response (cache, item->session, item->msg);
	return FALSE;
}

static void
queue_message (SoupSession *session, SoupMessage *req,
	       SoupSessionCallback callback, gpointer user_data)
{
	SoupCache *cache;
	SoupMessageQueueItem *item;

	SOUP_SESSION_CLASS (soup_session_async_parent_class)->queue_message (session, req, callback, user_data);

	item = soup_message_queue_lookup (soup_session_get_queue (session), req);
	g_return_if_fail (item != NULL);

	g_signal_connect (req, "restarted",
			  G_CALLBACK (request_restarted), item);
	g_signal_connect_after (req, "finished",
				G_CALLBACK (final_finished), item);

	cache = soup_session_get_cache (session);
	if (cache && soup_cache_has_response (cache, session, req)) {
		g_idle_add ((GSourceFunc)had_cache, item);
		return;
	}

	do_idle_run_queue (session);
}

static guint
send_message (SoupSession *session, SoupMessage *req)
{
	GMainContext *async_context =
		soup_session_get_async_context (session);

	/* Balance out the unref that final_finished will do */
	g_object_ref (req);

	queue_message (session, req, NULL, NULL);

	while (soup_message_get_io_status (req) != SOUP_MESSAGE_IO_STATUS_FINISHED &&
	       !SOUP_STATUS_IS_TRANSPORT_ERROR (req->status_code))
		g_main_context_iteration (async_context, TRUE);

	return req->status_code;
}
