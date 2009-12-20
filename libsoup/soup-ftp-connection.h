/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef SOUP_FTP_CONNECTION_H
#define SOUP_FTP_CONNECTION_H 1

#include <libsoup/soup-types.h>
#include <gio/gio.h>

#define SOUP_TYPE_FTP_CONNECTION            (soup_ftp_connection_get_type ())
#define SOUP_FTP_CONNECTION(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_FTP_CONNECTION, SoupFTPConnection))
#define SOUP_FTP_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_FTP_CONNECTION, SoupFTPConnectionInterface))
#define SOUP_IS_FTP_CONNECTION(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_FTP_CONNECTION))
#define SOUP_IS_FTP_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_FTP_CONNECTION))
#define SOUP_FTP_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SOUP_TYPE_FTP_CONNECTION, SoupFTPConnectionInterface))

typedef struct _SoupFTPConnectionPrivate SoupFTPConnectionPrivate;

typedef struct {
	GObject parent;

	SoupFTPConnectionPrivate *priv;
} SoupFTPConnection;

typedef struct {
	GObjectClass parent;

} SoupFTPConnectionClass;

GType              soup_ftp_connection_get_type (void);
SoupFTPConnection *soup_ftp_connection_new      (void);

GInputStream *soup_ftp_connection_load_uri        (SoupFTPConnection   *ftp,
						   SoupURI             *uri,
						   GCancellable        *cancellable,
						   GError             **error);
void          soup_ftp_connection_load_uri_async  (SoupFTPConnection   *ftp,
						   SoupURI             *uri,
						   GCancellable        *cancellable,
						   GAsyncReadyCallback  callback,
						   gpointer             user_data);
GInputStream *soup_ftp_connection_load_uri_finish (SoupFTPConnection   *ftp,
						   GAsyncResult        *result,
						   GError             **error);

#define SOUP_FTP_ERROR (soup_ftp_error_quark ())
GQuark soup_ftp_error_quark (void);

#define SOUP_FTP_CONNECTION_ERROR (soup_ftp_connection_error_quark ())
GQuark soup_ftp_connection_error_quark (void);

#endif /* SOUP_FTP_CONNECTION_H */
