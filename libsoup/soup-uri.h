/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* url-util.h : utility functions to parse URLs */

/* 
 * Copyright 1999-2002 Ximian, Inc.
 */


#ifndef  SOUP_URI_H
#define  SOUP_URI_H 1

#include <glib.h>
#include <libsoup/soup-types.h>

typedef struct {
	SoupProtocol  protocol;

	char         *user;
	char         *authmech;
	char         *passwd;

	char         *host;
	guint         port;

	char         *path;
	char         *query;

	char         *fragment;
} SoupUri;

SoupUri *soup_uri_new_with_base (const SoupUri *base,
				 const char    *uri_string);
SoupUri *soup_uri_new           (const char    *uri_string);

char    *soup_uri_to_string     (const SoupUri *uri, 
				 gboolean       just_path);

SoupUri *soup_uri_copy          (const SoupUri *uri);

gboolean soup_uri_equal         (const SoupUri *uri1, 
				 const SoupUri *uri2);

void     soup_uri_free          (SoupUri       *uri);

void     soup_uri_set_auth      (SoupUri       *uri, 
				 const char    *user, 
				 const char    *passwd, 
				 const char    *authmech);

char    *soup_uri_encode        (const char    *part,
				 gboolean       escape_unsafe,
				 const char    *escape_extra);
void     soup_uri_decode        (char          *part);

#endif /*SOUP_URI_H*/
