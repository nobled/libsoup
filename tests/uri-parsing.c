#include <config.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libsoup/soup-uri.h"

char *base = "http://a/b/c/d;p?q#f";

/* These are from RFC1808 except that I changed the protocol "g" to
 * "https" in the first example, since soup-uri won't handle unknown
 * protocols.
 */

struct {
	char *uri_string, *result;
} tests[] = {
	{ "https:h", "https:h" },
	{ "g", "http://a/b/c/g" },
	{ "./g", "http://a/b/c/g" },
	{ "g/", "http://a/b/c/g/" },
	{ "/g", "http://a/g" },
	{ "//g", "http://g" },
	{ "?y", "http://a/b/c/d;p?y" },
	{ "g?y", "http://a/b/c/g?y" },
	{ "g?y/./x", "http://a/b/c/g?y/./x" },
	{ "#s", "http://a/b/c/d;p?q#s" },
	{ "g#s", "http://a/b/c/g#s" },
	{ "g#s/./x", "http://a/b/c/g#s/./x" },
	{ "g?y#s", "http://a/b/c/g?y#s" },
	{ ";x", "http://a/b/c/d;x" },
	{ "g;x", "http://a/b/c/g;x" },
	{ "g;x?y#s", "http://a/b/c/g;x?y#s" },
	{ ".", "http://a/b/c/" },
	{ "./", "http://a/b/c/" },
	{ "..", "http://a/b/" },
	{ "../", "http://a/b/" },
	{ "../g", "http://a/b/g" },
	{ "../..", "http://a/" },
	{ "../../", "http://a/" },
	{ "../../g", "http://a/g" },
	{ "", "http://a/b/c/d;p?q#f" },
	{ "../../../g", "http://a/../g" },
	{ "../../../../g", "http://a/../../g" },
	{ "/./g", "http://a/./g" },
	{ "/../g", "http://a/../g" },
	{ "g.", "http://a/b/c/g." },
	{ ".g", "http://a/b/c/.g" },
	{ "g..", "http://a/b/c/g.." },
	{ "..g", "http://a/b/c/..g" },
	{ "./../g", "http://a/b/g" },
	{ "./g/.", "http://a/b/c/g/" },
	{ "g/./h", "http://a/b/c/g/h" },
	{ "g/../h", "http://a/b/c/h" },
	{ "http:g", "http:g" },
	{ "http:", "http:" }
};
int num_tests = sizeof (tests) / sizeof (tests[0]);

int
main (int argc, char **argv)
{
	SoupUri *base_uri, *uri;
	char *uri_string;
	int i;

	base_uri = soup_uri_new (base);
	if (!base_uri) {
		fprintf (stderr, "Could not parse %s\n", base);
		exit (1);
	}

	uri_string = soup_uri_to_string (base_uri, FALSE);
	if (strcmp (uri_string, base) != 0) {
		fprintf (stderr, "URI <%s> unparses to <%s>\n",
			 base, uri_string);
		exit (1);
	}
	g_free (uri_string);

	for (i = 0; i < num_tests; i++) {
		printf ("<%s> + <%s> = <%s>?\n", base, tests[i].uri_string, tests[i].result);
		uri = soup_uri_new_with_base (base_uri, tests[i].uri_string);
		if (!uri) {
			fprintf (stderr, "Could not parse %s\n", tests[i].uri_string);
			exit (1);
		}

		uri_string = soup_uri_to_string (uri, FALSE);
		if (strcmp (uri_string, tests[i].result) != 0) {
			fprintf (stderr, "Unparses to <%s>\n",
				 uri_string);
			exit (1);
		}
		g_free (uri_string);
	}

	return 0;
}
