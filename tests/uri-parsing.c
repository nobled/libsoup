#include <config.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libsoup/soup-uri.h"

char *base = "http://a/b/c/d;p?q";

/* These are from RFC 2396 except that I changed the protocol "g" to
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
	{ "?y", "http://a/b/c/?y" },
	{ "g?y", "http://a/b/c/g?y" },
	{ "#s", "http://a/b/c/d;p?q#s" },
	{ "g#s", "http://a/b/c/g#s" },
	{ "g?y#s", "http://a/b/c/g?y#s" },
	{ ";x", "http://a/b/c/;x" },
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
	{ "", "http://a/b/c/d;p?q" },
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
	{ "g;x=1/./y", "http://a/b/c/g;x=1/y" },
	{ "g;x=1/../y", "http://a/b/c/y" },
	{ "g?y/./x", "http://a/b/c/g?y/./x" },
	{ "g?y/../x", "http://a/b/c/g?y/../x" },
	{ "g#s/./x", "http://a/b/c/g#s/./x" },
	{ "g#s/../x", "http://a/b/c/g#s/../x" },
	{ "http:g", "http:g" },
};
int num_tests = sizeof (tests) / sizeof (tests[0]);

int
main (int argc, char **argv)
{
	SoupUri *base_uri, *uri;
	char *uri_string;
	int i, errs = 0;

	base_uri = soup_uri_new (base);
	if (!base_uri) {
		fprintf (stderr, "Could not parse %s\n", base);
		exit (1);
	}

	uri_string = soup_uri_to_string (base_uri, FALSE);
	if (strcmp (uri_string, base) != 0) {
		fprintf (stderr, "URI <%s> unparses to <%s>\n",
			 base, uri_string);
		errs++;
	}
	g_free (uri_string);

	for (i = 0; i < num_tests; i++) {
		printf ("<%s> + <%s> = <%s>? ", base, tests[i].uri_string, tests[i].result);
		uri = soup_uri_new_with_base (base_uri, tests[i].uri_string);
		if (!uri) {
			printf ("ERR\n  Could not parse %s\n", tests[i].uri_string);
			errs++;
			continue;
		}

		uri_string = soup_uri_to_string (uri, FALSE);
		soup_uri_free (uri);

		if (strcmp (uri_string, tests[i].result) != 0) {
			printf ("NO\n  Unparses to <%s>\n", uri_string);
			g_free (uri_string);
			errs++;
			continue;
		}
		g_free (uri_string);

		printf ("OK\n");
	}

	return errs;
}
