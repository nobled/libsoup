#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libsoup/soup.h>

int
main (int argc, char **argv)
{
	SoupSocket *listener, *client;
	SoupAddress *addr = NULL;
	gboolean ssl = FALSE;
	guint port = SOUP_SERVER_ANY_PORT;
	time_t now;
	char *timebuf;
	GIOChannel *chan;
	gsize wrote;
	int opt;

	g_type_init ();

	while ((opt = getopt (argc, argv, "6p:s")) != -1) {
		switch (opt) {
		case '6':
#ifdef HAVE_IPV6
			addr = soup_address_new_any (AF_INET6);
#else
			fprintf (stderr, "No IPv6 support\n");
			exit (1);
#endif
			break;

		case 'p':
			port = atoi (optarg);
			break;

		case 's':
			ssl = TRUE;
			break;

		default:
			fprintf (stderr, "Usage: %s [-6] [-p port] [-s]\n",
				 argv[0]);
			exit (1);
		}
	}

	if (!addr)
		addr = soup_address_new_any (AF_INET);

	listener = soup_socket_server_new (addr, port, ssl);
	if (!listener) {
		fprintf (stderr, "Could not create listening socket\n");
		exit (1);
	}
	printf ("Listening on port %d\n", soup_socket_get_local_port (listener));

	while ((client = soup_socket_server_accept (listener))) {
		addr = soup_socket_get_remote_address (client);
		printf ("got connection from %s port %d\n",
			soup_address_get_physical (addr),
			soup_socket_get_remote_port (client));

		now = time (NULL);
		timebuf = ctime (&now);

		chan = soup_socket_get_iochannel (client);
		g_io_channel_write (chan, timebuf, strlen (timebuf), &wrote);
		g_io_channel_unref (chan);

		g_object_unref (client);
	}

	return 0;
}
