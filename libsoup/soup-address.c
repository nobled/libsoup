/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-address.c: Internet address handing
 *
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include "soup-private.h"
#include "soup-address.h"
#include "soup-dns.h"

#ifndef socklen_t
#  define socklen_t size_t
#endif

#ifndef INET_ADDRSTRLEN
#  define INET_ADDRSTRLEN 16
#  define INET6_ADDRSTRLEN 46
#endif

#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif


struct _SoupAddressPrivate {
	char *name, *physical;
	int family;
	char buf[sizeof (struct soup_sockaddr_max)];

	SoupAsyncHandle lookup_dns_id;
	guint lookup_idle_id;
	GSList *lookups;
};


#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

static void
init (GObject *object)
{
	SoupAddress *addr = SOUP_ADDRESS (object);

	addr->priv = g_new0 (SoupAddressPrivate, 1);
}

static void
finalize (GObject *object)
{
	SoupAddress *addr = SOUP_ADDRESS (object);

	if (addr->priv->name)
		g_free (addr->priv->name);
	if (addr->priv->physical)
		g_free (addr->priv->physical);

	while (addr->priv->lookups)
		soup_address_cancel_get_name (addr->priv->lookups->data);
	if (addr->priv->lookup_dns_id)
		soup_gethostby_cancel (addr->priv->lookup_dns_id);
	if (addr->priv->lookup_idle_id)
		g_source_remove (addr->priv->lookup_idle_id);

	g_free (addr->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_address, SoupAddress, class_init, init, PARENT_TYPE)



/**
 * soup_address_new_from_hostent:
 * @h: a struct #hostent
 *
 * Return value: a #SoupAddress corresponding to @h
 **/
SoupAddress *
soup_address_new_from_hostent (struct hostent *h)
{
	SoupAddress *addr;

	addr = g_object_new (SOUP_TYPE_ADDRESS, NULL);
	addr->priv->name = g_strdup (h->h_name);
	addr->priv->family = h->h_addrtype;
	memcpy (addr->priv->buf, h->h_addr_list[0],
		MIN (sizeof (addr->priv->buf), h->h_length));

	return addr;
}

/**
 * soup_address_new_any:
 * @family: the address family
 *
 * Return value: a #SoupAddress corresponding to the "any" address
 * for @family (or %NULL if @family isn't supported), suitable for
 * passing to soup_socket_server_new().
 **/
SoupAddress *
soup_address_new_any (int family)
{
	static SoupAddress *ipv4_any = NULL;
#ifdef HAVE_IPV6
	static SoupAddress *ipv6_any = NULL;
#endif

	switch (family) {
	case AF_INET:
		if (!ipv4_any) {
			struct sockaddr_in sa_in;

			sa_in.sin_family = AF_INET;
			sa_in.sin_addr.s_addr = INADDR_ANY;
			ipv4_any = soup_address_new_from_sockaddr ((struct sockaddr *)&sa_in, NULL);
		}
		g_object_ref (ipv4_any);
		return ipv4_any;

#ifdef HAVE_IPV6
	case AF_INET6:
		if (!ipv6_any) {
			struct sockaddr_in6 sa_in6;

			sa_in6.sin6_family = AF_INET6;
			sa_in6.sin6_addr = in6addr_any;
			ipv6_any = soup_address_new_from_sockaddr ((struct sockaddr *)&sa_in6, NULL);
		}
		g_object_ref (ipv6_any);
		return ipv6_any;
#endif

	default:
		return NULL;
	}
}

/**
 * soup_address_new_from_sockaddr:
 * @sa: a pointer to a sockaddr
 * @port: pointer to a variable to store @sa's port number in
 *
 * This parses @sa and returns its address as a #SoupAddress
 * and its port in @port. @sa can point to a #sockaddr_in or
 * (if soup was compiled with IPv6 support) a #sockaddr_in6.
 *
 * Return value: a #SoupAddress, or %NULL if the lookup fails.
 **/
SoupAddress *
soup_address_new_from_sockaddr (struct sockaddr *sa,
				guint *port)
{
	SoupAddress *addr;

	addr = g_object_new (SOUP_TYPE_ADDRESS, NULL);
	addr->priv->family = sa->sa_family;

	switch (addr->priv->family) {
	case AF_INET:
	{
		struct sockaddr_in *sa_in = (struct sockaddr_in *)sa;

		memcpy (addr->priv->buf, &sa_in->sin_addr,
			sizeof (sa_in->sin_addr));
		if (port)
			*port = g_ntohs (sa_in->sin_port);
		break;
	}

#ifdef HAVE_IPV6
	case AF_INET6:
	{
		struct sockaddr_in6 *sa_in6 = (struct sockaddr_in6 *)sa;

		memcpy (addr->priv->buf, &sa_in6->sin6_addr,
			sizeof (sa_in6->sin6_addr));
		if (port)
			*port = g_ntohs (sa_in6->sin6_port);
		break;
	}
#endif

	default:
		g_object_unref (addr);
		addr = NULL;
		break;
	}

	return addr;
}

/**
 * soup_address_make_sockaddr:
 * @addr: The %SoupAddress.
 * @port: The port number
 * @sa: Pointer to struct sockaddr * to output the sockaddr into
 * @len: Pointer to int to return the size of the sockaddr into
 *
 * This creates an appropriate struct sockaddr for @addr and @port
 * and outputs it into *@sa. The caller must free *@sa with g_free().
 **/
void
soup_address_make_sockaddr (SoupAddress *addr, guint port,
			    struct sockaddr **sa, int *len)
{
	switch (addr->priv->family) {
	case AF_INET:
	{
		struct sockaddr_in sa_in;

		memset (&sa_in, 0, sizeof (sa_in));
		sa_in.sin_family = AF_INET;
		memcpy (&sa_in.sin_addr, addr->priv->buf,
			sizeof (sa_in.sin_addr));
		sa_in.sin_port = g_htons (port);

		*sa = g_memdup (&sa_in, sizeof (sa_in));
		*len = sizeof (sa_in);
		break;
	}

#ifdef HAVE_IPV6
	case AF_INET6:
	{
		struct sockaddr_in6 sa_in6;

		memset (&sa_in6, 0, sizeof (sa_in6));
		sa_in6.sin6_family = AF_INET6;
		memcpy (&sa_in6.sin6_addr, addr->priv->buf,
			sizeof (sa_in6.sin6_addr));
		sa_in6.sin6_port = g_htons (port);

		*sa = g_memdup (&sa_in6, sizeof (sa_in6));
		*len = sizeof (sa_in6);
		break;
	}
#endif

	default:
		*sa = NULL;
		*len = 0;
	}
}


typedef struct {
	SoupAddressGetNameFn  func;
	gpointer              data;
	SoupAddress          *addr;
} SoupAddressGetNameState;

static gboolean
got_name (gpointer data)
{
	SoupAddress *addr = data;
	SoupAddressGetNameState *state;

	addr->priv->lookup_idle_id = 0;

	while (addr->priv->lookups) {
		state = addr->priv->lookups->data;
		addr->priv->lookups = g_slist_remove (addr->priv->lookups, state);

		state->func (addr, addr->priv->name, state->data);
		g_free (state);
	}
	return FALSE;
}

static void
resolved_name (SoupKnownErrorCode err, struct hostent *h, gpointer data)
{
	SoupAddress *addr = data;

	addr->priv->lookup_dns_id = NULL;

	if (err == SOUP_ERROR_OK && !addr->priv->name)
		addr->priv->name = g_strdup (h->h_name);
	got_name (addr);
}

/**
 * soup_address_get_name:
 * @addr: the address
 * @func: function to call with @addr's name
 * @data: data to pass to @func
 *
 * Determines the hostname associated with @addr and asynchronously
 * calls @func. Even if @addr's name is already known, @func will not
 * be called right away.
 *
 * Return value: a handle that can be passed to
 * soup_address_cancel_get_name() to cancel the lookup.
 **/
SoupAsyncHandle
soup_address_get_name (SoupAddress *addr,
		       SoupAddressGetNameFn func, gpointer data)
{
	SoupAddressGetNameState *state;

	state = g_new0 (SoupAddressGetNameState, 1);
	state->addr = addr;
	state->func = func;
	state->data = data;

	addr->priv->lookups = g_slist_prepend (addr->priv->lookups, state);
	if (addr->priv->name) {
		if (!addr->priv->lookup_idle_id)
			addr->priv->lookup_idle_id = g_idle_add (got_name, addr);
	} else if (!addr->priv->lookup_dns_id) {
		addr->priv->lookup_dns_id =
			soup_gethostbyaddr (addr->priv->buf,
					    addr->priv->family,
					    resolved_name, addr);
	}
	return state;
}

/**
 * soup_address_cancel_get_name:
 * @id: operation to cancel
 *
 * Cancels the lookup identified by @id. The callback function
 * will not be called.
 **/
void
soup_address_cancel_get_name (SoupAsyncHandle id)
{
	SoupAddressGetNameState *state = id;
	SoupAddress *addr = state->addr;

	addr->priv->lookups = g_slist_remove (addr->priv->lookups, state);
	g_free (state);

	if (!addr->priv->lookups && addr->priv->lookup_dns_id) {
		if (addr->priv->lookup_dns_id) {
			soup_gethostby_cancel (addr->priv->lookup_dns_id);
			addr->priv->lookup_dns_id = NULL;
		} else if (addr->priv->lookup_idle_id) {
			g_source_remove (addr->priv->lookup_idle_id);
			addr->priv->lookup_idle_id = 0;
		}
	}
}

/**
 * soup_address_check_name:
 * @addr: an address
 *
 * Return value: the hostname associated with @addr, if known.
 * Otherwise %NULL.
 **/
const char *
soup_address_check_name (SoupAddress *addr)
{
	return addr->priv->name;
}

/**
 * soup_address_get_physical:
 * @addr: an address
 *
 * Return value: the physical address (eg, "127.0.0.1") associated
 * with @addr.
 **/
const char *
soup_address_get_physical (SoupAddress *addr)
{
	if (!addr->priv->physical) {
		addr->priv->physical = soup_ntop (addr->priv->buf,
						  addr->priv->family);
	}
	return addr->priv->physical;
}
