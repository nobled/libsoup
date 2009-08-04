/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Igalia S.L.
 */
#include <stdlib.h>

#include <libsoup/soup.h>

#include "test-utils.h"

SoupURI *base_uri;
SoupSession *session;
gboolean server_returned_data = FALSE;
GMainLoop *loop;
char *reply_headers = NULL;
SoupCache *cache;

static void
server_callback (SoupServer *server,
		 SoupMessage *msg,
		 const char *path,
		 GHashTable *query,
		 SoupClientContext *context,
		 gpointer data)
{
	GError *error = NULL;
	char *contents = NULL;
	gsize length = 0;

	if (g_str_has_prefix (path, "/plain/")) {
		char *base_name = g_path_get_basename (path);
		char *file_name = g_strdup_printf (SRCDIR "/resources/%s", base_name);

		g_file_get_contents (file_name,
				     &contents, &length,
				     &error);

		g_free (base_name);
		g_free (file_name);

		if (error) {
			g_error ("%s", error->message);
			g_error_free (error);
			exit (1);
		}
	}

	if (reply_headers) {
		GSList *l;
		GSList *list = soup_header_parse_list (reply_headers);
		for (l = list; l; l = l->next) {
			char **result;

			result = g_strsplit (l->data, ":", -1);
			soup_message_headers_append (msg->response_headers,
						     result[0], result[1]);
			g_free (l->data);
			g_strfreev (result);
		}
		g_slist_free (list);
	}

	if (contents && length)
		soup_message_body_append (msg->response_body,
					  SOUP_MEMORY_COPY,
					  contents,
					  length);

	g_free (contents);
	server_returned_data = TRUE;
	soup_message_set_status (msg, SOUP_STATUS_OK);
	soup_message_body_complete (msg->response_body);
}

static void
finished (SoupSession *session, SoupMessage *msg, gpointer data)
{
	soup_cache_flush (cache);

	if (g_main_loop_is_running (loop))
		g_main_loop_quit (loop);
}

static void
test_caching (const char *path,
	      const char *request_headers)
{
	SoupURI *uri = soup_uri_new_with_base (base_uri, path);
	SoupMessage *msg = soup_message_new_from_uri ("GET", uri);
	loop = g_main_loop_new (NULL, TRUE);

	/* Reset state variable */
	server_returned_data = FALSE;

	g_object_ref (msg);
	soup_session_queue_message (session, msg, finished, loop);

	g_main_loop_run (loop);
	soup_uri_free (uri);
	g_object_unref (msg);
	g_main_loop_unref (loop);
	loop = NULL;
}

int
main (int argc, char **argv)
{
	SoupServer *server;

	test_init (argc, argv, NULL);

	server = soup_test_server_new (TRUE);
	soup_server_add_handler (server, NULL, server_callback, NULL, NULL);
	base_uri = soup_uri_new ("http://127.0.0.1/");
	soup_uri_set_port (base_uri, soup_server_get_port (server));

	session = soup_test_session_new (SOUP_TYPE_SESSION_ASYNC, NULL);
        cache = soup_cache_new ("/tmp/");
        soup_session_add_feature (session, SOUP_SESSION_FEATURE (cache));

        /* 1. Test that an image with max-age=0 is not cached */

	/* Get it once so that it hits the cache */
	reply_headers = "Cache-Control: max-age=0";
	test_caching ("/plain/home.gif", NULL);
	g_assert (server_returned_data == TRUE);

	/* Get it again, we should hit the server because max-age was 0*/
	test_caching ("/plain/home.gif", NULL);
	g_assert (server_returned_data == TRUE);

	soup_cache_clear (cache);

        /* 2. Test that an image with max-age=100 is properly cached */

	/* Get it once so that it hits the cache */
	reply_headers = "Cache-Control: max-age=100";
	test_caching ("/plain/home.gif", NULL);
	g_assert (server_returned_data == TRUE);

	/* Get it again, this time we should not hit the server because max-age was 100 */
	test_caching ("/plain/home.gif", NULL);
	g_assert (server_returned_data == FALSE);

	soup_cache_clear (cache);

	/* 3. Test that Cache-Control: no-cache works */

	reply_headers = "Cache-Control: no-cache; max-age=100";
	test_caching ("/plain/home.gif", NULL);
	g_assert (server_returned_data == TRUE);

	test_caching ("/plain/home.gif", NULL);
	g_assert (server_returned_data == TRUE);

	soup_cache_clear (cache);

	/* */

	test_cleanup ();
	return errors != 0;
}

