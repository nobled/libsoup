/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors:
 *      David Helder  (dhelder@umich.edu)
 *      Alex Graveley (alex@ximian.com)
 * 
 * Original code compliments of David Helder's GNET Networking Library.
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_ADDRESS_H
#define SOUP_ADDRESS_H

#include <glib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <libsoup/soup-error.h>
#include <libsoup/soup-types.h>

typedef gpointer SoupAddressNewId;

typedef void (*SoupAddressNewFn) (SoupAddress        *inetaddr, 
				  SoupKnownErrorCode  status, 
				  gpointer            user_data);

SoupAddressNewId     soup_address_new                (const gchar*       name, 
						      SoupAddressNewFn   func, 
						      gpointer           data);

void                 soup_address_new_cancel         (SoupAddressNewId   id);

SoupAddress         *soup_address_new_sync           (const gchar       *name);

SoupAddress         *soup_address_ipv4_any           (void);
SoupAddress         *soup_address_ipv6_any           (void);

void                 soup_address_ref                (SoupAddress*       ia);

void                 soup_address_unref              (SoupAddress*       ia);

SoupAddress *        soup_address_copy               (SoupAddress*       ia);


typedef gpointer SoupAddressGetNameId;

typedef void (*SoupAddressGetNameFn) (SoupAddress        *inetaddr, 
				      SoupKnownErrorCode  status, 
				      const gchar        *name,
				      gpointer           user_data);

SoupAddressGetNameId soup_address_get_name           (SoupAddress*         ia, 
						      SoupAddressGetNameFn func,
						      gpointer             data);

void                 soup_address_get_name_cancel    (SoupAddressGetNameId id);

const gchar         *soup_address_get_name_sync      (SoupAddress *addr);

gchar*               soup_address_get_canonical_name (SoupAddress*         ia);


SoupAddress         *soup_address_new_from_sockaddr  (struct sockaddr   *sa,
						      guint             *port);

void                 soup_address_make_sockaddr      (SoupAddress       *ia,
						      guint              port,
						      struct sockaddr  **sa,
						      int               *len);

#endif /* SOUP_ADDRESS_H */
