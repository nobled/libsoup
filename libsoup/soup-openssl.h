/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-openssl.h: Asyncronous Callback-based SOAP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2001-2002, Ximian, Inc.
 */

#ifndef SOUP_OPENSSL_H
#define SOUP_OPENSSL_H 1

#include <glib.h>
#include <libsoup/soup-misc.h>

GIOChannel *soup_openssl_get_iochannel       (GIOChannel *sock);

gboolean    soup_openssl_init                (gboolean server_mode);

#endif /* SOUP_OPENSSL_H */
