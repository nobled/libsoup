/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_SERVER_CGI_H
#define SOUP_SERVER_CGI_H 1

#include <libsoup/soup-server.h>

#define SOUP_TYPE_SERVER_CGI            (soup_server_get_type ())
#define SOUP_SERVER_CGI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_SERVER_CGI, SoupServerCGI))
#define SOUP_SERVER_CGI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_SERVER_CGI, SoupServerCGIClass))
#define SOUP_IS_SERVER_CGI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_SERVER_CGI))
#define SOUP_IS_SERVER_CGI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_SERVER_CGI))

struct _SoupServerCGI {
	GObject parent;

	SoupServerCGIPrivate *priv;
};

struct _SoupServerCGIClass {
	GObjectClass parent_class;

};

GType       soup_server_cgi_get_type (void);

SoupServer *soup_server_cgi_new      (void);

#endif /* SOUP_SERVER_CGI_H */
