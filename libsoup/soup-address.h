/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_ADDRESS_H
#define SOUP_ADDRESS_H

#include <sys/types.h>

#include <libsoup/soup-portability.h>
#include <libsoup/soup-types.h>

G_BEGIN_DECLS

#define SOUP_TYPE_ADDRESS            (soup_address_get_type ())
#define SOUP_ADDRESS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_ADDRESS, SoupAddress))
#define SOUP_ADDRESS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_ADDRESS, SoupAddressClass))
#define SOUP_IS_ADDRESS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_ADDRESS))
#define SOUP_IS_ADDRESS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_ADDRESS))
#define SOUP_ADDRESS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_ADDRESS, SoupAddressClass))

struct SoupAddress {
	GObject parent;

};

typedef struct {
	GObjectClass parent_class;

	/* signals */
	void (*dns_result) (SoupAddress *addr, guint status);
} SoupAddressClass;

/* gtk-doc gets confused if there's an #ifdef inside the typedef */
#ifndef AF_INET6
#define AF_INET6 -1
#endif

typedef enum {
	SOUP_ADDRESS_FAMILY_IPV4 = AF_INET,
	SOUP_ADDRESS_FAMILY_IPV6 = AF_INET6
} SoupAddressFamily;

#if AF_INET6 == -1
#undef AF_INET6
#endif

/**
 * SOUP_ADDRESS_ANY_PORT:
 *
 * This can be passed to any #SoupAddress method that expects a port,
 * to indicate that you don't care what port is used.
 **/
#define SOUP_ADDRESS_ANY_PORT 0

/**
 * SoupAddressCallback:
 * @addr: the #SoupAddress that was resolved
 * @status: %SOUP_STATUS_OK or %SOUP_STATUS_CANT_RESOLVE
 * @data: the user data that was passed to
 * soup_address_resolve_async()
 *
 * The callback function passed to soup_address_resolve_async().
 **/
typedef void   (*SoupAddressCallback)            (SoupAddress         *addr,
						  guint                status,
						  gpointer             data);

GType soup_address_get_type (void);

SoupAddress     *soup_address_new                (const char          *name,
						  guint                port);
SoupAddress     *soup_address_new_from_sockaddr  (struct sockaddr     *sa,
						  int                  len);
SoupAddress     *soup_address_new_any            (SoupAddressFamily    family,
						  guint                port);

void             soup_address_resolve_async      (SoupAddress         *addr,
						  SoupAddressCallback  callback,
						  gpointer             user_data);
void             soup_address_resolve_async_full (SoupAddress         *addr,
						  GMainContext        *async_context,
						  SoupAddressCallback  callback,
						  gpointer             user_data);
guint            soup_address_resolve_sync       (SoupAddress         *addr);

const char      *soup_address_get_name           (SoupAddress         *addr);
const char      *soup_address_get_physical       (SoupAddress         *addr);
guint            soup_address_get_port           (SoupAddress         *addr);
struct sockaddr *soup_address_get_sockaddr       (SoupAddress         *addr,
						  int                 *len);

G_END_DECLS

#endif /* SOUP_ADDRESS_H */
