/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_ADDRESS_H
#define SOUP_ADDRESS_H

#include <glib-object.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <libsoup/soup-error.h>
#include <libsoup/soup-types.h>

#define SOUP_TYPE_ADDRESS            (soup_address_get_type ())
#define SOUP_ADDRESS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_ADDRESS, SoupAddress))
#define SOUP_ADDRESS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_ADDRESS, SoupAddressClass))
#define SOUP_IS_ADDRESS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_ADDRESS))
#define SOUP_IS_ADDRESS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_ADDRESS))

struct _SoupAddress {
	GObject parent;

	SoupAddressPrivate *priv;
};

struct _SoupAddressClass {
	GObjectClass parent_class;

};

GType soup_address_get_type (void);

SoupAddress     *soup_address_new_from_hostent  (struct hostent   *h);
SoupAddress     *soup_address_new_any           (int               family);
SoupAddress     *soup_address_new_from_sockaddr (struct sockaddr  *sa,
						 guint            *port);

void             soup_address_make_sockaddr     (SoupAddress      *addr,
						 guint             port,
						 struct sockaddr **sa,
						 int              *len);

typedef void   (*SoupAddressGetNameFn)          (SoupAddress      *addr,
						 const char       *name,
						 gpointer          data);
SoupAsyncHandle  soup_address_get_name          (SoupAddress      *addr,
						 SoupAddressGetNameFn,
						 gpointer          data);
void             soup_address_cancel_get_name   (SoupAsyncHandle   id);

const char      *soup_address_check_name        (SoupAddress      *addr);
const char      *soup_address_get_physical      (SoupAddress      *addr);

#endif /* SOUP_ADDRESS_H */
