/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* url-util.h : utility functions to parse URLs */

/* 
 * Copyright 1999-2002 Ximian, Inc.
 */


#ifndef  SOUP_URI_H
#define  SOUP_URI_H 1

#include <glib.h>

typedef enum {
	SOUP_PROTOCOL_HTTP = 1,
	SOUP_PROTOCOL_HTTPS,
	SOUP_PROTOCOL_SOCKS4,
	SOUP_PROTOCOL_SOCKS5,
	SOUP_PROTOCOL_FTP,
	SOUP_PROTOCOL_FILE,
	SOUP_PROTOCOL_MAILTO
} SoupProtocol;

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
