/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-server-cgi.c: Asynchronous CGI server
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "soup-server-cgi.h"
#include "soup-server-message.h"
#include "soup-private.h"

extern char **environ;

struct _SoupServerCGIPrivate {
	GIOChannel *read, *write;

	SoupMessage *msg;
	int nread;
};

#define PARENT_TYPE SOUP_TYPE_SERVER
static SoupServerClass *parent_class;

static void run_async (SoupServer *server);

static void
init (GObject *object)
{
	SoupServerCGI *cgi = SOUP_SERVER_CGI (object);

	cgi->priv = g_new0 (SoupServerCGIPrivate, 1);
}

static void
finalize (GObject *object)
{
	SoupServerCGI *cgi = SOUP_SERVER_CGI (object);

	if (cgi->priv->read)
		g_io_channel_unref (cgi->priv->read);
	if (cgi->priv->write)
		g_io_channel_unref (cgi->priv->write);
	g_free (cgi->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	SoupServerClass *server_class = SOUP_SERVER_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	server_class->run_async = run_async;
	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_server_cgi, SoupServerCGI, class_init, init, PARENT_TYPE)

SoupServer *
soup_server_cgi_new (void)
{
	SoupServerCGI *cgi;
	GIOChannel *read, *write;

	read = g_io_channel_unix_new (STDIN_FILENO);
	if (!read)
		return NULL;

	write = g_io_channel_unix_new (STDOUT_FILENO);
	if (!write) {
		g_io_channel_unref (read);
		return NULL;
	}

	cgi = g_object_new (SOUP_TYPE_SERVER_CGI, NULL);
	cgi->priv->read = read;
	cgi->priv->write = write;

	return (SoupServer *)cgi;
}

static void
sent_response (SoupMessage *msg, gpointer server)
{
	soup_server_quit (server);
}

static void
read_done (SoupServerCGI *cgi)
{
	soup_server_handle_request (SOUP_SERVER (cgi), cgi->priv->msg,
				    getenv ("PATH_INFO"));
	soup_server_message_respond (cgi->priv->msg, cgi->priv->write,
				     sent_response, cgi);
}

static gboolean
read_watch (GIOChannel    *chan,
	    GIOCondition   condition, 
	    gpointer       user_data)
{
	SoupServerCGI *cgi = user_data;
	SoupMessage *msg = cgi->priv->msg;

	if ((condition & G_IO_IN) && cgi->priv->nread < msg->request.length) {
		gsize nread;
		GIOError error;

		error = g_io_channel_read (
			chan, msg->request.body + cgi->priv->nread,
			msg->request.length - cgi->priv->nread, &nread);
		if (error == G_IO_ERROR_AGAIN)
			return TRUE;
		else if (error == G_IO_ERROR_NONE) {
			cgi->priv->nread += nread;

			if (cgi->priv->nread < msg->request.length)
				return TRUE;
		}
	}

	if (cgi->priv->nread != msg->request.length) {
		soup_message_set_error (msg, SOUP_ERROR_BAD_REQUEST);
		soup_server_message_respond (msg, cgi->priv->write,
					     sent_response, cgi);
		soup_server_message_free (msg);
		return FALSE;
	}

	read_done (cgi);
	return FALSE;
}

static void
run_async (SoupServer *serv)
{
	SoupServerCGI *cgi = SOUP_SERVER_CGI (serv);
	SoupMessage *msg;
	SoupContext *ctx;
	const char *length, *proto, *host, *https;
	int i;
	char *url;

	/* FIXME: Use REMOTE_HOST */
	msg = cgi->priv->msg =
		soup_server_message_new (NULL, SOUP_SERVER_MESSAGE_CGI);

	msg->method = g_strdup (getenv ("REQUEST_METHOD"));

	proto = getenv ("SERVER_PROTOCOL");
	if (proto) {
		if (!g_strcasecmp (proto, "HTTP/1.1"))
			msg->priv->http_version = SOUP_HTTP_1_1;
		else
			msg->priv->http_version = SOUP_HTTP_1_0;
	} else
		msg->priv->http_version = SOUP_HTTP_1_0;

	/* Set context */
	host = getenv ("HTTP_HOST");
	if (!host)
		host = getenv ("SERVER_ADDR");

	https = getenv ("HTTPS");

	url = g_strconcat (https ? "https://" : "http://",
			   host,
			   ":",
			   getenv ("SERVER_PORT"),
			   getenv ("REQUEST_URI"),
			   NULL);
	ctx = soup_context_get (url);
	g_free (url);

	if (!ctx) {
		soup_server_quit (serv);
		soup_server_message_free (msg);
		return;
	}

	soup_message_set_context (msg, ctx);
	soup_context_unref (ctx);

	/* Load request headers from environment. Header environment
	 * variables are of the form "HTTP_<NAME>=<VALUE>"
	 */
	for (i = 0; environ[i]; i++) {
		char *env = environ [i];

		if (!strncmp (env, "HTTP_", 5)) {
			char *dup, *p;

			dup = p = g_strdup (env + 5);

			/* Replace '_' with '-' in header names */
			while (*p && *p != '=') {
				if (*p == '_')
					*p = '-';
				p++;
			}

			if (*dup && *p) {
				/* Skip '=' between key and value */
				*p++ = '\0';

				soup_message_add_header (msg->request_headers,
							 dup, p);
			}
			g_free (dup);
		}
	}

	length = getenv ("CONTENT_LENGTH");
	msg->request.owner = SOUP_BUFFER_SYSTEM_OWNED;
	msg->request.length = length ? atoi (length) : 0;
	msg->request.body = g_malloc (msg->request.length);
	cgi->priv->nread = 0;

	if (msg->request.length > 0) {
		g_io_add_watch (cgi->priv->read,
				G_IO_IN|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
				read_watch, cgi);
	} else
		read_done (cgi);
}
