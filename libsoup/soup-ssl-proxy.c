/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-queue.c: Asyncronous Callback-based SOAP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2001-2002, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <glib.h>

#include <libsoup/soup-misc.h>
#include <libsoup/soup-private.h>

#ifdef HAVE_OPENSSL_SSL_H
#include "soup-openssl.h"
#endif

static gint ssl_library = 0; /* -1 = fail,
				 0 = first time, 
				 1 = openssl */

static gboolean server_mode = FALSE;

static GMainLoop *loop;

static void 
soup_ssl_proxy_init (void)
{
	ssl_library = -1;

#ifdef HAVE_OPENSSL_SSL_H
	if (ssl_library == -1)
		ssl_library = soup_openssl_init (server_mode) ? 1 : -1;
#endif

	if (ssl_library == -1) return;
}

static GIOChannel *
soup_ssl_proxy_get_iochannel (GIOChannel *sock)
{
	switch (ssl_library) {
	case -1:
		g_warning ("SSL Not Supported.");
		return NULL;
	case 0:
	default:
		soup_ssl_proxy_init ();
		return soup_ssl_proxy_get_iochannel (sock);
#ifdef HAVE_OPENSSL_SSL_H
	case 1:
		return soup_openssl_get_iochannel (sock);
#endif
	}
}

static gboolean 
soup_ssl_proxy_readwrite (GIOChannel   *iochannel, 
			  GIOCondition  condition, 
			  GIOChannel   *dest)
{
	gchar read_buf [RESPONSE_BLOCK_SIZE];
	gsize bytes_read = 0, bytes_written = 0, write_total = 0;
	GIOError error;

	error = g_io_channel_read (iochannel,
				   read_buf,
				   sizeof (read_buf),
				   &bytes_read);

	if (error == G_IO_ERROR_AGAIN) return TRUE;

	if (error != G_IO_ERROR_NONE || bytes_read == 0) goto FINISH;

	errno = 0;

	while (write_total != bytes_read) {
		error = g_io_channel_write (dest, 
					    &read_buf [write_total], 
					    bytes_read - write_total, 
					    &bytes_written);

		if (error != G_IO_ERROR_NONE || errno != 0) goto FINISH;

		write_total += bytes_written;
	}

	if (condition & G_IO_ERR)
		goto FINISH;

	return TRUE;

 FINISH:
	g_io_channel_close (iochannel);
	g_io_channel_close (dest);
	g_main_quit (loop);
	return FALSE;
}

int
main (int argc, char** argv) 
{
	gchar *env;
	GIOChannel *read_chan, *write_chan, *sock_chan;
	int sockfd, flags;

	if (getenv ("SOUP_PROXY_DELAY")) {
		g_warning ("Proxy delay set: sleeping for 20 seconds");
		sleep (20);
	}

	loop = g_main_new (FALSE);

	env = getenv ("SOCKFD");
	if (!env) 
		g_error ("SOCKFD environment not set.");

	sockfd = atoi (env);
	if (sockfd <= 0)
		g_error ("Invalid SOCKFD environment set.");

	env = getenv ("IS_SERVER");
	if (env)
		server_mode = TRUE;

	read_chan = g_io_channel_unix_new (STDIN_FILENO);
	if (!read_chan) 
		g_error ("Unable to open STDIN");

	write_chan = g_io_channel_unix_new (STDOUT_FILENO);
	if (!write_chan) 
		g_error ("Unable to open STDOUT");

	/* We use select. All fds should block. */
	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl (sockfd, F_SETFL, flags & ~O_NONBLOCK);
	flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl (STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
	flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
	fcntl (STDOUT_FILENO, F_SETFL, flags & ~O_NONBLOCK);

	sock_chan = g_io_channel_unix_new (sockfd);
	sock_chan = soup_ssl_proxy_get_iochannel (sock_chan);
	if (!sock_chan)
		g_error ("Unable to establish SSL connection");

	g_io_add_watch (read_chan, 
			G_IO_IN | G_IO_PRI | G_IO_ERR, 
			(GIOFunc) soup_ssl_proxy_readwrite,
			sock_chan);

	g_io_add_watch (sock_chan, 
			G_IO_IN | G_IO_PRI | G_IO_ERR, 
			(GIOFunc) soup_ssl_proxy_readwrite,
			write_chan);

	g_main_run (loop);

	exit (0);
}
