/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_CONNECTION_H
#define SOUP_CONNECTION_H 1

#include <glib.h>
#include <libsoup/soup-error.h>
#include <libsoup/soup-types.h>
#include <libsoup/soup-uri.h>

typedef void (*SoupConnectCallbackFn) (SoupConnection     *conn, 
				       SoupKnownErrorCode  err,
				       gpointer            user_data);

SoupConnectId  soup_connection_new_via_proxy  (SoupUri               *uri,
					       SoupUri               *proxy_uri,
					       SoupConnectCallbackFn  cb,
					       gpointer               user_data);

#define        soup_connection_new(uri, cb, user_data) \
	       soup_connection_new_via_proxy (uri, NULL, cb, user_data);

void           soup_connection_cancel_connect (SoupConnectId          tag);


GIOChannel    *soup_connection_get_iochannel  (SoupConnection        *conn);

void           soup_connection_set_keep_alive (SoupConnection        *conn, 
					       gboolean               keepalive);

gboolean       soup_connection_is_keep_alive  (SoupConnection        *conn);

void           soup_connection_release        (SoupConnection        *conn);

#endif /*SOUP_CONNECTION_H*/
