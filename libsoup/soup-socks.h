/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-socks.h: Asyncronous Callback-based HTTP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_SOCKS_H
#define SOUP_SOCKS_H 1

#include <glib.h>
#include "soup-socket.h"
#include "soup-uri.h"

void soup_socks_proxy_connect (SoupSocket          *socket,
			       SoupUri             *proxy_uri,
			       SoupUri             *dest_uri,
			       SoupSocketConnectFn  cb,
			       gpointer             user_data);

#endif /*SOUP_SOCKS_H*/
