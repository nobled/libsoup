/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-queue.c: Asyncronous Callback-based HTTP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#include <glib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "soup-socks.h"
#include "soup-address.h"
#include "soup-error.h"
#include "soup-socket.h"

typedef struct {
	SoupSocket          *socket;
	
	enum {
		SOCKS_4_DEST_ADDR_LOOKUP,
		SOCKS_4_SEND_DEST_ADDR,
		SOCKS_4_VERIFY_SUCCESS,

		SOCKS_5_SEND_INIT,
		SOCKS_5_VERIFY_INIT,
		SOCKS_5_SEND_AUTH,
		SOCKS_5_VERIFY_AUTH,
		SOCKS_5_SEND_DEST_ADDR,
		SOCKS_5_VERIFY_SUCCESS	
	} phase;

	SoupUri             *proxy_uri;
	SoupUri             *dest_uri;
	SoupAddress         *dest_addr;
	SoupSocketConnectFn  cb;
	gpointer             user_data;
} SoupSocksData;

static void
socks_data_free (SoupSocksData *sd)
{
	if (sd->socket)
		g_object_unref (sd->socket);

	if (sd->proxy_uri)
		soup_uri_free (sd->proxy_uri);

	if (sd->dest_uri)
		soup_uri_free (sd->dest_uri);

	if (sd->dest_addr)
		soup_address_unref (sd->dest_addr);

	while (g_source_remove_by_user_data (sd))
		continue;

	g_free (sd);
}

static inline void
WSTRING (char *buf, gint *len, gchar *str)
{
	gint l = strlen (str);
	buf [(*len)++] = (guchar) l;
	strncpy (&buf [*len], str, l);
	*len += l;
}

static inline void
WSHORT (char *buf, gint *len, gushort port)
{
	gushort np = htons (port);

	memcpy (&buf [*len], &np, sizeof (np));
	*len += sizeof (np);
}

static gboolean
soup_socks_write (GIOChannel* iochannel, 
		  GIOCondition condition, 
		  SoupSocksData *sd)
{
	struct sockaddr *sa;
	gboolean finished = FALSE;
	guchar buf[128];
	gint len = 0, sa_len;
	gsize bytes_written;
	GIOError error;

	switch (sd->phase) {
	case SOCKS_4_SEND_DEST_ADDR: 
		/* FIXME: This won't work if dest_addr isn't IPv4 */

		buf[len++] = 0x04;
		buf[len++] = 0x01;
		WSHORT (buf, &len, (gushort) sd->dest_uri->port);
		soup_address_make_sockaddr (sd->dest_addr, sd->dest_uri->port,
					    &sa, &sa_len);
		memcpy (&buf [len], 
			&((struct sockaddr_in *) sa)->sin_addr,
			4);
		g_free (sa);
		len += 4;
		buf[8] = 0x00;
		len = 9;
		
		sd->phase = SOCKS_4_VERIFY_SUCCESS;
		finished = TRUE;
		break;

	case SOCKS_5_SEND_INIT:
		if (sd->proxy_uri->user) {
			buf[0] = 0x05;
			buf[1] = 0x02;
			buf[2] = 0x00;
			buf[3] = 0x02;
			len = 4;
		} else {
			buf[0] = 0x05;
			buf[1] = 0x01;
			buf[2] = 0x00;
			len = 3;
		}

		sd->phase = SOCKS_5_VERIFY_INIT;
		break;

	case SOCKS_5_SEND_AUTH:
		buf[len++] = 0x01;
		WSTRING (buf, &len, sd->proxy_uri->user);
		WSTRING (buf, &len, sd->proxy_uri->passwd);

		sd->phase = SOCKS_5_VERIFY_AUTH;
		break;

	case SOCKS_5_SEND_DEST_ADDR:
		buf[len++] = 0x05;
		buf[len++] = 0x01;
		buf[len++] = 0x00;
		buf[len++] = 0x03;
		WSTRING (buf, &len, sd->dest_uri->host);
		WSHORT (buf, &len, (gushort) sd->dest_uri->port);

		sd->phase = SOCKS_5_VERIFY_SUCCESS;
		finished = TRUE;
		break;

	default:
		return TRUE;
	}

	error = g_io_channel_write (iochannel, buf, len, &bytes_written);
	
	if (error == G_IO_ERROR_AGAIN)
		return TRUE;
	if (error != G_IO_ERROR_NONE)
		goto CONNECT_ERROR;

	return !finished;

 CONNECT_ERROR:
	(*sd->cb) (NULL, SOUP_ERROR_CANT_CONNECT, sd->user_data);
	socks_data_free (sd);
	return FALSE;
}

static gboolean
soup_socks_read (GIOChannel* iochannel, 
		 GIOCondition condition, 
		 SoupSocksData *sd)
{
	guchar buf[128];
	gsize bytes_read;
	GIOError error;

	error = g_io_channel_read (iochannel, buf, sizeof (buf), &bytes_read);

	if (error == G_IO_ERROR_AGAIN)
		return TRUE;
	if (error != G_IO_ERROR_NONE || bytes_read == 0)
		goto CONNECT_ERROR;

	switch (sd->phase) {
	case SOCKS_4_VERIFY_SUCCESS:
		if (bytes_read < 4 || buf[1] != 90) 
			goto CONNECT_ERROR;

		goto CONNECT_OK;

	case SOCKS_5_VERIFY_INIT:
		if (bytes_read < 2 || buf [0] != 0x05 || buf [1] == 0xff)
			goto CONNECT_ERROR;

		if (buf [1] == 0x02) 
			sd->phase = SOCKS_5_SEND_AUTH;
		else 
			sd->phase = SOCKS_5_SEND_DEST_ADDR;
		break;

	case SOCKS_5_VERIFY_AUTH:
		if (bytes_read < 2 || buf [0] != 0x01 || buf [1] != 0x00)
			goto CONNECT_ERROR;

		sd->phase = SOCKS_5_SEND_DEST_ADDR;
		break;

	case SOCKS_5_VERIFY_SUCCESS:
		if (bytes_read < 10 || buf[0] != 0x05 || buf[1] != 0x00) 
			goto CONNECT_ERROR;

		goto CONNECT_OK;

	default:
		break;
	}

	return TRUE;

 CONNECT_OK:
	g_object_ref (sd->socket);
	(*sd->cb) (sd->socket, SOUP_ERROR_OK, sd->user_data);
	socks_data_free (sd);
	return FALSE;

 CONNECT_ERROR:
	(*sd->cb) (NULL, SOUP_ERROR_CANT_CONNECT, sd->user_data);
	socks_data_free (sd);
	return FALSE;
}

static gboolean
soup_socks_error (GIOChannel* iochannel, 
		  GIOCondition condition, 
		  SoupSocksData *sd)
{
	(*sd->cb) (NULL, SOUP_ERROR_CANT_CONNECT, sd->user_data);
	socks_data_free (sd);
	return FALSE;	
}

static void
soup_lookup_dest_addr_cb (SoupAddress        *inetaddr, 
			  SoupKnownErrorCode  status, 
			  gpointer            data)
{
	SoupSocksData *sd = data;
	GIOChannel *channel;

	if (status != SOUP_ERROR_OK) {
		(*sd->cb) (NULL, status, sd->user_data); 
		socks_data_free (sd);
		return;
	}

	sd->dest_addr = inetaddr;
	sd->phase = SOCKS_4_SEND_DEST_ADDR;

	channel = soup_socket_get_iochannel (sd->socket);
	g_io_add_watch (channel, G_IO_OUT, (GIOFunc) soup_socks_write, sd);
	g_io_add_watch (channel, G_IO_IN, (GIOFunc) soup_socks_read, sd);
	g_io_add_watch (channel, 
			G_IO_ERR | G_IO_HUP | G_IO_NVAL, 
			(GIOFunc) soup_socks_error, 
			sd);		
	g_io_channel_unref (channel);
}

void
soup_socks_proxy_connect (SoupSocket          *socket,
			  SoupUri             *proxy_uri,
			  SoupUri             *dest_uri,
			  SoupSocketConnectFn  cb,
			  gpointer             user_data)
{
	SoupSocksData *sd = NULL;
	GIOChannel *channel;

	sd = g_new0 (SoupSocksData, 1);
	sd->socket = socket;
	sd->proxy_uri = soup_uri_copy (proxy_uri);
	sd->dest_uri = soup_uri_copy (dest_uri);
	sd->cb = cb;
	sd->user_data = user_data;

	switch (proxy_uri->protocol) {
	case SOUP_PROTOCOL_SOCKS4:
		soup_address_new (dest_uri->host, 
				  soup_lookup_dest_addr_cb,
				  sd);
		sd->phase = SOCKS_4_DEST_ADDR_LOOKUP;
		break;

	case SOUP_PROTOCOL_SOCKS5:
		channel = soup_socket_get_iochannel (socket);
		g_io_add_watch (channel, 
				G_IO_OUT, 
				(GIOFunc) soup_socks_write, 
				sd);
		g_io_add_watch (channel, 
				G_IO_IN, 
				(GIOFunc) soup_socks_read, 
				sd);
		g_io_add_watch (channel, 
				G_IO_ERR | G_IO_HUP | G_IO_NVAL, 
				(GIOFunc) soup_socks_error, 
				sd);		
		g_io_channel_unref (channel);

		sd->phase = SOCKS_5_SEND_INIT;
		break;

	default:
		g_assert_not_reached ();
		break;
	}

	return;
}
