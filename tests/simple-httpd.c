/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2002, Ximian, Inc.
 */

#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-server.h>

static void
server_callback (SoupServerContext *context, SoupMessage *msg, gpointer data)
{
	char *path, *path_to_open, *slash;
	struct stat st;
	int fd;

	path = soup_uri_to_string (soup_message_get_uri (msg), TRUE);
	printf ("%s %s HTTP/1.%d\n", msg->method, path,
		soup_message_get_http_version (msg));

	if (soup_method_get_id (msg->method) != SOUP_METHOD_ID_GET) {
		soup_message_set_error (msg, SOUP_ERROR_NOT_IMPLEMENTED);
		goto DONE;
	}

	if (path) {
		if (*path != '/') {
			soup_message_set_error (msg, SOUP_ERROR_BAD_REQUEST);
			goto DONE;
		}
	} else
		path = "";

	path_to_open = g_strdup_printf (".%s", path);

 TRY_AGAIN:
	if (stat (path_to_open, &st) == -1) {
		g_free (path_to_open);
		if (errno == EPERM)
			soup_message_set_error (msg, SOUP_ERROR_FORBIDDEN);
		else if (errno == ENOENT)
			soup_message_set_error (msg, SOUP_ERROR_NOT_FOUND);
		else
			soup_message_set_error (msg, SOUP_ERROR_INTERNAL);
		goto DONE;
	}

	if (S_ISDIR (st.st_mode)) {
		slash = strrchr (path_to_open, '/');
		if (!slash || slash[1]) {
			char *uri, *redir_uri;

			uri = soup_uri_to_string (soup_message_get_uri (msg), FALSE);
			redir_uri = g_strdup_printf ("%s/", uri);
			soup_message_add_header (msg->response_headers,
						 "Location", redir_uri);
			soup_message_set_error (msg, SOUP_ERROR_MOVED_PERMANENTLY);
			g_free (redir_uri);
			g_free (uri);
			g_free (path_to_open);
			goto DONE;
		}

		g_free (path_to_open);
		path_to_open = g_strdup_printf (".%s/index.html", path);
		goto TRY_AGAIN;
	}

	fd = open (path_to_open, O_RDONLY);
	g_free (path_to_open);
	if (fd == -1) {
		soup_message_set_error (msg, SOUP_ERROR_INTERNAL);
		goto DONE;
	}

	msg->response.owner = SOUP_BUFFER_SYSTEM_OWNED;
	msg->response.length = st.st_size;
	msg->response.body = g_malloc (msg->response.length);

	read (fd, msg->response.body, msg->response.length);
	close (fd);

	soup_message_set_error (msg, SOUP_ERROR_OK);

 DONE:
	printf ("  -> %d %s\n", msg->errorcode, msg->errorphrase);
}

int
main (int argc, char **argv)
{
	GMainLoop *loop;
	int opt;
	int port = SOUP_SERVER_ANY_PORT;
	int ssl_port = SOUP_SERVER_ANY_PORT;
	SoupServer *server, *ssl_server;

	while ((opt = getopt (argc, argv, "p:s:")) != -1) {
		switch (opt) {
		case 'p':
			port = atoi (optarg);
			break;
		case 's':
			ssl_port = atoi (optarg);
			break;
		default:
			fprintf (stderr, "Usage: %s [-p port] [-s ssl-port]\n",
				 argv[0]);
			exit (1);
		}
	}

	server = soup_server_new (SOUP_PROTOCOL_HTTP, port);
	if (!server) {
		fprintf (stderr, "Unable to bind to server port %d\n", port);
		exit (1);
	}
	soup_server_register (server, NULL, NULL, server_callback, NULL, NULL);

	ssl_server = soup_server_new (SOUP_PROTOCOL_HTTPS, ssl_port);
	if (!ssl_server) {
		fprintf (stderr, "Unable to bind to SSL server port %d\n", ssl_port);
		exit (1);
	}
	soup_server_register (ssl_server, NULL, NULL, server_callback, NULL, NULL);

	printf ("\nStarting Server on port %d\n",
		soup_server_get_port (server));
	soup_server_run_async (server);

	printf ("Starting SSL Server on port %d\n", 
		soup_server_get_port (ssl_server));
	soup_server_run_async (ssl_server);

	printf ("\nWaiting for requests...\n");

	loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (loop);

	return 0;
}
