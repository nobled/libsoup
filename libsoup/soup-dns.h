/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_DNS_H
#define SOUP_DNS_H

#include <glib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <libsoup/soup-error.h>
#include <libsoup/soup-types.h>

typedef void   (*SoupGetHostByFn)          (SoupKnownErrorCode  status,
					    struct hostent     *h,
					    gpointer            user_data);

SoupAsyncHandle  soup_gethostbyname        (const char         *name, 
					    SoupGetHostByFn     func,
					    gpointer            data);

SoupAsyncHandle  soup_gethostbyaddr        (gpointer            addr,
					    int                 family,
					    SoupGetHostByFn     func,
					    gpointer            data);

void             soup_gethostby_cancel     (SoupAsyncHandle     id);

char            *soup_ntop                 (gpointer            addr,
					    int                 family);

#endif /* SOUP_DNS_H */
