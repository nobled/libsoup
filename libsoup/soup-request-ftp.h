/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifndef SOUP_REQUEST_FTP_H
#define SOUP_REQUEST_FTP_H 1

#include "soup-request.h"

#define SOUP_TYPE_REQUEST_FTP            (soup_request_ftp_get_type ())
#define SOUP_REQUEST_FTP(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_REQUEST_FTP, SoupRequestFtp))
#define SOUP_REQUEST_FTP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_REQUEST_FTP, SoupRequestFtpInterface))
#define SOUP_IS_REQUEST_FTP(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_REQUEST_FTP))
#define SOUP_IS_REQUEST_FTP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_REQUEST_FTP))
#define SOUP_REQUEST_FTP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SOUP_TYPE_REQUEST_FTP, SoupRequestFtpInterface))

typedef struct _SoupRequestFtpPrivate SoupRequestFtpPrivate;

typedef struct {
	GObject parent;

	SoupRequestFtpPrivate *priv;
} SoupRequestFtp;

typedef struct {
	GObjectClass parent;

} SoupRequestFtpClass;

GType soup_request_ftp_get_type (void);

#endif /* SOUP_REQUEST_FTP_H */
