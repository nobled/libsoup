/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-message.c: Asyncronous Callback-based HTTP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#include "soup-auth.h"
#include "soup-error.h"
#include "soup-message.h"
#include "soup-misc.h"
#include "soup-context.h"
#include "soup-private.h"
#include "soup-queue.h"
#include "soup-transfer.h"

void
soup_message_construct (SoupMessage *msg)
{
	msg->priv   = g_new0 (SoupMessagePrivate, 1);
	msg->status = SOUP_STATUS_IDLE;

	msg->request_headers  = g_hash_table_new (soup_str_case_hash, 
						  soup_str_case_equal);
	msg->response_headers = g_hash_table_new (soup_str_case_hash, 
						  soup_str_case_equal);

	msg->priv->http_version = SOUP_HTTP_1_1;
}

/**
 * soup_message_new:
 * @context: a %SoupContext for the destination endpoint.
 * @method: a string which will be used as the HTTP method for the created
 * request, if NULL a GET request will be made.
 * 
 * Creates a new empty %SoupMessage, which will connect to the URL represented
 * by @context.  A reference will be added to @context.
 * 
 * The new message has a status of @SOUP_STATUS_IDLE.
 *
 * Return value: the new %SoupMessage.
 */
SoupMessage *
soup_message_new (SoupContext *context, const gchar *method) 
{
	SoupMessage *ret;

	ret = g_new0 (SoupMessage, 1);
	soup_message_construct (ret);

	ret->method  = method ? method : SOUP_METHOD_GET;
	soup_message_set_context (ret, context);

	return ret;
}

/**
 * soup_message_new_full:
 * @context: a %SoupContext for the destination endpoint.
 * @method: a string which will be used as the HTTP method for the created
 * request, if NULL a GET request will be made..
 * @req_owner: the %SoupOwnership of the passed data buffer.
 * @req_body: a data buffer containing the body of the message request.
 * @req_length: the byte length of @req_body.
 * 
 * Creates a new %SoupMessage, which will connect to the URL represented by
 * @context.  A reference is added to @context.  The request data
 * buffer will be filled from @req_owner, @req_body, and @req_length
 * respectively.
 *
 * The new message has a status of @SOUP_STATUS_IDLE.
 *
 * Return value: the new %SoupMessage.
 */
SoupMessage *
soup_message_new_full (SoupContext   *context,
		       const gchar   *method,
		       SoupOwnership  req_owner,
		       gchar         *req_body,
		       gulong         req_length)
{
	SoupMessage *ret = soup_message_new (context, method);

	ret->request.owner = req_owner;
	ret->request.body = req_body;
	ret->request.length = req_length;

	return ret;
}

static void
copy_header (gchar *key, gchar *value, GHashTable *dest_hash)
{
	soup_message_add_header (dest_hash, key, value);
}

/*
 * Copy context, method, error, request buffer, response buffer, request
 * headers, response headers, message flags, http version.  
 *
 * Callback function, content handlers, message status, server, server socket,
 * and server message are NOT copied.  
 */
SoupMessage *
soup_message_copy (SoupMessage *req)
{
	SoupMessage *cpy;

	cpy = soup_message_new (req->context, req->method);

	cpy->errorcode = req->errorcode;
	cpy->errorclass = req->errorclass;
	cpy->errorphrase = req->errorphrase;

	cpy->request.owner = SOUP_BUFFER_SYSTEM_OWNED;
	cpy->request.length = cpy->request.length;
	cpy->request.body = g_memdup (req->request.body, 
				      req->request.length);

	cpy->response.owner = SOUP_BUFFER_SYSTEM_OWNED;
	cpy->response.length = cpy->response.length;
	cpy->response.body = g_memdup (req->response.body, 
				       req->response.length);

	soup_message_foreach_header (req->request_headers,
				     (GHFunc) copy_header,
				     cpy->request_headers);

	soup_message_foreach_header (req->response_headers,
				     (GHFunc) copy_header,
				     cpy->response_headers);

	cpy->priv->msg_flags = req->priv->msg_flags;

	/* 
	 * Note: We don't copy content handlers or callback function as this is
	 * a nightmare double free situation.  
	 */

	cpy->priv->http_version = req->priv->http_version;

	return cpy;
}

static void 
release_connection (const SoupDataBuffer *data,
		    gpointer              user_data)
{
	SoupConnection *conn = user_data;
	soup_connection_release (conn);
}

static void 
release_and_close_connection (gboolean headers_done, gpointer user_data)
{
	SoupConnection *conn = user_data;
	soup_connection_set_keep_alive (conn, FALSE);
	soup_connection_release (conn);
}

/**
 * soup_message_cleanup:
 * @req: a %SoupMessage.
 * 
 * Frees any temporary resources created in the processing of @req.  Also
 * releases the active connection, if one exists. Request and response data
 * buffers are left intact. 
 */
void 
soup_message_cleanup (SoupMessage *req)
{
	g_return_if_fail (req != NULL);

	if (req->connection && 
	    req->priv->read_tag &&
	    req->status == SOUP_STATUS_READING_RESPONSE) {
		soup_transfer_read_set_callbacks (req->priv->read_tag,
						  NULL,
						  NULL,
						  release_connection,
						  release_and_close_connection,
						  req->connection);
		req->priv->read_tag = 0;
		req->connection = NULL;
		/* 
		 * The buffer doesn't belong to us until the message is 
		 * finished.
		 */
		req->response.owner = SOUP_BUFFER_STATIC;
	}

	if (req->priv->read_tag) {
		soup_transfer_read_cancel (req->priv->read_tag);
		req->priv->read_tag = 0;
	}

	if (req->priv->write_tag) {
		soup_transfer_write_cancel (req->priv->write_tag);
		req->priv->write_tag = 0;
	}

	if (req->priv->connect_tag) {
		soup_context_cancel_connect (req->priv->connect_tag);
		req->priv->connect_tag = NULL;
	}

	if (req->connection) {
		soup_connection_release (req->connection);
		req->connection = NULL;
	}

	soup_queue_remove_request (req);
}

static void
finalize_message (SoupMessage *req)
{
	if (req->context)
		soup_context_unref (req->context);

	if (req->request.owner == SOUP_BUFFER_SYSTEM_OWNED)
		g_free (req->request.body);
	if (req->response.owner == SOUP_BUFFER_SYSTEM_OWNED)
		g_free (req->response.body);

	soup_message_clear_headers (req->request_headers);
	g_hash_table_destroy (req->request_headers);

	soup_message_clear_headers (req->response_headers);
	g_hash_table_destroy (req->response_headers);

	g_slist_foreach (req->priv->content_handlers, (GFunc) g_free, NULL);
	g_slist_free (req->priv->content_handlers);

	g_free ((gchar *) req->errorphrase);
	g_free (req->priv);
	g_free (req);
}

/**
 * soup_message_free:
 * @req: a %SoupMessage to destroy.
 * 
 * Destroys the %SoupMessage pointed to by @req. Request and response headers
 * are freed. Request and response data buffers are also freed if their
 * ownership is %SOUP_BUFFER_SYSTEM_OWNED. The message's destination context
 * will be de-referenced.
 */
void 
soup_message_free (SoupMessage *req)
{
	g_return_if_fail (req != NULL);

	soup_message_cleanup (req);

	finalize_message (req);
}

/**
 * soup_message_issue_callback:
 * @req: a %SoupMessage currently being processed.
 * @error: a %SoupErrorCode to be passed to %req's completion callback.
 * 
 * Finalizes the message request, by first freeing any temporary resources, then
 * issuing the callback function pointer passed in %soup_message_new or
 * %soup_message_new_full. If, after returning from the callback, the message
 * has not been requeued, @msg is destroyed using %soup_message_free.
 */
void
soup_message_issue_callback (SoupMessage *req)
{
	g_return_if_fail (req != NULL);

	/* 
	 * Make sure we don't have some icky recursion if the callback 
	 * runs the main loop, and the connection has some data or error 
	 * which causes the callback to be run again.
	 */
	soup_message_cleanup (req);

	if (req->priv->callback) {
		(*req->priv->callback) (req, req->priv->user_data);

		if (req->status != SOUP_STATUS_QUEUED)
			finalize_message (req);
	}
}

/**
 * soup_message_cancel:
 * @req: a %SoupMessage currently being processed.
 * 
 * Cancel a running message, and issue completion callback with a
 * %SoupTransferStatus of %SOUP_ERROR_CANCELLED. If not requeued by the
 * completion callback, the @msg will be destroyed.
 */
void 
soup_message_cancel (SoupMessage *msg) 
{
	soup_message_set_error (msg, SOUP_ERROR_CANCELLED);

	/* Kill the connection as a safety measure */
	if (msg->connection)
		soup_connection_set_keep_alive (msg->connection, FALSE);

	soup_message_issue_callback (msg);
}

static gboolean 
foreach_free_header_list (gchar *name, GSList *vals, gpointer notused)
{
	g_free (name);
	g_slist_foreach (vals, (GFunc) g_free, NULL);
	g_slist_free (vals);

	return TRUE;
}

void
soup_message_clear_headers       (GHashTable        *hash)
{
	g_return_if_fail (hash != NULL);

	g_hash_table_foreach_remove (hash, 
				     (GHRFunc) foreach_free_header_list, 
				     NULL);
}

void 
soup_message_remove_header (GHashTable  *hash,
			    const gchar *name)
{
	gchar *stored_key;
	GSList *vals;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (name != NULL || name [0] != '\0');

	if (g_hash_table_lookup_extended (hash, 
					  name, 
					  (gpointer *) &stored_key, 
					  (gpointer *) &vals)) {
		g_hash_table_remove (hash, name);
		foreach_free_header_list (stored_key, vals, NULL);
	}
}

void 
soup_message_add_header (GHashTable  *hash,
			 const gchar *name,
			 const gchar *value) 
{
	GSList *old_value;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (name != NULL || name [0] != '\0');
	g_return_if_fail (value != NULL);

	old_value = g_hash_table_lookup (hash, name);

	if (old_value)
		g_slist_append (old_value, g_strdup (value));
	else
		g_hash_table_insert (hash, 
				     g_strdup (name), 
				     g_slist_append (NULL, 
						     g_strdup (value)));
}

/**
 * soup_message_get_header:
 * @req: a %SoupMessage.
 * @name: header name.
 * 
 * Lookup the first transport header with a key equal to @name.
 *
 * Return value: the header's value or NULL if not found.
 */
const gchar *
soup_message_get_header (GHashTable *hash,
			 const gchar *name)
{
	GSList *vals;

	g_return_val_if_fail (hash != NULL, NULL);
	g_return_val_if_fail (name != NULL || name [0] != '\0', NULL);	

	vals = g_hash_table_lookup (hash, name);
	if (vals) 
		return vals->data;

	return NULL;
}

/**
 * soup_message_get_header_list:
 * @req: a %SoupMessage.
 * @name: header name.
 * 
 * Lookup the all transport request headers with a key equal to @name.
 *
 * Return value: a const pointer to a GSList of header values or NULL if not
 * found.  
 */
const GSList *
soup_message_get_header_list (GHashTable  *hash,
			      const gchar *name)
{
	g_return_val_if_fail (hash != NULL, NULL);
	g_return_val_if_fail (name != NULL || name [0] != '\0', NULL);	

	return g_hash_table_lookup (hash, name);
}

typedef struct {
	GHFunc   func;
	gpointer user_data;
} ForeachData;

static void 
foreach_value_in_list (gchar *name, GSList *vals, ForeachData *data)
{
	while (vals) {
		gchar *v = vals->data;

		(*data->func) (name, v, data->user_data);

		vals = vals->next;
	}
}

void
soup_message_foreach_header      (GHashTable        *hash,
				  GHFunc             func,
				  gpointer           user_data)
{
	ForeachData data = { func, user_data };

	g_return_if_fail (hash != NULL);
	g_return_if_fail (func != NULL);

	g_hash_table_foreach (hash, (GHFunc) foreach_value_in_list, &data);
}

typedef struct {
	GHRFunc   func;
	gpointer user_data;
} ForeachRemoveData;

static gboolean 
foreach_remove_value_in_list (gchar             *name, 
			      GSList            *vals, 
			      ForeachRemoveData *data)
{
	GSList *iter = vals;

	while (iter) {
		gchar *v = iter->data;
		gboolean ret = FALSE;

		ret = (*data->func) (name, v, data->user_data);
		if (ret) {
			GSList *next = iter->next;

			vals = g_slist_remove (vals, v);
			g_free (v);

			iter = next;
		} else
			iter = iter->next;
	}

	if (!vals) {
		g_free (name);
		return TRUE;
	} 

	return FALSE;
}

void
soup_message_foreach_remove_header (GHashTable        *hash,
				    GHRFunc            func,
				    gpointer           user_data)
{
	ForeachRemoveData data = { func, user_data };

	g_return_if_fail (hash != NULL);
	g_return_if_fail (func != NULL);

	g_hash_table_foreach_remove (hash, 
				     (GHRFunc) foreach_remove_value_in_list, 
				     &data);
}

/**
 * soup_message_queue:
 * @req: a %SoupMessage.
 * @callback: a %SoupCallbackFn which will be called after the message completes
 * or when an unrecoverable error occurs.
 * @user_data: a pointer passed to @callback.
 * 
 * Queues the message @req for sending. All messages are processed while the
 * glib main loop runs. If this %SoupMessage has been processed before, any
 * resources related to the time it was last sent are freed.
 *
 * If the response %SoupDataBuffer has an owner of %SOUP_BUFFER_USER_OWNED, the
 * message will not be queued, and @callback will be called with a
 * %SoupErrorCode of %SOUP_ERROR_CANCELLED.
 *
 * Upon message completetion, the callback specified in @callback will be
 * invoked. If after returning from this callback the message has not been
 * requeued using %soup_message_queue, %soup_message_free will be called on
 * @req.
 */
void 
soup_message_queue (SoupMessage    *req,
		    SoupCallbackFn  callback, 
		    gpointer        user_data)
{
	g_return_if_fail (req != NULL);

	req->priv->callback = callback;
	req->priv->user_data = user_data;
	soup_queue_message (req);
}

typedef struct {
	SoupMessage *msg;
	SoupAuth    *conn_auth;
} RequeueConnectData;

static void
requeue_connect_cb (SoupContext        *ctx,
		    SoupKnownErrorCode  err,
		    SoupConnection     *conn,
		    gpointer            user_data)
{
	RequeueConnectData *data = user_data;

	if (conn && !conn->auth)
		conn->auth = data->conn_auth;
	else
		soup_auth_free (data->conn_auth);

	soup_queue_connect_cb (ctx, err, conn, data->msg);
	if (data->msg->errorcode)
		soup_message_issue_callback (data->msg);

	g_free (data);
}

static void
requeue_read_error (gboolean body_started, gpointer user_data)
{
	RequeueConnectData *data = user_data;
	SoupMessage *msg = data->msg;
	SoupContext *dest_ctx = msg->context;

	soup_context_ref (dest_ctx);

	soup_connection_set_keep_alive (msg->connection, FALSE);
	soup_connection_release (msg->connection);
	msg->connection = NULL;

	soup_queue_message (msg);

	msg->status = SOUP_STATUS_CONNECTING;

	msg->priv->connect_tag =
		soup_context_get_connection (dest_ctx, 
					     requeue_connect_cb, 
					     data);

	soup_context_unref (dest_ctx);
}

static void
requeue_read_finished (const SoupDataBuffer *buf,
		       gpointer        user_data)
{
	RequeueConnectData *data = user_data;
	SoupMessage *msg = data->msg;
	SoupConnection *conn = msg->connection;

	if (!soup_connection_is_keep_alive (msg->connection))
		requeue_read_error (FALSE, data);
	else {
		g_free (data);
		msg->connection = NULL;

		soup_queue_message (msg);

		msg->connection = conn;
	}
}

/**
 * soup_message_requeue:
 * @req: a %SoupMessage
 *
 * This causes @req to be placed back on the queue to be attempted again.
 **/
void
soup_message_requeue (SoupMessage *req)
{
	g_return_if_fail (req != NULL);

	if (req->connection && req->connection->auth && req->priv->read_tag) {
		RequeueConnectData *data = NULL;

		data = g_new0 (RequeueConnectData, 1);
		data->msg = req;
		data->conn_auth = req->connection->auth;

		soup_transfer_read_set_callbacks (req->priv->read_tag,
						  NULL,
						  NULL,
						  requeue_read_finished,
						  requeue_read_error,
						  data);
		req->priv->read_tag = 0;
	} else
		soup_queue_message (req);
}

/**
 * soup_message_send:
 * @msg: a %SoupMessage.
 * 
 * Syncronously send @msg. This call will not return until the transfer is
 * finished successfully or there is an unrecoverable error. 
 *
 * @msg is not free'd upon return.
 *
 * Return value: the %SoupErrorClass of the error encountered while sending or
 * reading the response.
 */
SoupErrorClass
soup_message_send (SoupMessage *msg)
{
	soup_message_queue (msg, NULL, NULL);

	while (1) {
		g_main_iteration (TRUE); 

		if (msg->status == SOUP_STATUS_FINISHED || 
		    SOUP_ERROR_IS_TRANSPORT (msg->errorcode))
			break;

		/* Quit if soup_shutdown has been called */ 
		if (!soup_initialized)
			return SOUP_ERROR_CANCELLED;
	}

	return msg->errorclass;
}

static void
maybe_validate_auth (SoupMessage *msg, gpointer user_data)
{
	gboolean proxy = GPOINTER_TO_INT (user_data);
	int auth_failure;
	SoupContext *ctx;
	SoupAuth *auth;

	if (proxy) {
		ctx = soup_get_proxy ();
		auth_failure = SOUP_ERROR_PROXY_UNAUTHORIZED; /* 407 */
	}
	else {
		ctx = msg->context;
		auth_failure = SOUP_ERROR_UNAUTHORIZED; /* 401 */
	}

	auth = soup_auth_lookup (ctx);
	if (!auth)
		return;

	if (msg->errorcode == auth_failure) {
		/* Pass through */
	}
	else if (msg->errorclass == SOUP_ERROR_CLASS_SERVER_ERROR) {
		/* 
		 * We have no way of knowing whether our auth is any good
		 * anymore, so just invalidate it and start from the
		 * beginning next time.
		 */
		soup_auth_invalidate (auth, ctx);
	}
	else {
		auth->status = SOUP_AUTH_STATUS_SUCCESSFUL;
		auth->controlling_msg = NULL;
	}
} /* maybe_validate_auth */

static void 
authorize_handler (SoupMessage *msg, gboolean proxy)
{
	const GSList *vals;
	SoupAuth *auth;
	SoupContext *ctx;
	const SoupUri *uri;

	if (msg->connection->auth &&
	    msg->connection->auth->status == SOUP_AUTH_STATUS_SUCCESSFUL)
		goto THROW_CANT_AUTHENTICATE;

	ctx = proxy ? soup_get_proxy () : msg->context;
	uri = soup_context_get_uri (ctx);

	vals = soup_message_get_header_list (msg->response_headers, 
					     proxy ? 
					             "Proxy-Authenticate" : 
					             "WWW-Authenticate");
	if (!vals) goto THROW_CANT_AUTHENTICATE;

	auth = soup_auth_lookup (ctx);
	if (auth) {
		g_assert (auth->status != SOUP_AUTH_STATUS_INVALID);

		if (auth->status == SOUP_AUTH_STATUS_PENDING) {
			if (auth->controlling_msg == msg) {
				auth->status = SOUP_AUTH_STATUS_FAILED;
				goto THROW_CANT_AUTHENTICATE;
			}
			else {
				soup_message_requeue (msg);
				return;
			}
		}
		else if (auth->status == SOUP_AUTH_STATUS_FAILED ||
			 auth->status == SOUP_AUTH_STATUS_SUCCESSFUL) {
			/*
			 * We've failed previously, but we'll give it
			 * another go, or we've been successful
			 * previously, but it's not working anymore.
			 *
			 * Invalidate the auth, so it's removed from the
			 * hash and try it again as if we never had it
			 * in the first place.
			 */
			soup_auth_invalidate (auth, ctx);
			soup_message_requeue (msg);
			return;
		}
	}

	if (!auth) {
		auth = soup_auth_new_from_header_list (uri, vals);

		if (!auth) {
			soup_message_set_error_full (
				msg, 
				proxy ? 
			                SOUP_ERROR_CANT_AUTHENTICATE_PROXY : 
			                SOUP_ERROR_CANT_AUTHENTICATE,
				proxy ? 
			                "Unknown authentication scheme "
				        "required by proxy" :
			                "Unknown authentication scheme "
				        "required");
			return;
		}

		auth->status = SOUP_AUTH_STATUS_PENDING;
		auth->controlling_msg = msg;
		soup_message_add_handler (msg, SOUP_HANDLER_PRE_BODY,
					  maybe_validate_auth,
					  GINT_TO_POINTER (proxy));
	}

	/*
	 * Call registered authenticate handler
	 */
	if (!uri->user && soup_auth_fn)
		(*soup_auth_fn) (auth->type,
				 (SoupUri *) uri,
				 auth->realm, 
				 soup_auth_fn_user_data);

	if (!uri->user) {
		soup_auth_free (auth);
		goto THROW_CANT_AUTHENTICATE;
	}

	/*
	 * Initialize with auth data (possibly returned from 
	 * auth callback).
	 */
	soup_auth_initialize (auth, uri);

	if (auth->type == SOUP_AUTH_TYPE_NTLM) {
		SoupAuth *old_auth = msg->connection->auth;

		if (old_auth)
			soup_auth_free (old_auth);
		msg->connection->auth = auth;
	} else
		soup_auth_set_context (auth, ctx);

	soup_message_requeue (msg);

        return;

 THROW_CANT_AUTHENTICATE:
	soup_message_set_error (msg, 
				proxy ? 
			                SOUP_ERROR_CANT_AUTHENTICATE_PROXY : 
			                SOUP_ERROR_CANT_AUTHENTICATE);
}

static void 
redirect_handler (SoupMessage *msg, gpointer user_data)
{
	const gchar *new_loc;

	if (msg->errorclass != SOUP_ERROR_CLASS_REDIRECT || 
	    msg->priv->msg_flags & SOUP_MESSAGE_NO_REDIRECT) return;

	new_loc = soup_message_get_header (msg->response_headers, "Location");

	if (new_loc) {
		const SoupUri *old_uri;
		SoupUri *new_uri;
		SoupContext *new_ctx;

		old_uri = soup_context_get_uri (msg->context);

		new_uri = soup_uri_new (new_loc);
		if (!new_uri) 
			goto INVALID_REDIRECT;

		/* 
		 * Copy auth info from original URI.
		 */
		if (old_uri->user && !new_uri->user)
			soup_uri_set_auth (new_uri,
					   old_uri->user, 
					   old_uri->passwd, 
					   old_uri->authmech);

		new_ctx = soup_context_from_uri (new_uri);

		soup_uri_free (new_uri);

		if (!new_ctx)
			goto INVALID_REDIRECT;

		soup_message_set_context (msg, new_ctx);
		soup_context_unref (new_ctx);

		soup_message_requeue (msg);
	}

	return;

 INVALID_REDIRECT:
	soup_message_set_error_full (msg, 
				     SOUP_ERROR_MALFORMED,
				     "Invalid Redirect URL");
}

typedef enum {
	RESPONSE_HEADER_HANDLER = 1,
	RESPONSE_ERROR_CODE_HANDLER,
	RESPONSE_ERROR_CLASS_HANDLER
} SoupHandlerKind;

typedef struct {
	SoupHandlerType   type;
	SoupCallbackFn    handler_cb;
	gpointer          user_data;

	SoupHandlerKind   kind;
	union {
		guint             errorcode;
		SoupErrorClass    errorclass;
		const gchar      *header;
	} data;
} SoupHandlerData;

static SoupHandlerData global_handlers [] = {
	/* 
	 * Handle redirect response codes 300, 301, 302, 303, and 305.
	 */
	{
		SOUP_HANDLER_PRE_BODY,
		redirect_handler, 
		NULL, 
		RESPONSE_HEADER_HANDLER, 
		{ (guint) "Location" }
	},
	/* 
	 * Handle authorization.
	 */
	{
		SOUP_HANDLER_PRE_BODY,
		(SoupCallbackFn) authorize_handler, 
		GINT_TO_POINTER (FALSE), 
		RESPONSE_ERROR_CODE_HANDLER, 
		{ 401 }
	},
	/* 
	 * Handle proxy authorization.
	 */
	{
		SOUP_HANDLER_PRE_BODY,
		(SoupCallbackFn) authorize_handler, 
		GINT_TO_POINTER (TRUE), 
		RESPONSE_ERROR_CODE_HANDLER, 
		{ 407 }
	},
	{ 0 }
};

static inline void 
run_handler (SoupMessage     *msg, 
	     SoupHandlerType  invoke_type, 
	     SoupHandlerData *data)
{
	if (data->type != invoke_type) return;

	switch (data->kind) {
	case RESPONSE_HEADER_HANDLER:
		if (!soup_message_get_header (msg->response_headers,
					      data->data.header))
			return;
		break;
	case RESPONSE_ERROR_CODE_HANDLER:
		if (msg->errorcode != data->data.errorcode) return;
		break;
	case RESPONSE_ERROR_CLASS_HANDLER:
		if (msg->errorclass != data->data.errorclass) return;
		break;
	default:
		break;
	}

	(*data->handler_cb) (msg, data->user_data);
}

/*
 * Run each handler with matching criteria (first per-message then global
 * handlers). If a handler requeues a message, we stop processing and terminate
 * the current request. 
 *
 * After running all handlers, if there is an error set or the invoke type was
 * post_body, issue the final callback.  
 *
 * FIXME: If the errorcode is changed by a handler, we should restart the
 * processing.  
 */
gboolean
soup_message_run_handlers (SoupMessage *msg, SoupHandlerType invoke_type)
{
	GSList *list;
	SoupHandlerData *data;

	g_return_val_if_fail (msg != NULL, FALSE);

	for (list = msg->priv->content_handlers; list; list = list->next) {
		data = list->data;

		run_handler (msg, invoke_type, data);

		if (msg->status == SOUP_STATUS_QUEUED ||
		    msg->status == SOUP_STATUS_CONNECTING) return TRUE;
	}

	for (data = global_handlers; data->type; data++) {
		run_handler (msg, invoke_type, data);

		if (msg->status == SOUP_STATUS_QUEUED ||
		    msg->status == SOUP_STATUS_CONNECTING) return TRUE;
	}

	/*
	 * Issue final callback if the invoke_type is POST_BODY and the error
	 * class is not INFORMATIONAL. 
	 */
	if (invoke_type == SOUP_HANDLER_POST_BODY && 
	    msg->errorclass != SOUP_ERROR_CLASS_INFORMATIONAL) {
		soup_message_issue_callback (msg);
		return TRUE;
	}

	return FALSE;
}

static void 
add_handler (SoupMessage      *msg,
	     SoupHandlerType   type,
	     SoupCallbackFn    handler_cb,
	     gpointer          user_data,
	     SoupHandlerKind   kind,
	     const gchar      *header,
	     guint             errorcode,
	     guint             errorclass)
{
	SoupHandlerData *data;

	data = g_new0 (SoupHandlerData, 1);
	data->type = type;
	data->handler_cb = handler_cb;
	data->user_data = user_data;
	data->kind = kind;

	switch (kind) {
	case RESPONSE_HEADER_HANDLER:
		data->data.header = header;
		break;
	case RESPONSE_ERROR_CODE_HANDLER:
		data->data.errorcode = errorcode;
		break;
	case RESPONSE_ERROR_CLASS_HANDLER:
		data->data.errorclass = errorclass;
		break;
	default:
		break;
	}

	msg->priv->content_handlers = 
		g_slist_append (msg->priv->content_handlers, data);
}

void 
soup_message_add_header_handler (SoupMessage      *msg,
				 const gchar      *header,
				 SoupHandlerType   type,
				 SoupCallbackFn    handler_cb,
				 gpointer          user_data)
{
	g_return_if_fail (msg != NULL);
	g_return_if_fail (header != NULL);
	g_return_if_fail (handler_cb != NULL);

	add_handler (msg, 
		     type, 
		     handler_cb, 
		     user_data, 
		     RESPONSE_HEADER_HANDLER, 
		     header, 
		     0,
		     0);
}

void 
soup_message_add_error_code_handler (SoupMessage      *msg,
				     guint             errorcode,
				     SoupHandlerType   type,
				     SoupCallbackFn    handler_cb,
				     gpointer          user_data)
{
	g_return_if_fail (msg != NULL);
	g_return_if_fail (errorcode != 0);
	g_return_if_fail (handler_cb != NULL);

	add_handler (msg, 
		     type, 
		     handler_cb, 
		     user_data, 
		     RESPONSE_ERROR_CODE_HANDLER, 
		     NULL, 
		     errorcode,
		     0);
}

void 
soup_message_add_error_class_handler (SoupMessage      *msg,
				      SoupErrorClass    errorclass,
				      SoupHandlerType   type,
				      SoupCallbackFn    handler_cb,
				      gpointer          user_data)
{
	g_return_if_fail (msg != NULL);
	g_return_if_fail (errorclass != 0);
	g_return_if_fail (handler_cb != NULL);

	add_handler (msg, 
		     type, 
		     handler_cb, 
		     user_data, 
		     RESPONSE_ERROR_CLASS_HANDLER, 
		     NULL, 
		     0,
		     errorclass);
}

void 
soup_message_add_handler (SoupMessage      *msg,
			  SoupHandlerType   type,
			  SoupCallbackFn    handler_cb,
			  gpointer          user_data)
{
	g_return_if_fail (msg != NULL);
	g_return_if_fail (handler_cb != NULL);

	add_handler (msg, 
		     type, 
		     handler_cb, 
		     user_data, 
		     0, 
		     NULL, 
		     0,
		     0);
}

void
soup_message_remove_handler (SoupMessage     *msg, 
			     SoupHandlerType  type,
			     SoupCallbackFn   handler_cb,
			     gpointer         user_data)
{
	GSList *iter = msg->priv->content_handlers;

	while (iter) {
		SoupHandlerData *data = iter->data;

		if (data->handler_cb == handler_cb &&
		    data->user_data == user_data &&
		    data->type == type) {
			msg->priv->content_handlers = 
				g_slist_remove_link (
					msg->priv->content_handlers,
					iter);
			g_free (data);
			break;
		}
		
		iter = iter->next;
	}
}

static inline gboolean
ADDED_FLAG (SoupMessage *msg, guint newflags, SoupMessageFlags find)
{
	return ((newflags & find) && !(msg->priv->msg_flags & find));
}

static inline gboolean
REMOVED_FLAG (SoupMessage *msg, guint newflags, SoupMessageFlags find)
{
	return (!(newflags & find) && (msg->priv->msg_flags & find));
}

void
soup_message_set_flags (SoupMessage *msg, guint flags)
{
	g_return_if_fail (msg != NULL);

	msg->priv->msg_flags = flags;
}

guint
soup_message_get_flags (SoupMessage *msg)
{
	g_return_val_if_fail (msg != NULL, 0);

	return msg->priv->msg_flags;
}

void 
soup_message_set_http_version  (SoupMessage *msg, SoupHttpVersion version)
{
	g_return_if_fail (msg != NULL);

	msg->priv->http_version = version;
}

SoupHttpVersion
soup_message_get_http_version (SoupMessage *msg)
{
	g_return_val_if_fail (msg != NULL, SOUP_HTTP_1_0);

	return msg->priv->http_version;
}

void
soup_message_set_context (SoupMessage       *msg,
			  SoupContext       *new_ctx)
{
	g_return_if_fail (msg != NULL);

	if (msg->context)
		soup_context_unref (msg->context);

	if (new_ctx)
		soup_context_ref (new_ctx);

	msg->context = new_ctx;
}

SoupContext *
soup_message_get_context (SoupMessage       *msg)
{
	g_return_val_if_fail (msg != NULL, NULL);

	if (msg->context)
		soup_context_ref (msg->context);

	return msg->context;
}

void
soup_message_set_error (SoupMessage *msg, SoupKnownErrorCode errcode)
{
	g_return_if_fail (msg != NULL);
	g_return_if_fail (errcode != 0);

	g_free ((gchar *) msg->errorphrase);

	msg->errorcode = errcode;
	msg->errorclass = soup_error_get_class (errcode);
	msg->errorphrase = g_strdup (soup_error_get_phrase (errcode));
}

void
soup_message_set_error_full (SoupMessage *msg, 
			     guint        errcode, 
			     const gchar *errphrase)
{
	g_return_if_fail (msg != NULL);
	g_return_if_fail (errcode != 0);
	g_return_if_fail (errphrase != NULL);

	g_free ((gchar *) msg->errorphrase);

	msg->errorcode = errcode;
	msg->errorclass = soup_error_get_class (errcode);
	msg->errorphrase = g_strdup (errphrase);
}

void
soup_message_set_handler_error (SoupMessage *msg, 
				guint        errcode, 
				const gchar *errphrase)
{
	g_return_if_fail (msg != NULL);
	g_return_if_fail (errcode != 0);
	g_return_if_fail (errphrase != NULL);

	g_free ((gchar *) msg->errorphrase);

	msg->errorcode = errcode;
	msg->errorclass = SOUP_ERROR_CLASS_HANDLER;
	msg->errorphrase = g_strdup (errphrase);
}
