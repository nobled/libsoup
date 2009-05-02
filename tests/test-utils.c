#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "test-utils.h"
#include "libsoup/soup.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_APACHE
static gboolean apache_running;
#endif
static SoupServer *test_server;
static GThread *server_thread;
static void test_server_shutdown (void);

static SoupLogger *logger;

int debug_level, errors;
gboolean expect_warning;
static int http_debug_level;

static gboolean
increment_debug_level (const char *option_name, const char *value,
		       gpointer data, GError **error)
{
	debug_level++;
	return TRUE;
}

static gboolean
increment_http_debug_level (const char *option_name, const char *value,
			    gpointer data, GError **error)
{
	http_debug_level++;
	return TRUE;
}

static GOptionEntry debug_entry[] = {
	{ "debug", 'd', G_OPTION_FLAG_NO_ARG,
	  G_OPTION_ARG_CALLBACK, increment_debug_level,
	  "Enable (or increase) test-specific debugging", NULL },
	{ "http-debug", 'h', G_OPTION_FLAG_NO_ARG,
	  G_OPTION_ARG_CALLBACK, increment_http_debug_level,
	  "Enable (or increase) HTTP-level debugging", NULL },
	{ NULL }
};

static void
quit (int sig)
{
#ifdef HAVE_APACHE
	if (apache_running)
		apache_cleanup ();
#endif
	if (test_server)
		test_server_shutdown ();

	exit (1);
}

static void
test_log_handler (const char *log_domain, GLogLevelFlags log_level,
		  const char *message, gpointer user_data)
{
	if (log_level & (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL)) {
		if (expect_warning) {
			expect_warning = FALSE;
			debug_printf (2, "Got expected warning: %s\n", message);
			return;
		} else
			errors++;
	}
	g_log_default_handler (log_domain, log_level, message, user_data);
}

void
test_init (int argc, char **argv, GOptionEntry *entries)
{
	GOptionContext *opts;
	char *name;
	GError *error = NULL;

	g_type_init ();

	name = strrchr (argv[0], '/');
	if (!name++)
		name = argv[0];
	if (!strncmp (name, "lt-", 3))
		name += 3;
	g_set_prgname (name);

	opts = g_option_context_new (NULL);
	g_option_context_add_main_entries (opts, debug_entry, NULL);
	if (entries)
		g_option_context_add_main_entries (opts, entries, NULL);

	if (!g_option_context_parse (opts, &argc, &argv, &error)) {
		fprintf (stderr, "Could not parse arguments: %s\n",
			 error->message);
		fprintf (stderr, "%s",
			 g_option_context_get_help (opts, TRUE, NULL));
		exit (1);
	}
	g_option_context_free (opts);

	/* Exit cleanly on ^C in case we're valgrinding. */
	signal (SIGINT, quit);

	g_log_set_default_handler (test_log_handler, NULL);
}

void
test_cleanup (void)
{
#ifdef HAVE_APACHE
	if (apache_running)
		apache_cleanup ();
#endif
	if (test_server)
		test_server_shutdown ();

	if (logger)
		g_object_unref (logger);

	g_main_context_unref (g_main_context_default ());

	debug_printf (1, "\n");
	if (errors) {
		printf ("%s: %d error(s).%s\n",
			g_get_prgname (), errors,
			debug_level == 0 ? " Run with '-d' for details" : "");
	} else
		printf ("%s: OK\n", g_get_prgname ());
}

void
debug_printf (int level, const char *format, ...)
{
	va_list args;

	if (debug_level < level)
		return;

	va_start (args, format);
	vprintf (format, args);
	va_end (args);
}

#ifdef HAVE_APACHE

static gboolean
apache_cmd (const char *cmd)
{
	const char *argv[8];
	char *cwd, *conf;
	int status;
	gboolean ok;

	cwd = g_get_current_dir ();
	conf = g_build_filename (cwd, "httpd.conf", NULL);

	argv[0] = APACHE_HTTPD;
	argv[1] = "-d";
	argv[2] = cwd;
	argv[3] = "-f";
	argv[4] = conf;
	argv[5] = "-k";
	argv[6] = cmd;
	argv[7] = NULL;

	ok = g_spawn_sync (cwd, (char **)argv, NULL, 0, NULL, NULL,
			   NULL, NULL, &status, NULL);
	if (ok)
		ok = (status == 0);

	g_free (cwd);
	g_free (conf);

	return ok;
}

void
apache_init (void)
{
	if (!apache_cmd ("start")) {
		fprintf (stderr, "Could not start apache\n");
		exit (1);
	}
	apache_running = TRUE;
}

void
apache_cleanup (void)
{
	pid_t pid;
	char *contents;

	if (g_file_get_contents ("httpd.pid", &contents, NULL, NULL)) {
		pid = strtoul (contents, NULL, 10);
		g_free (contents);
	} else
		pid = 0;

	if (!apache_cmd ("graceful-stop"))
		return;
	apache_running = FALSE;

	if (pid) {
		while (kill (pid, 0) == 0)
			g_usleep (100);
	}
}

#endif /* HAVE_APACHE */

SoupSession *
soup_test_session_new (GType type, ...)
{
	va_list args;
	const char *propname;
	SoupSession *session;

	va_start (args, type);
	propname = va_arg (args, const char *);
	session = (SoupSession *)g_object_new_valist (type, propname, args);
	va_end (args);

	if (http_debug_level && !logger) {
		SoupLoggerLogLevel level = MIN ((SoupLoggerLogLevel)http_debug_level, SOUP_LOGGER_LOG_BODY);

		logger = soup_logger_new (level, -1);
	}

	if (logger)
		soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));

	return session;
}

void
soup_test_session_abort_unref (SoupSession *session)
{
	g_object_add_weak_pointer (G_OBJECT (session), (gpointer *)&session);

	soup_session_abort (session);
	g_object_unref (session);

	if (session) {
		errors++;
		debug_printf (1, "leaked SoupSession!\n");
		g_object_remove_weak_pointer (G_OBJECT (session), (gpointer *)&session);
	}
}

static gpointer run_server_thread (gpointer user_data);

static SoupServer *
test_server_new (gboolean in_own_thread, gboolean ssl)
{
	GMainContext *async_context;
	const char *ssl_cert_file, *ssl_key_file;

	if (test_server)
		test_server_shutdown ();

	async_context = in_own_thread ? g_main_context_new () : NULL;

	if (ssl) {
		ssl_cert_file = SRCDIR "/test-cert.pem";
		ssl_key_file = SRCDIR "/test-key.pem";
	} else
		ssl_cert_file = ssl_key_file = NULL;

	test_server = soup_server_new (SOUP_SERVER_ASYNC_CONTEXT, async_context,
				       SOUP_SERVER_SSL_CERT_FILE, ssl_cert_file,
				       SOUP_SERVER_SSL_KEY_FILE, ssl_key_file,
				       NULL);
	if (async_context)
		g_main_context_unref (async_context);

	if (!test_server) {
		fprintf (stderr, "Unable to create server\n");
		exit (1);
	}

	if (in_own_thread) {
		server_thread = g_thread_create (run_server_thread, test_server,
						 TRUE, NULL);
	} else
		soup_server_run_async (test_server);

	return test_server;
}

SoupServer *
soup_test_server_new (gboolean in_own_thread)
{
	return test_server_new (in_own_thread, FALSE);
}

SoupServer *
soup_test_server_new_ssl (gboolean in_own_thread)
{
	return test_server_new (in_own_thread, TRUE);
}

static gpointer
run_server_thread (gpointer user_data)
{
	SoupServer *server = user_data;

	soup_server_run (server);
	return NULL;
}

static gboolean
idle_quit_server (gpointer server)
{
	soup_server_quit (server);
	return FALSE;
}

static void
test_server_shutdown (void)
{
	g_object_add_weak_pointer (G_OBJECT (test_server),
				   (gpointer *)&test_server);

	if (server_thread) {
		soup_add_completion (soup_server_get_async_context (test_server),
				     idle_quit_server, test_server);
		g_thread_join (server_thread);
	} else
		soup_server_quit (test_server);
	g_object_unref (test_server);

	if (test_server) {
		errors++;
		debug_printf (1, "leaked SoupServer!\n");
		g_object_remove_weak_pointer (G_OBJECT (test_server),
					      (gpointer *)&test_server);
	}
}
