/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-address.c: Internet address handing
 *
 * Authors:
 *      David Helder  (dhelder@umich.edu)
 *      Alex Graveley (alex@ximian.com)
 *
 * Original code compliments of David Helder's GNET Networking Library, and is
 * Copyright (C) 2000  David Helder & Andrew Lanoix.
 *
 * All else Copyright (C) 2000-2002, Ximian, Inc.
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

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include "soup-private.h"
#include "soup-address.h"
#include "soup-dns.h"

struct _SoupAddress {
	gchar*          name;
	int             family;
	union {
		struct in_addr  in;
#ifdef HAVE_IPV6
		struct in6_addr in6;
#endif
	} addr;

	gint            ref_count;
	gint            cached;
};

#include <unistd.h>
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

static void
soup_address_new_sync_cb (SoupAddress        *addr,
			  SoupKnownErrorCode  status,
			  gpointer            user_data)
{
	SoupAddress **ret = user_data;
	*ret = addr;
}

/**
 * soup_address_new_sync:
 * @name: a hostname, as with soup_address_new()
 *
 * Return value: a #SoupAddress, or %NULL if the lookup fails.
 **/
SoupAddress *
soup_address_new_sync (const char *name)
{
	SoupAddress *ret = (SoupAddress *) 0xdeadbeef;

	soup_address_new (name, soup_address_new_sync_cb, &ret);

	while (1) {
		g_main_iteration (TRUE);
		if (ret != (SoupAddress *) 0xdeadbeef) return ret;
	}

	return ret;
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
	SoupAddress *ia;

	ia = g_new0 (SoupAddress, 1);
	ia->ref_count = 1;
	ia->family = sa->sa_family;

	switch (ia->family) {
	case AF_INET:
	{
		struct sockaddr_in *sa_in = (struct sockaddr_in *)sa;

		memcpy (&ia->addr.in, &sa_in->sin_addr, sizeof (ia->addr.in));
		if (port)
			*port = g_ntohs (sa_in->sin_port);
		break;
	}

#ifdef HAVE_IPV6
	case AF_INET6:
	{
		struct sockaddr_in6 *sa_in6 = (struct sockaddr_in6 *)sa;

		memcpy (&ia->addr.in6, &sa_in6->sin6_addr, sizeof (ia->addr.in6));
		if (port)
			*port = g_ntohs (sa_in6->sin6_port);
		break;
	}
#endif

	default:
		g_free (ia);
		ia = NULL;
		break;
	}

	return ia;
}

/**
 * soup_address_ipv4_any:
 *
 * Return value: a #SoupAddress corresponding to %INADDR_ANY, suitable
 * for passing to soup_socket_server_new().
 **/
SoupAddress *
soup_address_ipv4_any (void)
{
	static SoupAddress *ipv4_any = NULL;

	if (!ipv4_any) {
		struct sockaddr_in sa_in;

		sa_in.sin_family = AF_INET;
		sa_in.sin_addr.s_addr = INADDR_ANY;
		ipv4_any = soup_address_new_from_sockaddr ((struct sockaddr *)&sa_in, NULL);
	}

	soup_address_ref (ipv4_any);
	return ipv4_any;
}

/**
 * soup_address_ipv6_any:
 *
 * Return value: If soup was compiled without IPv6 support, %NULL.
 * Otherwise, a #SoupAddress corresponding to the IPv6 address "::",
 * suitable for passing to soup_socket_server_new().
 **/
SoupAddress *
soup_address_ipv6_any (void)
{
	static SoupAddress *ipv6_any = NULL;

#ifdef HAVE_IPV6
	if (!ipv6_any) {
		struct sockaddr_in6 sa_in6;

		sa_in6.sin6_family = AF_INET6;
		sa_in6.sin6_addr = in6addr_any;
		ipv6_any = soup_address_new_from_sockaddr ((struct sockaddr *)&sa_in6, NULL);
	}

	soup_address_ref (ipv6_any);
#endif
	return ipv6_any;
}

/**
 * soup_address_ref
 * @ia: SoupAddress to reference
 *
 * Increment the reference counter of the SoupAddress.
 **/
void
soup_address_ref (SoupAddress* ia)
{
	g_return_if_fail (ia != NULL);

	++ia->ref_count;
}

/**
 * soup_address_copy
 * @ia: SoupAddress to copy
 *
 * Creates a copy of the given SoupAddress
 **/
SoupAddress *
soup_address_copy (SoupAddress* ia)
{
	SoupAddress* new_ia;
	g_return_val_if_fail (ia != NULL, NULL);

	new_ia = g_new0 (SoupAddress, 1);
	new_ia->ref_count = 1;

	new_ia->name = g_strdup (ia->name);
	new_ia->family = ia->family;
	memcpy (&new_ia->addr, &ia->addr, sizeof (new_ia->addr));

	return new_ia;
}

static void
soup_address_get_name_sync_cb (SoupAddress        *addr,
			       SoupKnownErrorCode  status,
			       const char         *name,
			       gpointer            user_data)
{
	const char **ret = user_data;
	*ret = name;
}

/**
 * soup_address_get_name_sync:
 * @ia: a #SoupAddress
 *
 * Return value: the hostname associated with @ia, as with
 * soup_address_get_name().
 **/
const char *
soup_address_get_name_sync (SoupAddress *ia)
{
	const char *ret = (const char *) 0xdeadbeef;

	soup_address_get_name (ia, soup_address_get_name_sync_cb, &ret);

	while (1) {
		g_main_iteration (TRUE);
		if (ret != (const char *) 0xdeadbeef) return ret;
	}

	return ret;
}

/**
 * soup_address_get_canonical_name:
 * @ia: Address to get the canonical name of.
 *
 * Get the "canonical" name of an address (eg, for IP4 the dotted
 * decimal name 141.213.8.59).
 *
 * Returns: %NULL if there was an error.  The caller is responsible
 * for deleting the returned string.
 **/
char*
soup_address_get_canonical_name (SoupAddress* ia)
{
	switch (ia->family) {
	case AF_INET:
	{
#ifdef HAVE_INET_NTOP
		char buffer[INET_ADDRSTRLEN];

		inet_ntop (ia->family, &ia->addr.in, buffer, sizeof (buffer));
		return g_strdup (buffer);
#else
		return g_strdup (inet_ntoa (ia->addr.in));
#endif
	}

#ifdef HAVE_IPV6
	case AF_INET6:
	{
		char buffer[INET6_ADDRSTRLEN];

		inet_ntop (ia->family, &ia->addr.in6, buffer, sizeof (buffer));
		return g_strdup (buffer);
	}
#endif

	default:
		return NULL;
	}
}

/**
 * soup_address_make_sockaddr:
 * @ia: The %SoupAddress.
 * @port: The port number
 * @sa: Pointer to struct sockaddr * to output the sockaddr into
 * @len: Pointer to int to return the size of the sockaddr into
 *
 * This creates an appropriate struct sockaddr for @ia and @port
 * and outputs it into *@sa. The caller must free *@sa with g_free().
 **/
void
soup_address_make_sockaddr (SoupAddress *ia, guint port,
			    struct sockaddr **sa, int *len)
{
	switch (ia->family) {
	case AF_INET:
	{
		struct sockaddr_in sa_in;

		memset (&sa_in, 0, sizeof (sa_in));
		sa_in.sin_family = AF_INET;
		memcpy (&sa_in.sin_addr, &ia->addr.in, sizeof (sa_in.sin_addr));
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
		memcpy (&sa_in6.sin6_addr, &ia->addr.in6, sizeof (sa_in6.sin6_addr));
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
	SoupAddressNewFn        func;
	gpointer                data;
	SoupAsyncHandle         lookup_id;
} SoupAddressLookupState;

static void
soup_address_new_cb (SoupKnownErrorCode err, struct hostent *h, gpointer data)
{
	SoupAddressLookupState *state = data;
	SoupAddress *ia;

	if (err == SOUP_ERROR_OK) {
		ia = g_new0 (SoupAddress, 1);
		ia->name = g_strdup (h->h_name);
		ia->family = h->h_addrtype;
		memcpy (&ia->addr, h->h_addr_list[0],
			MIN (h->h_length, sizeof (ia->addr)));
	} else
		ia = NULL;

	state->func (ia, err, state->data);
	g_free (state);
}

/**
 * soup_address_new:
 * @name: a nice name (eg, mofo.eecs.umich.edu) or a dotted decimal name
 *   (eg, 141.213.8.59).  You can delete the after the function is called.
 * @func: Callback function.
 * @data: User data passed when callback function is called.
 *
 * Create a SoupAddress from a name asynchronously.  Once the
 * structure is created, it will call the callback.  It may call the
 * callback before the function returns.  It will call the callback
 * if there is a failure.
 *
 * Currently this routine forks and does the lookup, which can cause
 * some problems. In general, this will work ok for most programs most
 * of the time. It will be slow or even fail when using operating
 * systems that copy the entire process when forking.
 *
 * If you need to lookup a lot of addresses, you should call
 * g_main_iteration(FALSE) between calls. This will help prevent an
 * explosion of processes.
 *
 * Returns: ID of the lookup which can be used with
 * soup_address_new_cancel() to cancel it; NULL on immediate
 * success or failure.
 **/
SoupAsyncHandle
soup_address_new (const gchar* name, SoupAddressNewFn func, gpointer data)
{
	SoupAddressLookupState *state;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (func != NULL, NULL);

	state = g_new0 (SoupAddressLookupState, 1);
	state->func = func;
	state->data = data;
	state->lookup_id = soup_gethostbyname (name, soup_address_new_cb, state);

	return state;
}

/**
 * soup_address_new_cancel:
 * @id: ID of the lookup
 *
 * Cancel an asynchronous SoupAddress creation that was started with
 * soup_address_new(). The lookup's callback will not be called.
 */
void
soup_address_new_cancel (SoupAsyncHandle id)
{
	SoupAddressLookupState *state = (SoupAddressLookupState *)id;

	g_return_if_fail (state != NULL);

	soup_gethostby_cancel (state->lookup_id);
	g_free (state);
}

/**
 * soup_address_unref
 * @ia: SoupAddress to unreference
 *
 * Remove a reference from the SoupAddress.  When reference count
 * reaches 0, the address is deleted.
 **/
void
soup_address_unref (SoupAddress* ia)
{
	g_return_if_fail (ia != NULL);

	--ia->ref_count;

	if (ia->ref_count == 0) {
		g_free (ia->name);
		g_free (ia);
	}
}

typedef struct {
	SoupAddress          *ia;
	SoupAddressGetNameFn  func;
	gpointer              data;
	SoupAsyncHandle       lookup_id;
} SoupAddressReverseState;

static void
soup_address_get_name_cb (SoupKnownErrorCode err, struct hostent *h, gpointer data)
{
	SoupAddressReverseState *state = data;

	if (err == SOUP_ERROR_OK)
		state->ia->name = g_strdup (h->h_name);

	state->func (state->ia, err, state->ia->name, state->data);
	soup_address_unref (state->ia);
	g_free (state);
}

/**
 * soup_address_get_name:
 * @ia: Address to get the name of.
 * @func: Callback function.
 * @data: User data passed when callback function is called.
 *
 * Get the nice name of the address (eg, "mofo.eecs.umich.edu").
 * This function will use the callback once it knows the nice name.
 * It may even call the callback before it returns.  The callback
 * will be called if there is an error.
 *
 * As with soup_address_new(), this forks to do the lookup.
 *
 * Returns: ID of the lookup which can be used with
 * soup_address_get_name_cancel() to cancel it; NULL on
 * immediate success or failure.
 **/
SoupAsyncHandle
soup_address_get_name (SoupAddress *ia,
		       SoupAddressGetNameFn func,
		       gpointer data)
{
	SoupAddressReverseState *state;

	g_return_val_if_fail (ia != NULL, NULL);
	g_return_val_if_fail (func != NULL, NULL);

	if (ia->name) {
		(func) (ia, SOUP_ERROR_OK, ia->name, data);
		return NULL;
	}

	state = g_new0 (SoupAddressReverseState, 1);
	state->ia = ia;
	soup_address_ref (ia);
	state->func = func;
	state->data = data;
	state->lookup_id = soup_gethostbyaddr (&ia->addr, ia->family,
					       soup_address_get_name_cb, state);

	return state;
}

/**
 * soup_address_get_name_cancel:
 * @id: ID of the lookup
 *
 * Cancel an asynchronous nice name lookup that was started with
 * soup_address_get_name().
 */
void
soup_address_get_name_cancel (SoupAsyncHandle id)
{
	SoupAddressReverseState *state = id;

	g_return_if_fail (state != NULL);

	soup_address_unref (state->ia);
	soup_gethostby_cancel (state->lookup_id);
	g_free(state);
}
