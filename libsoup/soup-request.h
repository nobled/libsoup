/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008 Red Hat, Inc.
 */

#ifndef SOUP_REQUEST_H
#define SOUP_REQUEST_H 1

#include <libsoup/soup-types.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define SOUP_TYPE_REQUEST            (soup_request_get_type ())
#define SOUP_REQUEST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_REQUEST, SoupRequest))
#define SOUP_REQUEST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_REQUEST, SoupRequestInterface))
#define SOUP_IS_REQUEST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_REQUEST))
#define SOUP_IS_REQUEST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_REQUEST))
#define SOUP_REQUEST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SOUP_TYPE_REQUEST, SoupRequestInterface))

typedef struct {
	GTypeInterface parent;

	/* methods */
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
} SoupRequestInterface;

GType soup_request_get_type (void);

#define SOUP_REQUEST_URI "uri"

SoupURI      *soup_request_get_uri     (SoupRequest          *request);

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

G_END_DECLS

#endif /* SOUP_REQUEST_H */
