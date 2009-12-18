/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_GNOME
#include <libsoup/soup-gnome.h>
#else
#include <libsoup/soup.h>
#endif

static SoupSession *session;
static gboolean debug = FALSE, quiet = FALSE;

static void
get_url (SoupURI *uri)
{
	SoupRequest *req;
	GInputStream *istream;
	GError *error = NULL;
	char buf[8192];
	gsize nread;

	req = soup_session_request_uri (session, uri, &error);
	if (!req) {
		fprintf (stderr, "Could not request URI: %s\n",
			 error->message);
		exit (1);
	}

	istream = soup_request_send (req, NULL, &error);
	if (!istream) {
		fprintf (stderr, "Could not send URI: %s\n",
			 error->message);
		exit (1);
	}

	while ((nread = g_input_stream_read (istream, buf, sizeof (buf),
					     NULL, &error)) > 0)
		fwrite (buf, 1, nread, stdout);

	if (error) {
		fprintf (stderr, "Read failed: %s\n", error->message);
		exit (1);
	}
}

static void
usage (void)
{
	fprintf (stderr, "Usage: get [-p proxy] [-d] URL\n");
	exit (1);
}

int
main (int argc, char **argv)
{
	const char *url;
	SoupURI *proxy = NULL, *parsed;
	int opt;

	g_thread_init (NULL);
	g_type_init ();

	while ((opt = getopt (argc, argv, "dp:qs")) != -1) {
		switch (opt) {
		case 'd':
			debug = TRUE;
			break;

		case 'p':
			proxy = soup_uri_new (optarg);
			if (!proxy) {
				fprintf (stderr, "Could not parse %s as URI\n",
					 optarg);
				exit (1);
			}
			break;

		case 'q':
			quiet = TRUE;
			break;

		case '?':
			usage ();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage ();
	url = argv[0];
	parsed = soup_uri_new (url);
	if (!parsed) {
		fprintf (stderr, "Could not parse '%s' as a URL\n", url);
		exit (1);
	}

	session = soup_session_sync_new_with_options (
#ifdef HAVE_GNOME
		SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_GNOME_FEATURES_2_26,
#endif
		SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_CONTENT_DECODER,
		SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_COOKIE_JAR,
		SOUP_SESSION_USER_AGENT, "get ",
		SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
		NULL);

	/* Need to do this after creating the session, since adding
	 * SOUP_TYPE_GNOME_FEATURE_2_26 will add a proxy resolver, thereby
	 * bashing over the manually-set proxy.
	 */
	if (proxy) {
		g_object_set (G_OBJECT (session), 
			      SOUP_SESSION_PROXY_URI, proxy,
			      NULL);
	}

	get_url (parsed);
	soup_uri_free (parsed);
	return 0;
}
