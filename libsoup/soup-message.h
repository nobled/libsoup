/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_MESSAGE_H
#define SOUP_MESSAGE_H 1

#include <glib-object.h>
#include <libsoup/soup-error.h>
#include <libsoup/soup-method.h>
#include <libsoup/soup-types.h>
#include <libsoup/soup-uri.h>

#define SOUP_TYPE_MESSAGE            (soup_message_get_type ())
#define SOUP_MESSAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_MESSAGE, SoupMessage))
#define SOUP_MESSAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_MESSAGE, SoupMessageClass))
#define SOUP_IS_MESSAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_MESSAGE))
#define SOUP_IS_MESSAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_MESSAGE))

typedef enum {
	SOUP_STATUS_IDLE = 0,
	SOUP_STATUS_QUEUED,
        SOUP_STATUS_CONNECTING,
	SOUP_STATUS_SENDING_REQUEST,
	SOUP_STATUS_READING_RESPONSE,
	SOUP_STATUS_FINISHED
} SoupTransferStatus;

typedef enum {
	SOUP_BUFFER_SYSTEM_OWNED = 0,
	SOUP_BUFFER_USER_OWNED,
	SOUP_BUFFER_STATIC
} SoupOwnership;

typedef struct {
	SoupOwnership  owner;
	char          *body;
	guint          length;
} SoupDataBuffer;

struct _SoupMessage {
	GObject parent;

	SoupMessagePrivate *priv;

	SoupConnection     *connection;

	const char         *method;

	SoupTransferStatus  status;

	guint               errorcode;
	SoupErrorClass      errorclass;
	const char         *errorphrase;

	SoupDataBuffer      request;
	GHashTable         *request_headers;

	SoupDataBuffer      response;
	GHashTable         *response_headers;
};

struct _SoupMessageClass {
	GObjectClass parent_class;

};

GType soup_message_get_type (void);


#define SOUP_MESSAGE_IS_ERROR(_msg)                            \
        (_msg->errorclass &&                                   \
	 _msg->errorclass != SOUP_ERROR_CLASS_SUCCESS &&       \
         _msg->errorclass != SOUP_ERROR_CLASS_INFORMATIONAL && \
	 _msg->errorclass != SOUP_ERROR_CLASS_UNKNOWN)

typedef void (*SoupCallbackFn) (SoupMessage *req, gpointer user_data);

SoupMessage   *soup_message_new                 (SoupContext       *context,
						 const char        *method);

SoupMessage   *soup_message_new_full            (SoupContext       *context,
						 const char        *method,
						 SoupOwnership      req_owner,
						 char              *req_body,
						 gulong             req_length);

void           soup_message_cancel              (SoupMessage       *req);

SoupErrorClass soup_message_send                (SoupMessage       *msg);

void           soup_message_queue               (SoupMessage       *req, 
						 SoupCallbackFn     callback, 
						 gpointer           user_data);

void           soup_message_requeue             (SoupMessage       *req);

void           soup_message_add_header          (GHashTable        *hash,
						 const char        *name,
						 const char        *value);

const char    *soup_message_get_header          (GHashTable        *hash,
						 const char        *name);

const GSList  *soup_message_get_header_list     (GHashTable        *hash,
						 const char        *name);

void           soup_message_foreach_header      (GHashTable        *hash,
						 GHFunc             func,
						 gpointer           user_data);

void           soup_message_foreach_remove_header (
						 GHashTable        *hash,
						 GHRFunc            func,
						 gpointer           user_data);

void           soup_message_remove_header       (GHashTable        *hash,
						 const char        *name);

void           soup_message_clear_headers       (GHashTable        *hash);

typedef enum {
	SOUP_HTTP_1_0 = 0,
	SOUP_HTTP_1_1 = 1,
} SoupHttpVersion;

void             soup_message_set_http_version  (SoupMessage       *msg,
						 SoupHttpVersion    version);

SoupHttpVersion  soup_message_get_http_version  (SoupMessage       *msg);

void             soup_message_set_context       (SoupMessage       *msg,
						 SoupContext       *new_ctx);

SoupContext     *soup_message_get_context       (SoupMessage       *msg);

const SoupUri   *soup_message_get_uri           (SoupMessage       *msg);

typedef enum {
	/*
	 * SOUP_MESSAGE_NO_REDIRECT: 
	 * Do not follow redirection responses.
	 */
	SOUP_MESSAGE_NO_REDIRECT      = (1 << 1),

	/*
	 * SOUP_MESSAGE_OVERWRITE_CHUNKS:
	 * Downloaded data chunks should not be stored in the response 
	 * data buffer.  Instead only send data to SOUP_HANDLER_BODY_CHUNK 
	 * handlers, then truncate the data buffer.
	 *
	 * Useful when the response is expected to be very large, and 
	 * storage in memory is not desired.
	 */
	SOUP_MESSAGE_OVERWRITE_CHUNKS = (1 << 3)
} SoupMessageFlags;

void           soup_message_set_flags           (SoupMessage        *msg,
						 guint               flags);

guint          soup_message_get_flags           (SoupMessage        *msg);

/*
 * Handler Registration 
 */
typedef enum {
	SOUP_HANDLER_PRE_BODY = 1,
	SOUP_HANDLER_BODY_CHUNK,
	SOUP_HANDLER_POST_BODY
} SoupHandlerType;

void           soup_message_add_handler         (SoupMessage       *msg,
						 SoupHandlerType    type,
						 SoupCallbackFn     handler_cb,
						 gpointer           user_data);

void           soup_message_add_header_handler  (SoupMessage       *msg,
						 const char        *header,
						 SoupHandlerType    type,
						 SoupCallbackFn     handler_cb,
						 gpointer           user_data);

void           soup_message_add_error_code_handler (
						 SoupMessage       *msg,
						 guint              errorcode,
						 SoupHandlerType    type,
						 SoupCallbackFn     handler_cb,
						 gpointer           user_data);

void           soup_message_add_error_class_handler (
						 SoupMessage       *msg,
						 SoupErrorClass     errorclass,
						 SoupHandlerType    type,
						 SoupCallbackFn     handler_cb,
						 gpointer           user_data);

void           soup_message_remove_handler      (SoupMessage       *msg, 
						 SoupHandlerType    type,
						 SoupCallbackFn     handler_cb,
						 gpointer           user_data);

/*
 * Error Setting (for use by Handlers)
 */
void           soup_message_set_error           (SoupMessage       *msg, 
						 SoupKnownErrorCode errcode);

void           soup_message_set_error_full      (SoupMessage       *msg, 
						 guint              errcode, 
						 const char        *errphrase);

void           soup_message_set_handler_error   (SoupMessage       *msg, 
						 guint              errcode, 
						 const char        *errphrase);

#endif /*SOUP_MESSAGE_H*/
