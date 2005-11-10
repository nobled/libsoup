/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* url-util.h : utility functions to parse URLs */

/* 
 * Copyright 1999-2002 Ximian, Inc.
 */


#ifndef  SOUP_URI_H
#define  SOUP_URI_H 1

#include <libsoup/soup-types.h>

/**
 * SoupProtocol:
 *
 * #GQuark is used for SoupProtocol so that the protocol of a #SoupUri
 * can be tested quickly.
 **/
typedef GQuark SoupProtocol;

/**
 * SOUP_PROTOCOL_HTTP:
 *
 * This returns the #SoupProtocol value for "http".
 **/
#define SOUP_PROTOCOL_HTTP (g_quark_from_static_string ("http"))

/**
 * SOUP_PROTOCOL_HTTPS:
 *
 * This returns the #SoupProtocol value for "https".
**/
#define SOUP_PROTOCOL_HTTPS (g_quark_from_static_string ("https"))

struct SoupUri {
	SoupProtocol  protocol;

	char         *user;
	char         *passwd;

	char         *host;
	guint         port;

	char         *path;
	char         *query;

	char         *fragment;
};

SoupUri  *soup_uri_new_with_base     (const SoupUri *base,
				      const char    *uri_string);
SoupUri  *soup_uri_new               (const char    *uri_string);

char     *soup_uri_to_string         (const SoupUri *uri, 
				      gboolean       just_path);

SoupUri  *soup_uri_copy              (const SoupUri *uri);
SoupUri  *soup_uri_copy_root         (const SoupUri *uri);

gboolean  soup_uri_equal             (const SoupUri *uri1, 
				      const SoupUri *uri2);

void      soup_uri_free              (SoupUri       *uri);

char     *soup_uri_encode            (const char    *part,
				      const char    *escape_extra);
void      soup_uri_decode            (char          *part);
void      soup_uri_normalize         (char          *part);

gboolean  soup_uri_uses_default_port (const SoupUri *uri);

#endif /*SOUP_URI_H*/
