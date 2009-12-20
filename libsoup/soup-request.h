/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef SOUP_REQUEST_H
#define SOUP_REQUEST_H 1

#include <libsoup/soup-types.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define SOUP_TYPE_REQUEST            (soup_request_get_type ())
#define SOUP_REQUEST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_REQUEST, SoupRequest))
#define SOUP_REQUEST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_REQUEST, SoupRequestClass))
#define SOUP_IS_REQUEST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_REQUEST))
#define SOUP_IS_REQUEST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_REQUEST))
#define SOUP_REQUEST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_REQUEST, SoupRequestClass))

typedef struct _SoupRequestPrivate SoupRequestPrivate;
typedef struct _SoupRequestClass   SoupRequestClass;

struct _SoupRequest {
	GObject parent;

	SoupRequestPrivate *priv;
};

struct _SoupRequestClass {
	GObjectClass parent;

	gboolean       (*check_uri)   (SoupRequest          *req_base,
				       SoupURI              *uri,
				       GError              **error);

	GInputStream * (*send)        (SoupRequest          *request,
				       GCancellable         *cancellable,
				       GError              **error);
	void           (*send_async)  (SoupRequest          *request,
				       GCancellable         *cancellable,
				       GAsyncReadyCallback   callback,
				       gpointer              user_data);
	GInputStream * (*send_finish) (SoupRequest          *request,
				       GAsyncResult         *result,
				       GError              **error);
};

GType soup_request_get_type (void);

#define SOUP_REQUEST_URI     "uri"
#define SOUP_REQUEST_SESSION "session"

GInputStream *soup_request_send        (SoupRequest          *request,
					GCancellable         *cancellable,
					GError              **error);
void          soup_request_send_async  (SoupRequest          *request,
					GCancellable         *cancellable,
					GAsyncReadyCallback   callback,
					gpointer              user_data);
GInputStream *soup_request_send_finish (SoupRequest          *request,
					GAsyncResult         *result,
					GError              **error);

SoupURI      *soup_request_get_uri     (SoupRequest          *request);
SoupSession  *soup_request_get_session (SoupRequest          *request);

G_END_DECLS

#endif /* SOUP_REQUEST_H */
