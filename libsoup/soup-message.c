/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-message.c: an HTTP message
 *
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "soup-connection.h"
#include "soup-error.h"
#include "soup-headers.h"
#include "soup-message.h"
#include "soup-misc.h"
#include "soup-context.h"
#include "soup-private.h"
#include "soup-proxy-connection.h"
#include "soup-queue.h"
#include "soup-transfer.h"

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

static void
init (GObject *object)
{
	SoupMessage *msg = SOUP_MESSAGE (object);

	msg->priv = g_new0 (SoupMessagePrivate, 1);
	msg->priv->http_version = SOUP_HTTP_1_1;

	msg->status = SOUP_STATUS_IDLE;

	msg->request_headers  = g_hash_table_new (soup_str_case_hash, 
						  soup_str_case_equal);
	msg->response_headers = g_hash_table_new (soup_str_case_hash, 
						  soup_str_case_equal);
}

static void
finalize (GObject *object)
{
	SoupMessage *msg = SOUP_MESSAGE (object);

	soup_message_cleanup (msg);

	if (msg->priv->context)
		soup_context_unref (msg->priv->context);

	if (msg->request.owner == SOUP_BUFFER_SYSTEM_OWNED)
		g_free (msg->request.body);
	if (msg->response.owner == SOUP_BUFFER_SYSTEM_OWNED)
		g_free (msg->response.body);

	soup_message_clear_headers (msg->request_headers);
	g_hash_table_destroy (msg->request_headers);

	soup_message_clear_headers (msg->response_headers);
	g_hash_table_destroy (msg->response_headers);

	g_slist_foreach (msg->priv->content_handlers, (GFunc) g_free, NULL);
	g_slist_free (msg->priv->content_handlers);

	g_free ((char *) msg->errorphrase);

	g_free (msg->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_message, SoupMessage, class_init, init, PARENT_TYPE)


/**
 * soup_message_new:
 * @uri: a #SoupUri for the destination endpoint.
 * @method: a (static) string which will be used as the HTTP method
 * for the created request
 * 
 * Creates a new empty #SoupMessage, which will connect to @uri
 * 
 * The new message has a status of %SOUP_STATUS_IDLE.
 *
 * Return value: the new #SoupMessage.
 */
SoupMessage *
soup_message_new (const SoupUri *uri, const char *method) 
{
	SoupMessage *msg;

	msg = g_object_new (SOUP_TYPE_MESSAGE, NULL);

	msg->method = method;
	soup_message_set_uri (msg, uri);

	return msg;
}

/**
 * soup_message_new_full:
 * @uri: a #SoupUri for the destination endpoint.
 * @method: a string which will be used as the HTTP method for the
 * created request
 * @req_owner: the #SoupOwnership of the passed data buffer.
 * @req_body: a data buffer containing the body of the message request.
 * @req_length: the byte length of @req_body.
 * 
 * Creates a new #SoupMessage, which will connect to @uri. The
 * request data buffer will be filled from @req_owner, @req_body, and
 * @req_length respectively.
 *
 * The new message has a status of %SOUP_STATUS_IDLE.
 *
 * Return value: the new #SoupMessage.
 */
SoupMessage *
soup_message_new_full (const SoupUri *uri,
		       const char    *method,
		       SoupOwnership  req_owner,
		       char          *req_body,
		       gulong         req_length)
{
	SoupMessage *msg = soup_message_new (uri, method);

	msg->request.owner = req_owner;
	msg->request.body = req_body;
	msg->request.length = req_length;

	return msg;
}

static void
release_connection (const SoupDataBuffer *data,
		    gpointer              user_data)
{
	SoupConnection *conn = user_data;

	soup_connection_set_in_use (conn, FALSE);
}

static void
close_connection (gboolean headers_done, gpointer user_data)
{
	SoupConnection *conn = user_data;

	soup_connection_close (conn);
}

/**
 * soup_message_cleanup:
 * @msg: a #SoupMessage.
 * 
 * Frees any temporary resources created in the processing of @msg.
 * Also releases the active connection, if one exists. Request and
 * response data buffers are left intact.
 */
void 
soup_message_cleanup (SoupMessage *msg)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	if (msg->connection &&
	    msg->priv->read_tag &&
	    msg->status == SOUP_STATUS_READING_RESPONSE) {
		soup_transfer_read_set_callbacks (msg->priv->read_tag,
						  NULL,
						  NULL,
						  release_connection,
						  close_connection,
						  msg->connection);
		msg->priv->read_tag = 0;
		msg->connection = NULL;
		/*
		 * The buffer doesn't belong to us until the message is
		 * finished.
		 */
		msg->response.owner = SOUP_BUFFER_STATIC;
	}

	if (msg->priv->read_tag) {
		soup_transfer_read_cancel (msg->priv->read_tag);
		msg->priv->read_tag = 0;
	}

	if (msg->priv->write_tag) {
		soup_transfer_write_cancel (msg->priv->write_tag);
		msg->priv->write_tag = 0;
	}

	if (msg->priv->connect_tag) {
		soup_context_cancel_connect (msg->priv->connect_tag);
		msg->priv->connect_tag = NULL;
	}

	if (msg->connection) {
		soup_connection_set_in_use (msg->connection, FALSE);
		msg->connection = NULL;
	}

	soup_queue_remove_request (msg);
}

/**
 * soup_message_issue_callback:
 * @msg: a #SoupMessage currently being processed.
 * @error: a #SoupErrorCode to be passed to @msg's completion callback.
 * 
 * Finalizes the message request, by first freeing any temporary
 * resources, then issuing the callback function pointer passed in
 * soup_message_queue(). If, after returning from the callback, the
 * message has not been requeued, @msg is destroyed using
 * %soup_message_free.
 */
void
soup_message_issue_callback (SoupMessage *msg)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	/* 
	 * Make sure we don't have some icky recursion if the callback 
	 * runs the main loop, and the connection has some data or error 
	 * which causes the callback to be run again.
	 */
	soup_message_cleanup (msg);

	if (msg->priv->callback) {
		(*msg->priv->callback) (msg, msg->priv->user_data);

		if (msg->status != SOUP_STATUS_QUEUED)
			g_object_unref (msg);
	}
}

/**
 * soup_message_cancel:
 * @msg: a #SoupMessage currently being processed.
 * 
 * Cancel a running message, and issue completion callback with a
 * #SoupTransferStatus of %SOUP_ERROR_CANCELLED. If not requeued by
 * the completion callback, the @msg will be destroyed.
 */
void 
soup_message_cancel (SoupMessage *msg) 
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	soup_message_set_error (msg, SOUP_ERROR_CANCELLED);

	/* Kill the connection as a safety measure */
	if (msg->connection) {
		soup_connection_close (msg->connection);
		msg->connection = NULL;
	}

	soup_message_issue_callback (msg);
}

static void
free_one_header (gpointer header, gpointer data)
{
	g_free (header);
}

static gboolean
foreach_free_header_list (gpointer name, gpointer vals, gpointer data)
{
	g_free (name);
	g_slist_foreach (vals, free_one_header, NULL);
	g_slist_free (vals);

	return TRUE;
}

void
soup_message_clear_headers (GHashTable *hash)
{
	g_return_if_fail (hash != NULL);

	g_hash_table_foreach_remove (hash, foreach_free_header_list, NULL);
}

void 
soup_message_remove_header (GHashTable *hash, const char *name)
{
	gpointer stored_key, vals;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (name != NULL || name [0] != '\0');

	if (g_hash_table_lookup_extended (hash, name, &stored_key, &vals)) {
		g_hash_table_remove (hash, name);
		foreach_free_header_list (stored_key, vals, NULL);
	}
}

void 
soup_message_add_header (GHashTable *hash, const char *name, const char *value)
{
	GSList *old_value;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (name != NULL || name [0] != '\0');
	g_return_if_fail (value != NULL);

	old_value = g_hash_table_lookup (hash, name);

	if (old_value)
		g_slist_append (old_value, g_strdup (value));
	else {
		g_hash_table_insert (hash, g_strdup (name), 
				     g_slist_append (NULL, g_strdup (value)));
	}
}

/**
 * soup_message_get_header:
 * @msg: a #SoupMessage.
 * @name: header name.
 * 
 * Lookup the first transport header with a key equal to @name.
 *
 * Return value: the header's value or %NULL if not found.
 */
const char *
soup_message_get_header (GHashTable *hash, const char *name)
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
 * @msg: a #SoupMessage.
 * @name: header name.
 * 
 * Lookup the all transport request headers with a key equal to @name.
 *
 * Return value: a const pointer to a #GSList of header values or
 * %NULL if not found.
 */
const GSList *
soup_message_get_header_list (GHashTable  *hash, const char *name)
{
	g_return_val_if_fail (hash != NULL, NULL);
	g_return_val_if_fail (name != NULL || name [0] != '\0', NULL);	

	return g_hash_table_lookup (hash, name);
}

struct foreach_header_data {
	GHFunc   func;
	gpointer user_data;
};

static void 
foreach_value_in_list (gpointer name, gpointer value, gpointer user_data)
{
	GSList *vals = value;
	struct foreach_header_data *data = user_data;

	while (vals) {
		(*data->func) (name, vals->data, data->user_data);
		vals = vals->next;
	}
}

void
soup_message_foreach_header (GHashTable *hash, GHFunc func, gpointer user_data)
{
	struct foreach_header_data data;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (func != NULL);

	data.func = func;
	data.user_data = user_data;

	g_hash_table_foreach (hash, foreach_value_in_list, &data);
}

/**
 * soup_message_queue:
 * @msg: a #SoupMessage.
 * @callback: a #SoupCallbackFn which will be called after the message
 * completes or when an unrecoverable error occurs.
 * @user_data: a pointer passed to @callback.
 * 
 * Queues the message @msg for sending. All messages are processed
 * while the glib main loop runs. If this #SoupMessage has been
 * processed before, any resources related to the time it was last
 * sent are freed.
 *
 * If the response #SoupDataBuffer has an owner of
 * %SOUP_BUFFER_USER_OWNED, the message will not be queued, and
 * @callback will be called with a #SoupErrorCode of
 * %SOUP_ERROR_CANCELLED.
 *
 * Upon message completetion, the callback specified in @callback will
 * be invoked. If after returning from this callback the message has
 * not been requeued using %soup_message_queue, %soup_message_free
 * will be called on @msg.
 */
void 
soup_message_queue (SoupMessage    *msg,
		    SoupCallbackFn  callback, 
		    gpointer        user_data)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	msg->priv->callback = callback;
	msg->priv->user_data = user_data;
	soup_queue_message (msg);
}

/**
 * soup_message_requeue:
 * @msg: a #SoupMessage
 *
 * This causes @msg to be placed back on the queue to be attempted again.
 **/
void
soup_message_requeue (SoupMessage *msg)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	soup_queue_message (msg);
}

/**
 * soup_message_send:
 * @msg: a #SoupMessage.
 * 
 * Syncronously send @msg. This call will not return until the
 * transfer is finished successfully or there is an unrecoverable
 * error.
 *
 * @msg is not free'd upon return.
 *
 * Return value: the #SoupErrorClass of the error encountered while
 * sending or reading the response.
 */
SoupErrorClass
soup_message_send (SoupMessage *msg)
{
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), SOUP_ERROR_CLASS_TRANSPORT);

	soup_message_queue (msg, NULL, NULL);

	while (1) {
		g_main_iteration (TRUE); 

		if (msg->status == SOUP_STATUS_FINISHED || 
		    SOUP_ERROR_IS_TRANSPORT (msg->errorcode))
			break;

		/* Quit if soup_shutdown has been called */ 
		if (!soup_initialized)
			return SOUP_ERROR_CLASS_TRANSPORT;
	}

	return msg->errorclass;
}

static void 
redirect_handler (SoupMessage *msg, gpointer user_data)
{
	const char *new_loc;

	if (msg->errorclass != SOUP_ERROR_CLASS_REDIRECT || 
	    msg->priv->msg_flags & SOUP_MESSAGE_NO_REDIRECT) return;

	new_loc = soup_message_get_header (msg->response_headers, "Location");

	if (new_loc) {
		const SoupUri *old_uri;
		SoupUri *new_uri;

		old_uri = soup_message_get_uri (msg);

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


		soup_message_set_uri (msg, new_uri);
		soup_uri_free (new_uri);

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
		const char       *header;
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

	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), FALSE);

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
	     const char       *header,
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
				 const char       *header,
				 SoupHandlerType   type,
				 SoupCallbackFn    handler_cb,
				 gpointer          user_data)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));
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
	g_return_if_fail (SOUP_IS_MESSAGE (msg));
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
	g_return_if_fail (SOUP_IS_MESSAGE (msg));
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
	g_return_if_fail (SOUP_IS_MESSAGE (msg));
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

void
soup_message_set_flags (SoupMessage *msg, guint flags)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	msg->priv->msg_flags = flags;
}

guint
soup_message_get_flags (SoupMessage *msg)
{
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), 0);

	return msg->priv->msg_flags;
}

void 
soup_message_set_http_version  (SoupMessage *msg, SoupHttpVersion version)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	msg->priv->http_version = version;
}

SoupHttpVersion
soup_message_get_http_version (SoupMessage *msg)
{
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), SOUP_HTTP_1_0);

	return msg->priv->http_version;
}

void
soup_message_set_uri (SoupMessage       *msg,
		      const SoupUri     *new_uri)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));

	if (msg->priv->context)
		soup_context_unref (msg->priv->context);

	if (new_uri)
		msg->priv->context = soup_context_from_uri (new_uri);
	else
		msg->priv->context = NULL;
}

const SoupUri *
soup_message_get_uri (SoupMessage *msg)
{
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), NULL);
	g_return_val_if_fail (msg->priv->context != NULL, NULL);

	return soup_context_get_uri (msg->priv->context);
}

void
soup_message_set_error (SoupMessage *msg, SoupKnownErrorCode errcode)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));
	g_return_if_fail (errcode != 0);

	g_free ((char *) msg->errorphrase);

	msg->errorcode = errcode;
	msg->errorclass = soup_error_get_class (errcode);
	msg->errorphrase = g_strdup (soup_error_get_phrase (errcode));
}

void
soup_message_set_error_full (SoupMessage *msg, 
			     guint        errcode, 
			     const char  *errphrase)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));
	g_return_if_fail (errcode != 0);
	g_return_if_fail (errphrase != NULL);

	g_free ((char *) msg->errorphrase);

	msg->errorcode = errcode;
	msg->errorclass = soup_error_get_class (errcode);
	msg->errorphrase = g_strdup (errphrase);
}

void
soup_message_set_handler_error (SoupMessage *msg, 
				guint        errcode, 
				const char  *errphrase)
{
	g_return_if_fail (SOUP_IS_MESSAGE (msg));
	g_return_if_fail (errcode != 0);
	g_return_if_fail (errphrase != NULL);

	g_free ((char *) msg->errorphrase);

	msg->errorcode = errcode;
	msg->errorclass = SOUP_ERROR_CLASS_HANDLER;
	msg->errorphrase = g_strdup (errphrase);
}


static void 
soup_queue_error_cb (gboolean body_started, gpointer user_data)
{
	SoupMessage *req = user_data;

	soup_connection_close (req->connection);
	req->connection = NULL;

	req->priv->read_tag = 0;
	req->priv->write_tag = 0;

	switch (req->status) {
	case SOUP_STATUS_IDLE:
	case SOUP_STATUS_QUEUED:
	case SOUP_STATUS_FINISHED:
		break;

	case SOUP_STATUS_CONNECTING:
		soup_message_set_error (req, SOUP_ERROR_CANT_CONNECT);
		soup_message_issue_callback (req);
		break;

	case SOUP_STATUS_READING_RESPONSE:
	case SOUP_STATUS_SENDING_REQUEST:
		if (!body_started) {
			/*
			 * This can easily happen if we are using the OpenSSL
			 * out-of-process proxy and we couldn't establish an
			 * SSL connection.
			 */
			if (req->priv->retries >= 3) {
				soup_message_set_error (
					req,
					SOUP_ERROR_CANT_CONNECT);
				soup_message_issue_callback (req);
			} else {
				req->priv->retries++;
				soup_message_requeue (req);
			}
		} else {
			soup_message_set_error (req, SOUP_ERROR_IO);
			soup_message_issue_callback (req);
		}
		break;

	default:
		soup_message_set_error (req, SOUP_ERROR_IO);
		soup_message_issue_callback (req);
		break;
	}
}

static SoupTransferDone
soup_queue_read_headers_cb (const GString        *headers,
                            SoupTransferEncoding *encoding,
			    gint                 *content_len,
			    gpointer              user_data)
{
	SoupMessage *req = user_data;
	const gchar *length, *enc;
	SoupHttpVersion version;
	GHashTable *resp_hdrs;
	SoupMethodId meth_id;

	if (!soup_headers_parse_response (headers->str, 
					  headers->len, 
					  req->response_headers,
					  &version,
					  &req->errorcode,
					  (gchar **) &req->errorphrase)) {
		soup_message_set_error_full (req, 
					     SOUP_ERROR_MALFORMED,
					     "Unable to parse response "
					     "headers");
		goto THROW_MALFORMED_HEADER;
	}

	meth_id   = soup_method_get_id (req->method);
	resp_hdrs = req->response_headers;

	req->errorclass = soup_error_get_class (req->errorcode);

	/* 
	 * Special case zero body handling for:
	 *   - HEAD requests (where content-length must be ignored) 
	 *   - CONNECT requests (no body expected) 
	 *   - No Content (204) responses (no message-body allowed)
	 *   - Reset Content (205) responses (no entity allowed)
	 *   - Not Modified (304) responses (no message-body allowed)
	 *   - 1xx Informational responses (where no body is allowed)
	 */
	if (meth_id == SOUP_METHOD_ID_HEAD ||
	    meth_id == SOUP_METHOD_ID_CONNECT ||
	    req->errorcode  == SOUP_ERROR_NO_CONTENT || 
	    req->errorcode  == SOUP_ERROR_RESET_CONTENT || 
	    req->errorcode  == SOUP_ERROR_NOT_MODIFIED || 
	    req->errorclass == SOUP_ERROR_CLASS_INFORMATIONAL) {
		*encoding = SOUP_TRANSFER_CONTENT_LENGTH;
		*content_len = 0;
		goto SUCCESS_CONTINUE;
	}

	/* 
	 * Handle Chunked encoding.  Prefer Chunked over a Content-Length to
	 * support broken Traffic-Server proxies that supply both.  
	 */
	enc = soup_message_get_header (resp_hdrs, "Transfer-Encoding");
	if (enc) {
		if (g_strcasecmp (enc, "chunked") == 0)
			*encoding = SOUP_TRANSFER_CHUNKED;
		else {
			soup_message_set_error_full (
				req, 
				SOUP_ERROR_MALFORMED,
				"Unknown Response Encoding");
			goto THROW_MALFORMED_HEADER;
		}
		goto SUCCESS_CONTINUE;
	}

	/* 
	 * Handle Content-Length encoding 
	 */
	length = soup_message_get_header (resp_hdrs, "Content-Length");
	if (length) {
		*encoding = SOUP_TRANSFER_CONTENT_LENGTH;
		*content_len = atoi (length);
		if (*content_len < 0) {
			soup_message_set_error_full (req, 
						     SOUP_ERROR_MALFORMED,
						     "Invalid Content-Length");
			goto THROW_MALFORMED_HEADER;
		} 
		goto SUCCESS_CONTINUE;
	}

 SUCCESS_CONTINUE:
	soup_message_run_handlers (req, SOUP_HANDLER_PRE_BODY);
	return SOUP_TRANSFER_CONTINUE;

 THROW_MALFORMED_HEADER:
	soup_connection_close (req->connection);
	req->connection = NULL;
	soup_message_issue_callback (req);
	return SOUP_TRANSFER_END;
}

static SoupTransferDone
soup_queue_read_chunk_cb (const SoupDataBuffer *data,
			  gpointer              user_data)
{
	SoupMessage *req = user_data;

	req->response.owner = data->owner;
	req->response.length = data->length;
	req->response.body = data->body;

	soup_message_run_handlers (req, SOUP_HANDLER_BODY_CHUNK);

	return SOUP_TRANSFER_CONTINUE;
}

static void
soup_queue_read_done_cb (const SoupDataBuffer *data,
			 gpointer              user_data)
{
	SoupMessage *req = user_data;
	const char *connection;

	req->response.owner = data->owner;
	req->response.length = data->length;
	req->response.body = data->body;

	if (req->errorclass == SOUP_ERROR_CLASS_INFORMATIONAL) {
		GIOChannel *channel;
		gboolean overwrt;

		channel = soup_connection_get_iochannel (req->connection);
		overwrt = req->priv->msg_flags & SOUP_MESSAGE_OVERWRITE_CHUNKS;

		req->priv->read_tag = 
			soup_transfer_read (channel,
					    overwrt,
					    soup_queue_read_headers_cb,
					    soup_queue_read_chunk_cb,
					    soup_queue_read_done_cb,
					    soup_queue_error_cb,
					    req);
	} 
	else {
		req->status = SOUP_STATUS_FINISHED;
		req->priv->read_tag = 0;

		connection = soup_message_get_header (req->response_headers,
						      "Connection");
		if ((connection && !g_strcasecmp (connection, "close")) ||
		    (req->priv->http_version == SOUP_HTTP_1_0)) {
			soup_connection_close (req->connection);
			req->connection = NULL;
		}
	}

	soup_message_run_handlers (req, SOUP_HANDLER_POST_BODY);
}

struct SoupUsedHeaders {
	gboolean host;
	gboolean user_agent;
	gboolean content_type;
	gboolean connection;

	GString *out;
};

static void 
soup_check_used_headers (gchar  *key, 
			 GSList *vals, 
			 struct SoupUsedHeaders *hdrs)
{
	switch (toupper (key [0])) {
	case 'H':
		if (!g_strcasecmp (key+1, "ost")) 
			hdrs->host = TRUE;
		break;
	case 'U':
		if (!g_strcasecmp (key+1, "ser-Agent")) 
			hdrs->user_agent = TRUE;
		break;
	case 'C':
		if (!g_strcasecmp (key+1, "onnection")) 
			hdrs->connection = TRUE;
		else if (!g_strcasecmp (key+1, "ontent-Type"))
			hdrs->content_type = TRUE;
		else if (!g_strcasecmp (key+1, "ontent-Length")) {
			g_warning ("Content-Length set as custom request "
				   "header is not allowed.");
			return;
		}
		break;
	}

	while (vals) {
		g_string_sprintfa (hdrs->out, 
				   "%s: %s\r\n", 
				   key, 
				   (gchar *) vals->data);
		vals = vals->next;
	}
}

static GString *
soup_get_request_header (SoupMessage *req, gboolean via_proxy)
{
	GString *header;
	gchar *uri;
	const SoupUri *suri;
	struct SoupUsedHeaders hdrs = {
		FALSE, 
		FALSE, 
		FALSE, 
		FALSE, 
		NULL
	};

	header = hdrs.out = g_string_new (NULL);
	suri = soup_message_get_uri (req);

	if (!g_strcasecmp (req->method, "CONNECT")) 
		/*
		 * CONNECT URI is hostname:port for tunnel destination
		 */
		uri = g_strdup_printf ("%s:%d", suri->host, suri->port);
	else {
		/*
		 * Proxy expects full URI to destination, otherwise
		 * just path.
		 */
		uri = soup_uri_to_string (suri, !via_proxy);
	}

	g_string_sprintfa (header,
			   req->priv->http_version == SOUP_HTTP_1_1 ? 
			           "%s %s HTTP/1.1\r\n" : 
			           "%s %s HTTP/1.0\r\n",
			   req->method,
			   uri);
	g_free (uri);

	/*
	 * FIXME: Add a 411 "Length Required" response code handler here?
	 */
	if (req->request.length > 0) {
		g_string_sprintfa (header,
				   "Content-Length: %d\r\n",
				   req->request.length);
	}

	g_hash_table_foreach (req->request_headers, 
			      (GHFunc) soup_check_used_headers,
			      &hdrs);

	/* 
	 * If we specify an absoluteURI in the request line, the Host header
	 * MUST be ignored by the proxy.  
	 */
	g_string_sprintfa (header, 
			   "%s%s%s%s%s%s%s",
			   hdrs.host ? "" : "Host: ",
			   hdrs.host ? "" : suri->host,
			   hdrs.host ? "" : "\r\n",
			   hdrs.content_type ? "" : "Content-Type: text/xml; ",
			   hdrs.content_type ? "" : "charset=utf-8\r\n",
			   hdrs.connection ? "" : "Connection: keep-alive\r\n",
			   hdrs.user_agent ? 
			           "" : 
			           "User-Agent: Soup/" VERSION "\r\n");

	g_string_append (header, "\r\n");

	return header;
}

static void 
soup_queue_write_done_cb (gpointer user_data)
{
	SoupMessage *req = user_data;

	req->priv->write_tag = 0;
	req->status = SOUP_STATUS_READING_RESPONSE;
}

void
soup_message_send_request (SoupMessage *req)
{
	GIOChannel *channel;
	gboolean proxy, overwrt; 

	channel = soup_connection_get_iochannel (req->connection);
	if (!channel) {
		soup_message_set_error (req, SOUP_ERROR_CANT_CONNECT);
		soup_message_issue_callback (req);
		return;
	}

	proxy = SOUP_IS_PROXY_CONNECTION (req->connection);

	req->priv->write_tag = 
		soup_transfer_write_simple (channel,
					    soup_get_request_header (req, proxy),
					    &req->request,
					    soup_queue_write_done_cb,
					    soup_queue_error_cb,
					    req);

	overwrt = req->priv->msg_flags & SOUP_MESSAGE_OVERWRITE_CHUNKS;

	req->priv->read_tag = 
		soup_transfer_read (channel,
				    overwrt,
				    soup_queue_read_headers_cb,
				    soup_queue_read_chunk_cb,
				    soup_queue_read_done_cb,
				    soup_queue_error_cb,
				    req);

	req->status = SOUP_STATUS_SENDING_REQUEST;
}

