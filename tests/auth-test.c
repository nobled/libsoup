#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libsoup/soup.h"
#include "libsoup/soup-auth.h"
#include "libsoup/soup-session.h"

#include "test-utils.h"

static GMainLoop *loop;

typedef struct {
	/* Explanation of what you should see */
	const char *explanation;

	/* URL to test against */
	const char *url;

	/* Provided passwords, 1 character each. ('1', '2', and '3'
	 * mean the correct passwords for "realm1", "realm2", and
	 * "realm3" respectively. '4' means "use the wrong password".)
	 * The first password (if present) will be used by
	 * authenticate(), and the second (if present) will be used by
	 * reauthenticate().
	 */
	const char *provided;

	/* Expected passwords, 1 character each. (As with the provided
	 * passwords, with the addition that '0' means "no
	 * Authorization header expected".) Used to verify that soup
	 * used the password it was supposed to at each step.
	 */
	const char *expected;

	/* What the final status code should be. */
	guint final_status;
} SoupAuthTest;

static SoupAuthTest tests[] = {
	{ "No auth available, should fail",
	  "Basic/realm1/", "", "0", SOUP_STATUS_UNAUTHORIZED },

	{ "Should fail with no auth, fail again with bad password, and give up",
	  "Basic/realm2/", "4", "04", SOUP_STATUS_UNAUTHORIZED },

	{ "Auth provided this time, so should succeed",
	  "Basic/realm1/", "1", "01", SOUP_STATUS_OK },

	{ "Now should automatically reuse previous auth",
	  "Basic/realm1/", "", "1", SOUP_STATUS_OK },

	{ "Subdir should also automatically reuse auth",
	  "Basic/realm1/subdir/", "", "1", SOUP_STATUS_OK },

	{ "Subdir should retry last auth, but will fail this time",
	  "Basic/realm1/realm2/", "", "1", SOUP_STATUS_UNAUTHORIZED },

	{ "Now should use provided auth",
	  "Basic/realm1/realm2/", "2", "02", SOUP_STATUS_OK },

	{ "Reusing last auth. Should succeed on first try",
	  "Basic/realm1/realm2/", "", "2", SOUP_STATUS_OK },

	{ "Reuse will fail, but 2nd try will succeed because it's a known realm",
	  "Basic/realm1/realm2/realm1/", "", "21", SOUP_STATUS_OK },

	{ "Should succeed on first try. (Known realm with cached password)",
	  "Basic/realm2/", "", "2", SOUP_STATUS_OK },

	{ "Fail once, then use typoed password, then use right password",
	  "Basic/realm3/", "43", "043", SOUP_STATUS_OK },


	{ "No auth available, should fail",
	  "Digest/realm1/", "", "0", SOUP_STATUS_UNAUTHORIZED },

	{ "Should fail with no auth, fail again with bad password, and give up",
	  "Digest/realm2/", "4", "04", SOUP_STATUS_UNAUTHORIZED },

	{ "Known realm, auth provided, so should succeed",
	  "Digest/realm1/", "1", "01", SOUP_STATUS_OK },

	{ "Now should automatically reuse previous auth",
	  "Digest/realm1/", "", "1", SOUP_STATUS_OK },

	{ "Subdir should also automatically reuse auth",
	  "Digest/realm1/subdir/", "", "1", SOUP_STATUS_OK },

	{ "Password provided, should succeed",
	  "Digest/realm2/", "2", "02", SOUP_STATUS_OK },

	{ "Should already know correct domain and use provided auth on first try",
	  "Digest/realm1/realm2/", "2", "2", SOUP_STATUS_OK },

	{ "Reusing last auth. Should succeed on first try",
	  "Digest/realm1/realm2/", "", "2", SOUP_STATUS_OK },

	{ "Should succeed on first try because of earlier domain directive",
	  "Digest/realm1/realm2/realm1/", "", "1", SOUP_STATUS_OK },

	{ "Fail once, then use typoed password, then use right password",
	  "Digest/realm3/", "43", "043", SOUP_STATUS_OK },


	{ "Make sure we haven't forgotten anything",
	  "Basic/realm1/", "", "1", SOUP_STATUS_OK },

	{ "Make sure we haven't forgotten anything",
	  "Basic/realm1/realm2/", "", "2", SOUP_STATUS_OK },

	{ "Make sure we haven't forgotten anything",
	  "Basic/realm1/realm2/realm1/", "", "1", SOUP_STATUS_OK },

	{ "Make sure we haven't forgotten anything",
	  "Basic/realm2/", "", "2", SOUP_STATUS_OK },

	{ "Make sure we haven't forgotten anything",
	  "Basic/realm3/", "", "3", SOUP_STATUS_OK },


	{ "Make sure we haven't forgotten anything",
	  "Digest/realm1/", "", "1", SOUP_STATUS_OK },

	{ "Make sure we haven't forgotten anything",
	  "Digest/realm1/realm2/", "", "2", SOUP_STATUS_OK },

	{ "Make sure we haven't forgotten anything",
	  "Digest/realm1/realm2/realm1/", "", "1", SOUP_STATUS_OK },

	{ "Make sure we haven't forgotten anything",
	  "Digest/realm2/", "", "2", SOUP_STATUS_OK },

	{ "Make sure we haven't forgotten anything",
	  "Digest/realm3/", "", "3", SOUP_STATUS_OK },

	{ "Now the server will reject the formerly-good password",
	  "Basic/realm1/not/", "1" /* should not be used */, "1", SOUP_STATUS_UNAUTHORIZED },

	{ "Make sure we've forgotten it",
	  "Basic/realm1/", "", "0", SOUP_STATUS_UNAUTHORIZED },

	{ "Likewise, reject the formerly-good Digest password",
	  "Digest/realm1/not/", "1" /* should not be used */, "1", SOUP_STATUS_UNAUTHORIZED },

	{ "Make sure we've forgotten it",
	  "Digest/realm1/", "", "0", SOUP_STATUS_UNAUTHORIZED }
};
static int ntests = sizeof (tests) / sizeof (tests[0]);

static const char *auths[] = {
	"no password", "password 1",
	"password 2", "password 3",
	"intentionally wrong password",
};

static int
identify_auth (SoupMessage *msg)
{
	const char *header;
	int num;

	header = soup_message_headers_get_one (msg->request_headers,
					       "Authorization");
	if (!header)
		return 0;

	if (!g_ascii_strncasecmp (header, "Basic ", 6)) {
		char *token;
		gsize len;

		token = (char *)g_base64_decode (header + 6, &len);
		num = token[len - 1] - '0';
		g_free (token);
	} else {
		const char *user;

		user = strstr (header, "username=\"user");
		if (user)
			num = user[14] - '0';
		else
			num = 0;
	}

	g_assert (num >= 0 && num <= 4);

	return num;
}

static void
handler (SoupMessage *msg, gpointer data)
{
	char *expected = data;
	int auth, exp;

	auth = identify_auth (msg);

	debug_printf (1, "  %d %s (using %s)\n",
		      msg->status_code, msg->reason_phrase,
		      auths[auth]);

	if (*expected) {
		exp = *expected - '0';
		if (auth != exp) {
			debug_printf (1, "    expected %s!\n", auths[exp]);
			errors++;
		}
		memmove (expected, expected + 1, strlen (expected));
	} else {
		debug_printf (1, "    expected to be finished\n");
		errors++;
	}
}

static void
authenticate (SoupSession *session, SoupMessage *msg,
	      SoupAuth *auth, gboolean retrying, gpointer data)
{
	int *i = data;
	char *username, *password;
	char num;

	if (!tests[*i].provided[0])
		return;
	if (retrying) {
		if (!tests[*i].provided[1])
			return;
		num = tests[*i].provided[1];
	} else
		num = tests[*i].provided[0];

	username = g_strdup_printf ("user%c", num);
	password = g_strdup_printf ("realm%c", num);
	soup_auth_authenticate (auth, username, password);
	g_free (username);
	g_free (password);
}

static void
bug271540_sent (SoupMessage *msg, gpointer data)
{
	int n = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (msg), "#"));
	gboolean *authenticated = data;
	int auth = identify_auth (msg);

	if (!*authenticated && auth) {
		debug_printf (1, "    using auth on message %d before authenticating!!??\n", n);
		errors++;
	} else if (*authenticated && !auth) {
		debug_printf (1, "    sent unauthenticated message %d after authenticating!\n", n);
		errors++;
	}
}

static void
bug271540_authenticate (SoupSession *session, SoupMessage *msg,
			SoupAuth *auth, gboolean retrying, gpointer data)
{
	int n = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (msg), "#"));
	gboolean *authenticated = data;

	if (strcmp (soup_auth_get_scheme_name (auth), "Basic") != 0 ||
	    strcmp (soup_auth_get_realm (auth), "realm1") != 0)
		return;

	if (!*authenticated) {
		debug_printf (1, "    authenticating message %d\n", n);
		soup_auth_authenticate (auth, "user1", "realm1");
		*authenticated = TRUE;
	} else {
		debug_printf (1, "    asked to authenticate message %d after authenticating!\n", n);
		errors++;
	}
}

static void
bug271540_finished (SoupSession *session, SoupMessage *msg, gpointer data)
{
	int *left = data;
	int n = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (msg), "#"));

	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		debug_printf (1, "      got status '%d %s' on message %d!\n",
			      msg->status_code, msg->reason_phrase, n);
		errors++;
	}

	(*left)--;
	if (!*left)
		g_main_loop_quit (loop);
}

static void
digest_nonce_authenticate (SoupSession *session, SoupMessage *msg,
			   SoupAuth *auth, gboolean retrying, gpointer data)
{
	if (retrying)
		return;

	if (strcmp (soup_auth_get_scheme_name (auth), "Digest") != 0 ||
	    strcmp (soup_auth_get_realm (auth), "realm1") != 0)
		return;

	soup_auth_authenticate (auth, "user1", "realm1");
}

static void
digest_nonce_unauthorized (SoupMessage *msg, gpointer data)
{
	gboolean *got_401 = data;
	*got_401 = TRUE;
}

static void
do_digest_nonce_test (SoupSession *session,
		      const char *nth, const char *uri,
		      gboolean expect_401, gboolean expect_signal)
{
	SoupMessage *msg;
	gboolean got_401;

	msg = soup_message_new (SOUP_METHOD_GET, uri);
	if (expect_signal) {
		g_signal_connect (session, "authenticate",
				  G_CALLBACK (digest_nonce_authenticate),
				  NULL);
	}
	soup_message_add_status_code_handler (msg, "got_headers",
					      SOUP_STATUS_UNAUTHORIZED,
					      G_CALLBACK (digest_nonce_unauthorized),
					      &got_401);
	got_401 = FALSE;
	soup_session_send_message (session, msg);
	if (got_401 != expect_401) {
		debug_printf (1, "  %s request %s a 401 Unauthorized!\n", nth,
			      got_401 ? "got" : "did not get");
		errors++;
	}
	if (msg->status_code != SOUP_STATUS_OK) {
		debug_printf (1, "  %s request got status %d %s!\n", nth,
			      msg->status_code, msg->reason_phrase);
		errors++;
	}
	if (errors == 0)
		debug_printf (1, "  %s request succeeded\n", nth);
	g_object_unref (msg);
}

/* Async auth test. We queue three requests to /Basic/realm1, ensuring
 * that they are sent in order. The first and third ones will be
 * paused from the authentication callback. The second will be allowed
 * to fail. Shortly after the third one requests auth, we'll provide
 * the auth and unpause the two remaining messages, allowing them to
 * succeed.
 */

static void
async_authenticate (SoupSession *session, SoupMessage *msg,
		    SoupAuth *auth, gboolean retrying, gpointer data)
{
	SoupAuth **saved_auth = data;
	int id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (msg), "id"));

	debug_printf (2, "  async_authenticate msg%d\n", id);

	/* The session will try to authenticate msg3 *before* sending
	 * it, because it already knows it's going to need the auth.
	 * Ignore that.
	 */
	if (msg->status_code != SOUP_STATUS_UNAUTHORIZED) {
		debug_printf (2, "    (ignoring)\n");
		return;
	}

	soup_session_pause_message (session, msg);
	if (saved_auth)
		*saved_auth = g_object_ref (auth);
	g_main_loop_quit (loop);
}

static void
async_finished (SoupSession *session, SoupMessage *msg, gpointer user_data)
{
	int *finished = user_data;
	int id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (msg), "id"));

	debug_printf (2, "  async_finished msg%d\n", id);

	(*finished)++;
	if (*finished == 2)
		g_main_loop_quit (loop);
}

static void
async_authenticate_assert_once (SoupSession *session, SoupMessage *msg,
                                SoupAuth *auth, gboolean retrying, gpointer data)
{
	gboolean *been_here = data;

	debug_printf (2, "  async_authenticate_assert_once\n");

	if (*been_here) {
		debug_printf (1, "  ERROR: async_authenticate_assert_once called twice\n");
		errors++;
	}
	*been_here = TRUE;

	soup_session_pause_message (session, msg);
	g_main_loop_quit (loop);
}

static void
do_async_auth_test (const char *base_uri)
{
	SoupSession *session;
	SoupMessage *msg1, *msg2, *msg3, msg2_bak;
	guint auth_id;
	char *uri;
	SoupAuth *auth = NULL;
	int finished = 0;
	gboolean been_there = FALSE;

	debug_printf (1, "\nTesting async auth:\n");

	session = soup_test_session_new (SOUP_TYPE_SESSION_ASYNC, NULL);

	uri = g_strconcat (base_uri, "Basic/realm1/", NULL);

	msg1 = soup_message_new ("GET", uri);
	g_object_set_data (G_OBJECT (msg1), "id", GINT_TO_POINTER (1));
	auth_id = g_signal_connect (session, "authenticate",
				    G_CALLBACK (async_authenticate), &auth);
	g_object_ref (msg1);
	soup_session_queue_message (session, msg1, async_finished, &finished);
	g_main_loop_run (loop);
	g_signal_handler_disconnect (session, auth_id);

	/* async_authenticate will pause msg1 and quit loop */

	msg2 = soup_message_new ("GET", uri);
	g_object_set_data (G_OBJECT (msg2), "id", GINT_TO_POINTER (2));
	soup_session_send_message (session, msg2);

	if (msg2->status_code == SOUP_STATUS_UNAUTHORIZED)
		debug_printf (1, "  msg2 failed as expected\n");
	else {
		debug_printf (1, "  msg2 got wrong status! (%u)\n",
			      msg2->status_code);
		errors++;
	}

	/* msg2 should be done at this point; assuming everything is
	 * working correctly, the session won't look at it again; we
	 * ensure that if it does, it will crash the test program.
	 */
	memcpy (&msg2_bak, msg2, sizeof (SoupMessage));
	memset (msg2, 0, sizeof (SoupMessage));

	msg3 = soup_message_new ("GET", uri);
	g_object_set_data (G_OBJECT (msg3), "id", GINT_TO_POINTER (3));
	auth_id = g_signal_connect (session, "authenticate",
				    G_CALLBACK (async_authenticate), NULL);
	g_object_ref (msg3);
	soup_session_queue_message (session, msg3, async_finished, &finished);
	g_main_loop_run (loop);
	g_signal_handler_disconnect (session, auth_id);

	/* async_authenticate will pause msg3 and quit loop */

	/* Now do the auth, and restart */
	if (auth) {
		soup_auth_authenticate (auth, "user1", "realm1");
		g_object_unref (auth);
		soup_session_unpause_message (session, msg1);
		soup_session_unpause_message (session, msg3);

		g_main_loop_run (loop);

		/* async_finished will quit the loop */
	} else {
		debug_printf (1, "  msg1 didn't get authenticate signal!\n");
		errors++;
	}

	if (msg1->status_code == SOUP_STATUS_OK)
		debug_printf (1, "  msg1 succeeded\n");
	else {
		debug_printf (1, "  msg1 FAILED! (%u %s)\n",
			      msg1->status_code, msg1->reason_phrase);
		errors++;
	}
	if (msg3->status_code == SOUP_STATUS_OK)
		debug_printf (1, "  msg3 succeeded\n");
	else {
		debug_printf (1, "  msg3 FAILED! (%u %s)\n",
			      msg3->status_code, msg3->reason_phrase);
		errors++;
	}

	soup_test_session_abort_unref (session);

	g_object_unref (msg1);
	g_object_unref (msg3);
	memcpy (msg2, &msg2_bak, sizeof (SoupMessage));
	g_object_unref (msg2);

	/* Test that giving the wrong password doesn't cause multiple
	 * authenticate signals the second time.
	 */
	debug_printf (1, "\nTesting async auth with wrong password (#522601):\n");

	session = soup_test_session_new (SOUP_TYPE_SESSION_ASYNC, NULL);
	auth = NULL;

	msg1 = soup_message_new ("GET", uri);
	g_object_set_data (G_OBJECT (msg1), "id", GINT_TO_POINTER (1));
	auth_id = g_signal_connect (session, "authenticate",
				    G_CALLBACK (async_authenticate), &auth);
	g_object_ref (msg1);
	soup_session_queue_message (session, msg1, async_finished, &finished);
	g_main_loop_run (loop);
	g_signal_handler_disconnect (session, auth_id);
	soup_auth_authenticate (auth, "user1", "wrong");
	g_object_unref (auth);
	soup_session_unpause_message (session, msg1);

	auth_id = g_signal_connect (session, "authenticate",
				    G_CALLBACK (async_authenticate_assert_once),
				    &been_there);
	g_main_loop_run (loop);
	g_signal_handler_disconnect (session, auth_id);

	soup_test_session_abort_unref (session);

	g_object_unref (msg1);


	/* Test that giving no password doesn't cause multiple
	 * authenticate signals the second time.
	 */
	debug_printf (1, "\nTesting async auth with no password (#583462):\n");

	/* For this test, our first message will not finish twice */
	finished = 1;
	been_there = FALSE;
	session = soup_test_session_new (SOUP_TYPE_SESSION_ASYNC, NULL);

	/* Send a message that doesn't actually authenticate
	 */
	msg1 = soup_message_new ("GET", uri);
	g_object_set_data (G_OBJECT (msg1), "id", GINT_TO_POINTER (1));
	auth_id = g_signal_connect (session, "authenticate",
				    G_CALLBACK (async_authenticate), NULL);
	g_object_ref (msg1);
	soup_session_queue_message (session, msg1, async_finished, &finished);
	g_main_loop_run (loop);
	g_signal_handler_disconnect (session, auth_id);
	soup_session_unpause_message (session, msg1);
	g_main_loop_run (loop);
	g_object_unref(msg1);

	/* Now send a second message */
	finished = 1;
	msg1 = soup_message_new ("GET", uri);
	g_object_set_data (G_OBJECT (msg1), "id", GINT_TO_POINTER (2));
	g_object_ref (msg1);
	auth_id = g_signal_connect (session, "authenticate",
				    G_CALLBACK (async_authenticate_assert_once),
				    &been_there);
	soup_session_queue_message (session, msg1, async_finished, &finished);
	g_main_loop_run (loop);
	soup_session_unpause_message (session, msg1);

	g_main_loop_run (loop);
	g_signal_handler_disconnect (session, auth_id);

	soup_test_session_abort_unref (session);

	g_object_unref (msg1);

	g_free (uri);
}

typedef struct {
	const char *password;
	struct {
		const char *headers;
		const char *response;
	} round[2];
} SelectAuthData;

static void
select_auth_authenticate (SoupSession *session, SoupMessage *msg,
			  SoupAuth *auth, gboolean retrying, gpointer data)
{
	SelectAuthData *sad = data;
	const char *header, *basic, *digest;
	int round = retrying ? 1 : 0;

	header = soup_message_headers_get_list (msg->response_headers,
						"WWW-Authenticate");
	basic = strstr (header, "Basic");
	digest = strstr (header, "Digest");
	if (basic && digest) {
		if (basic < digest)
			sad->round[round].headers = "Basic, Digest";
		else
			sad->round[round].headers = "Digest, Basic";
	} else if (basic)
		sad->round[round].headers = "Basic";
	else if (digest)
		sad->round[round].headers = "Digest";

	sad->round[round].response = soup_auth_get_scheme_name (auth);
	if (sad->password && !retrying)
		soup_auth_authenticate (auth, "user", sad->password);
}

static void
select_auth_test_one (SoupURI *uri, const char *password,
		      const char *first_headers, const char *first_response,
		      const char *second_headers, const char *second_response,
		      guint final_status)
{
	SelectAuthData sad;
	SoupMessage *msg;
	SoupSession *session;

	session = soup_test_session_new (SOUP_TYPE_SESSION_ASYNC, NULL);
	g_signal_connect (session, "authenticate",
			  G_CALLBACK (select_auth_authenticate), &sad);
	memset (&sad, 0, sizeof (sad));
	sad.password = password;

	msg = soup_message_new_from_uri ("GET", uri);
	soup_session_send_message (session, msg);

	if (strcmp (sad.round[0].headers, first_headers) != 0) {
		debug_printf (1, "    Header order wrong: expected %s, got %s\n",
			      first_headers, sad.round[0].headers);
		errors++;
	}
	if (strcmp (sad.round[0].response, first_response) != 0) {
		debug_printf (1, "    Selected auth type wrong: expected %s, got %s\n",
			      first_response, sad.round[0].response);
		errors++;
	}

	if (second_headers && !sad.round[1].headers) {
		debug_printf (1, "    Expected a second round!\n");
		errors++;
	} else if (!second_headers && sad.round[1].headers) {
		debug_printf (1, "    Didn't expect a second round!\n");
		errors++;
	} else if (second_headers) {
		if (strcmp (sad.round[1].headers, second_headers) != 0) {
			debug_printf (1, "    Second round header order wrong: expected %s, got %s\n",
				      second_headers, sad.round[1].headers);
			errors++;
		}
		if (strcmp (sad.round[1].response, second_response) != 0) {
			debug_printf (1, "    Second round selected auth type wrong: expected %s, got %s\n",
				      second_response, sad.round[1].response);
			errors++;
		}
	}

	if (msg->status_code != final_status) {
		debug_printf (1, "    Final status wrong: expected %u, got %u\n",
			      final_status, msg->status_code);
		errors++;
	}

	g_object_unref (msg);
	soup_test_session_abort_unref (session);
}

static void
server_callback (SoupServer *server, SoupMessage *msg,
		 const char *path, GHashTable *query,
		 SoupClientContext *context, gpointer data)
{
	soup_message_set_response (msg, "text/plain",
				   SOUP_MEMORY_STATIC,
				   "OK\r\n", 4);
	soup_message_set_status (msg, SOUP_STATUS_OK);
}

static gboolean
server_basic_auth_callback (SoupAuthDomain *auth_domain, SoupMessage *msg,
			    const char *username, const char *password, gpointer data)
{
	return FALSE;
}

static char *
server_digest_auth_callback (SoupAuthDomain *auth_domain, SoupMessage *msg,
			     const char *username, gpointer data)
{
	if (strcmp (username, "user") != 0)
		return NULL;
	return soup_auth_domain_digest_encode_password ("user",
							"auth-test",
							"good");
}

static void
do_select_auth_test (void)
{
	SoupServer *server;
	SoupAuthDomain *basic_auth_domain, *digest_auth_domain;
	SoupURI *uri;

	debug_printf (1, "\nTesting selection among multiple auths:\n");

	/* It doesn't seem to be possible to configure Apache to serve
	 * multiple auth types for a single URL. So we have to use
	 * SoupServer here. We know that SoupServer handles the server
	 * side of this scenario correctly, because we test it against
	 * curl in server-auth-test.
	 */
	server = soup_test_server_new (FALSE);
	soup_server_add_handler (server, NULL,
				 server_callback, NULL, NULL);

	uri = soup_uri_new ("http://127.0.0.1/");
	soup_uri_set_port (uri, soup_server_get_port (server));

	basic_auth_domain = soup_auth_domain_basic_new (
		SOUP_AUTH_DOMAIN_REALM, "auth-test",
		SOUP_AUTH_DOMAIN_ADD_PATH, "/",
		SOUP_AUTH_DOMAIN_BASIC_AUTH_CALLBACK, server_basic_auth_callback,
		NULL);
	soup_server_add_auth_domain (server, basic_auth_domain);

	digest_auth_domain = soup_auth_domain_digest_new (
		SOUP_AUTH_DOMAIN_REALM, "auth-test",
		SOUP_AUTH_DOMAIN_ADD_PATH, "/",
		SOUP_AUTH_DOMAIN_DIGEST_AUTH_CALLBACK, server_digest_auth_callback,
		NULL);
	soup_server_add_auth_domain (server, digest_auth_domain);

	/* FIXME: when we support disabling auth types in the session,
	 * test that too.
	 */

	debug_printf (1, "  Testing with no auth\n");
	select_auth_test_one (uri, NULL,
			      "Basic, Digest", "Digest",
			      NULL, NULL,
			      SOUP_STATUS_UNAUTHORIZED);

	debug_printf (1, "  Testing with bad password\n");
	select_auth_test_one (uri, "bad",
			      "Basic, Digest", "Digest",
			      "Basic, Digest", "Digest",
			      SOUP_STATUS_UNAUTHORIZED);

	debug_printf (1, "  Testing with good password\n");
	select_auth_test_one (uri, "good",
			      "Basic, Digest", "Digest",
			      NULL, NULL,
			      SOUP_STATUS_OK);

	/* Now flip the order of the domains, verify that this flips
	 * the order of the headers, and make sure that digest auth
	 * *still* gets used.
	 */

	soup_server_remove_auth_domain (server, basic_auth_domain);
	soup_server_remove_auth_domain (server, digest_auth_domain);
	soup_server_add_auth_domain (server, digest_auth_domain);
	soup_server_add_auth_domain (server, basic_auth_domain);

	debug_printf (1, "  Testing flipped with no auth\n");
	select_auth_test_one (uri, NULL,
			      "Digest, Basic", "Digest",
			      NULL, NULL,
			      SOUP_STATUS_UNAUTHORIZED);

	debug_printf (1, "  Testing flipped with bad password\n");
	select_auth_test_one (uri, "bad",
			      "Digest, Basic", "Digest",
			      "Digest, Basic", "Digest",
			      SOUP_STATUS_UNAUTHORIZED);

	debug_printf (1, "  Testing flipped with good password\n");
	select_auth_test_one (uri, "good",
			      "Digest, Basic", "Digest",
			      NULL, NULL,
			      SOUP_STATUS_OK);
}

int
main (int argc, char **argv)
{
	SoupSession *session;
	SoupMessage *msg;
	const char *base_uri;
	char *uri, *expected;
	gboolean authenticated;
	int i;

	test_init (argc, argv, NULL);
	apache_init ();

	base_uri = "http://127.0.0.1:47524/";

	session = soup_test_session_new (SOUP_TYPE_SESSION_ASYNC, NULL);
	g_signal_connect (session, "authenticate",
			  G_CALLBACK (authenticate), &i);

	for (i = 0; i < ntests; i++) {
		debug_printf (1, "Test %d: %s\n", i + 1, tests[i].explanation);

		uri = g_strconcat (base_uri, tests[i].url, NULL);
		debug_printf (1, "  GET %s\n", uri);

		msg = soup_message_new (SOUP_METHOD_GET, uri);
		g_free (uri);
		if (!msg) {
			fprintf (stderr, "auth-test: Could not parse URI\n");
			exit (1);
		}

		expected = g_strdup (tests[i].expected);
		soup_message_add_status_code_handler (
			msg, "got_headers", SOUP_STATUS_UNAUTHORIZED,
			G_CALLBACK (handler), expected);
		soup_message_add_status_code_handler (
			msg, "got_headers", SOUP_STATUS_OK,
			G_CALLBACK (handler), expected);
		soup_session_send_message (session, msg);
		if (msg->status_code != SOUP_STATUS_UNAUTHORIZED &&
		    msg->status_code != SOUP_STATUS_OK) {
			debug_printf (1, "  %d %s !\n", msg->status_code,
				      msg->reason_phrase);
			errors++;
		}
		if (*expected) {
			debug_printf (1, "  expected %d more round(s)\n",
				      (int)strlen (expected));
			errors++;
		}
		g_free (expected);

		if (msg->status_code != tests[i].final_status) {
			debug_printf (1, "  expected %d\n",
				      tests[i].final_status);
		}

		debug_printf (1, "\n");

		g_object_unref (msg);
	}
	soup_test_session_abort_unref (session);

	/* And now for some regression tests */
	loop = g_main_loop_new (NULL, TRUE);

	debug_printf (1, "Testing pipelined auth (bug 271540):\n");
	session = soup_test_session_new (SOUP_TYPE_SESSION_ASYNC, NULL);

	authenticated = FALSE;
	g_signal_connect (session, "authenticate",
			  G_CALLBACK (bug271540_authenticate), &authenticated);

	uri = g_strconcat (base_uri, "Basic/realm1/", NULL);
	for (i = 0; i < 10; i++) {
		msg = soup_message_new (SOUP_METHOD_GET, uri);
		g_object_set_data (G_OBJECT (msg), "#", GINT_TO_POINTER (i + 1));
		g_signal_connect (msg, "wrote_headers",
				  G_CALLBACK (bug271540_sent), &authenticated);

		soup_session_queue_message (session, msg,
					    bug271540_finished, &i);
	}
	g_free (uri);

	g_main_loop_run (loop);
	soup_test_session_abort_unref (session);

	debug_printf (1, "\nTesting digest nonce expiration:\n");

	/* We test two different things here:
	 *
	 *   1. If we get a 401 response with
	 *      "WWW-Authenticate: Digest stale=true...", we should
	 *      retry and succeed *without* the session asking for a
	 *      password again.
	 *
	 *   2. If we get a successful response with
	 *      "Authentication-Info: nextnonce=...", we should update
	 *      the nonce automatically so as to avoid getting a
	 *      stale nonce error on the next request.
	 *
	 * In our Apache config, /Digest/realm1 and
	 * /Digest/realm1/expire are set up to use the same auth info,
	 * but only the latter has an AuthDigestNonceLifetime (of 2
	 * seconds). The way nonces work in Apache, a nonce received
	 * from /Digest/realm1 will still expire in
	 * /Digest/realm1/expire, but it won't issue a nextnonce for a
	 * request in /Digest/realm1. This lets us test both
	 * behaviors.
	 *
	 * The expected conversation is:
	 *
	 * First message
	 *   GET /Digest/realm1
	 *
	 *   401 Unauthorized
	 *   WWW-Authenticate: Digest nonce=A
	 *
	 *   [emit 'authenticate']
	 *
	 *   GET /Digest/realm1
	 *   Authorization: Digest nonce=A
	 *
	 *   200 OK
	 *   [No Authentication-Info]
	 *
	 * [sleep 2 seconds: nonce A is no longer valid, but we have no
	 * way of knowing that]
	 *
	 * Second message
	 *   GET /Digest/realm1/expire/
	 *   Authorization: Digest nonce=A
	 *
	 *   401 Unauthorized
	 *   WWW-Authenticate: Digest stale=true nonce=B
	 *
	 *   GET /Digest/realm1/expire/
	 *   Authorization: Digest nonce=B
	 *
	 *   200 OK
	 *   Authentication-Info: nextnonce=C
	 *
	 * [sleep 1 second]
	 *
	 * Third message
	 *   GET /Digest/realm1/expire/
	 *   Authorization: Digest nonce=C
	 *   [nonce=B would work here too]
	 *
	 *   200 OK
	 *   Authentication-Info: nextnonce=D
	 *
	 * [sleep 1 second; nonces B and C are no longer valid, but D is]
	 *
	 * Fourth message
	 *   GET /Digest/realm1/expire/
	 *   Authorization: Digest nonce=D
	 *
	 *   200 OK
	 *   Authentication-Info: nextnonce=D
	 *
	 */

	session = soup_test_session_new (SOUP_TYPE_SESSION_ASYNC, NULL);

	uri = g_strconcat (base_uri, "Digest/realm1/", NULL);
	do_digest_nonce_test (session, "First", uri, TRUE, TRUE);
	g_free (uri);
	sleep (2);
	uri = g_strconcat (base_uri, "Digest/realm1/expire/", NULL);
	do_digest_nonce_test (session, "Second", uri, TRUE, FALSE);
	sleep (1);
	do_digest_nonce_test (session, "Third", uri, FALSE, FALSE);
	sleep (1);
	do_digest_nonce_test (session, "Fourth", uri, FALSE, FALSE);
	g_free (uri);

	soup_test_session_abort_unref (session);

	/* Async auth */
	do_async_auth_test (base_uri);

	/* Selecting correct auth when multiple auth types are available */
	do_select_auth_test ();

	g_main_loop_unref (loop);

	test_cleanup ();
	return errors != 0;
}
