/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-socket.c: socket networking code.
 *
 * Original code compliments of David Helder's GNET Networking
 * Library, and is Copyright (C) 2000 David Helder & Andrew Lanoix.
 *
 * All else Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <string.h>
#include <unistd.h>

#include "soup-address.h"
#include "soup-dns.h"
#include "soup-private.h"
#include "soup-socket.h"
#include "soup-ssl.h"

#include <sys/socket.h>
#include <netinet/tcp.h>

#ifndef socklen_t
#  define socklen_t size_t
#endif

#define SOUP_SOCKET_IS_SERVER (1<<0)
#define SOUP_SOCKET_IS_SSL    (1<<1)

struct _SoupSocketPrivate {
	int          sockfd;
	guint32      flags;
	SoupAddress *local_addr, *remote_addr;
	guint        local_port, remote_port;
	GIOChannel  *iochannel;

	SoupSocketConnectedFn connect_func;
	gpointer connect_data;
	guint connect_watch;
	SoupAsyncHandle addr_lookup_id;
};

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

static void
init (GObject *object)
{
	SoupSocket *sock = SOUP_SOCKET (object);

	sock->priv = g_new0 (SoupSocketPrivate, 1);
	sock->priv->sockfd = -1;
}

static void
finalize (GObject *object)
{
	SoupSocket *sock = SOUP_SOCKET (object);

	if (sock->priv->connect_watch)
		g_source_remove (sock->priv->connect_watch);
	if (sock->priv->addr_lookup_id)
		soup_gethostby_cancel (sock->priv->addr_lookup_id);

	if (sock->priv->local_addr)
		g_object_unref (sock->priv->local_addr);
	if (sock->priv->remote_addr)
		g_object_unref (sock->priv->remote_addr);

	if (sock->priv->iochannel)
		g_io_channel_unref (sock->priv->iochannel);
	else if (sock->priv->sockfd != -1)
		close (sock->priv->sockfd);

	g_free (sock->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
}

SOUP_MAKE_TYPE (soup_socket, SoupSocket, class_init, init, PARENT_TYPE)


#define SOUP_SOCKET_NONBLOCKING (1<<0)
#define SOUP_SOCKET_NONBUFFERED (1<<1)
#define SOUP_SOCKET_REUSEADDR   (1<<2)

static void
soup_set_sockopts (int sockfd, int opts)
{
	int flags;

	if (opts & SOUP_SOCKET_NONBLOCKING) {
		flags = fcntl (sockfd, F_GETFL, 0);
		if (flags != -1)
			fcntl (sockfd, F_SETFL, flags | O_NONBLOCK);
	}

	if (opts & SOUP_SOCKET_NONBUFFERED) {
		flags = 1;
		setsockopt (sockfd, IPPROTO_TCP, TCP_NODELAY,
			    &flags, sizeof (flags));
	}

	if (opts & SOUP_SOCKET_REUSEADDR) {
		flags = 1;
		setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR,
			    &flags, sizeof (flags));
	}
}

#define SOUP_ANY_IO_CONDITION  (G_IO_IN | G_IO_OUT | G_IO_PRI | \
                                G_IO_ERR | G_IO_HUP | G_IO_NVAL)

static void
done_connect (SoupSocket *sock, SoupKnownErrorCode status)
{
	SoupSocketConnectedFn connect_func = sock->priv->connect_func;
	gpointer connect_data = sock->priv->connect_data;

	sock->priv->connect_func = NULL;
	sock->priv->connect_data = NULL;
	connect_func (sock, status, connect_data);
}

static gboolean
connect_watch (GIOChannel* iochannel, GIOCondition condition, gpointer data)
{
	SoupSocket *sock = data;
	int error = 0;
	int len = sizeof (error);

	/* Remove the watch now in case we don't return immediately */
	g_source_remove (sock->priv->connect_watch);

	if (condition & ~(G_IO_IN | G_IO_OUT))
		goto cant_connect;

	if (getsockopt (sock->priv->sockfd, SOL_SOCKET, SO_ERROR,
			&error, &len) != 0)
		goto cant_connect;
	if (error)
		goto cant_connect;

	if ((sock->priv->flags & SOUP_SOCKET_IS_SSL) &&
	    !soup_socket_start_ssl (sock)) {
		/* FIXME */
		g_object_unref (sock);
		goto cant_connect;
	}

	done_connect (sock, SOUP_ERROR_OK);
	return FALSE;

 cant_connect:
	done_connect (sock, SOUP_ERROR_CANT_CONNECT);
	return FALSE;
}

static void
got_address (SoupKnownErrorCode err, struct hostent *h, gpointer user_data)
{
	SoupSocket *sock = user_data;
	struct sockaddr *sa = NULL;
	int len, status;

	sock->priv->addr_lookup_id = NULL;

	if (err != SOUP_ERROR_OK) {
		done_connect (sock, SOUP_ERROR_CANT_RESOLVE);
		return;
	}

	sock->priv->remote_addr = soup_address_new_from_hostent (h);
	soup_address_make_sockaddr (sock->priv->remote_addr,
				    sock->priv->remote_port,
				    &sa, &len);
	sock->priv->sockfd = socket (sa->sa_family, SOCK_STREAM, 0);
	if (sock->priv->sockfd < 0)
		goto cant_connect;
	soup_set_sockopts (sock->priv->sockfd,
			   SOUP_SOCKET_NONBLOCKING | SOUP_SOCKET_NONBUFFERED);

	/* Connect (non-blocking) */
	status = connect (sock->priv->sockfd, sa, len);
	g_free (sa);
	sa = NULL;

	if (status == 0) {
		/* Connect already succeeded */
		done_connect (sock, SOUP_ERROR_OK);
		return;
	}
	if (errno != EINPROGRESS)
		goto cant_connect;

	soup_socket_get_iochannel (sock);
	sock->priv->connect_watch = g_io_add_watch (sock->priv->iochannel,
						    SOUP_ANY_IO_CONDITION,
						    connect_watch,
						    sock);
	return;

 cant_connect:
	if (sa)
		g_free (sa);
	done_connect (sock, SOUP_ERROR_CANT_CONNECT);
}

/**
 * soup_socket_client_connect:
 * @sock: the socket to connect
 * @hostname: Name of host to connect to
 * @port: Port to connect to
 * @ssl: Whether or not to use SSL
 * @func: Callback function
 * @data: User data passed when callback function is called.
 *
 * See soup_socket_client_new().
 **/
void
soup_socket_client_connect (SoupSocket *sock,
			    const char *hostname, guint port, gboolean ssl,
			    SoupSocketConnectedFn func, gpointer data)
{
	g_return_if_fail (hostname != NULL);
	g_return_if_fail (func != NULL);

	sock->priv->remote_port = port;
	if (ssl)
		sock->priv->flags |= SOUP_SOCKET_IS_SSL;
	else
		sock->priv->flags &= ~SOUP_SOCKET_IS_SSL;

	sock->priv->connect_func = func;
	sock->priv->connect_data = data;

	sock->priv->addr_lookup_id =
		soup_gethostbyname (hostname, got_address, sock);
}

/**
 * soup_socket_client_new:
 * @hostname: Name of host to connect to
 * @port: Port to connect to
 * @ssl: Whether or not to use SSL
 * @func: Callback function
 * @data: User data passed when callback function is called.
 *
 * Creates a connection to the given @hostname and @port, optionall
 * using SSL. @func will be called when the connection is complete.
 * If the socket is destroyed before the connection completes, @func
 * will not be called.
 *
 * Return value: the new socket (not yet ready for use).
 **/
SoupSocket *
soup_socket_client_new (const char *hostname, guint port, gboolean ssl,
			SoupSocketConnectedFn func, gpointer data)
{
	SoupSocket *sock;

	g_return_val_if_fail (hostname != NULL, NULL);
	g_return_val_if_fail (func != NULL, NULL);

	sock = g_object_new (SOUP_TYPE_SOCKET, NULL);
	soup_socket_client_connect (sock, hostname, port, ssl, func, data);

	return sock;
}

/**
 * soup_socket_get_iochannel:
 * @socket: #SoupSocket to get #GIOChannel from.
 *
 * Get the #GIOChannel for the #SoupSocket.
 *
 * For a client socket or connected server socket, the #GIOChannel
 * represents the data stream. Use it like you would any other
 * #GIOChannel.
 *
 * For a listening server socket, the #GIOChannel represents incoming
 * connections. If you can read from it, there's a connection waiting.
 *
 * If you ref the iochannel, it will remain valid after @socket is
 * destroyed.
 *
 * Returns: A #GIOChannel; %NULL on failure.
 **/
GIOChannel *
soup_socket_get_iochannel (SoupSocket *socket)
{
	g_return_val_if_fail (SOUP_IS_SOCKET (socket), NULL);

	if (!socket->priv->iochannel) {
		socket->priv->iochannel =
			g_io_channel_unix_new (socket->priv->sockfd);
		g_io_channel_set_close_on_unref (socket->priv->iochannel, TRUE);
	}

	return socket->priv->iochannel;
}

static void
get_local_addr (SoupSocket *socket)
{
	struct soup_sockaddr_max bound_sa;
	int sa_len;

	sa_len = sizeof (bound_sa);
	getsockname (socket->priv->sockfd, (struct sockaddr *)&bound_sa, &sa_len);
	socket->priv->local_addr = soup_address_new_from_sockaddr (
		(struct sockaddr *)&bound_sa, &socket->priv->local_port);
}

/**
 * soup_socket_get_local_address:
 * @socket: #SoupSocket to get local address of.
 *
 * Get the local address of the socket. You must ref the address if
 * you want to keep it after the socket is destroyed.
 *
 * Returns: #SoupAddress of the local end of the socket; %NULL on
 * failure.
 **/
SoupAddress *
soup_socket_get_local_address (SoupSocket *socket)
{
	g_return_val_if_fail (SOUP_IS_SOCKET (socket), NULL);

	if (!socket->priv->local_addr)
		get_local_addr (socket);

	return socket->priv->local_addr;
}

/**
 * soup_socket_get_local_port:
 * @socket: SoupSocket to get the local port number of.
 *
 * Get the local port number the socket is bound to.
 *
 * Returns: Local port number of the socket.
 **/
guint
soup_socket_get_local_port (SoupSocket *socket)
{
	g_return_val_if_fail (SOUP_IS_SOCKET (socket), 0);

	if (!socket->priv->local_port)
		get_local_addr (socket);

	return socket->priv->local_port;
}

/**
 * soup_socket_get_remote_address:
 * @socket: #SoupSocket to get remote address of.
 *
 * Get the remote address of the socket. (For a listening socket,
 * this will be %NULL.) You must ref the address if you want to
 * keep if after the socket is destroyed.
 *
 * Returns: #SoupAddress of the remote end of the socket; %NULL on
 * failure.
 **/
SoupAddress *
soup_socket_get_remote_address (SoupSocket *socket)
{
	g_return_val_if_fail (SOUP_IS_SOCKET (socket), NULL);

	return socket->priv->remote_addr;
}

/**
 * soup_socket_get_remote_port:
 * @socket: SoupSocket to get the remote port number of.
 *
 * Get the remote port number the socket is bound to. For a listening
 * socket, this will be 0.
 *
 * Returns: Remote port number of the socket.
 **/
guint
soup_socket_get_remote_port (SoupSocket *socket)
{
	g_return_val_if_fail (SOUP_IS_SOCKET (socket), 0);

	return socket->priv->remote_port;
}

/**
 * soup_socket_server_new:
 * @local_addr: Local address to bind to. (Use soup_address_any_new() to
 * accept connections on any local address)
 * @local_port: Port number for the socket (SOUP_SERVER_ANY_PORT if you
 * don't care).
 * @ssl: Whether or not this is an SSL server.
 *
 * Create and open a new #SoupSocket listening on the specified
 * address and port. Use this sort of socket when your are a server
 * and you know what the port number should be (or pass 0 if you don't
 * care what the port is).
 *
 * Returns: a new #SoupSocket, or NULL if there was a failure.
 **/
SoupSocket *
soup_socket_server_new (SoupAddress *local_addr, guint local_port,
			gboolean ssl)
{
	SoupSocket *sock;
	struct sockaddr *sa = NULL;
	int sockfd, sa_len;

	g_return_val_if_fail (SOUP_IS_ADDRESS (local_addr), NULL);

	/* Create an appropriate sockaddr */
	soup_address_make_sockaddr (local_addr, local_port, &sa, &sa_len);

	sockfd = socket (sa->sa_family, SOCK_STREAM, 0);
	if (sockfd < 0)
		goto cant_listen;
	soup_set_sockopts (sockfd,
			   SOUP_SOCKET_NONBLOCKING | SOUP_SOCKET_REUSEADDR);

	/* Bind */
	if (bind (sockfd, sa, sa_len) != 0)
		goto cant_listen;
	g_free (sa);
	sa = NULL;

	/* Listen */
	if (listen (sockfd, 10) != 0)
		goto cant_listen;

	/* Create socket */
	sock = g_object_new (SOUP_TYPE_SOCKET, NULL);
	sock->priv->flags = SOUP_SOCKET_IS_SERVER | (ssl ? SOUP_SOCKET_IS_SSL : 0);
	sock->priv->sockfd = sockfd;

	return sock;

 cant_listen:
	if (sockfd != -1)
		close (sockfd);
	if (sa)
		g_free (sa);
	return NULL;
}

static SoupSocket *
server_accept_internal (SoupSocket *socket, gboolean block)
{
	int sockfd;
	struct soup_sockaddr_max sa;
	socklen_t n;
	fd_set fdset;
	SoupSocket *new;

	g_return_val_if_fail (SOUP_IS_SOCKET (socket), NULL);
	g_return_val_if_fail (socket->priv->flags & SOUP_SOCKET_IS_SERVER, NULL);

 try_again:
	FD_ZERO (&fdset);
	FD_SET (socket->priv->sockfd, &fdset);

	if (select (socket->priv->sockfd + 1, &fdset, NULL, NULL, NULL) == -1) {
		if (errno == EINTR)
			goto try_again;
		return NULL;
	}

	n = sizeof (sa);
	sockfd = accept (socket->priv->sockfd, (struct sockaddr *)&sa, &n);
	if (sockfd == -1) {
		if (!block)
			return NULL;
		if (errno == EWOULDBLOCK ||
		    errno == ECONNABORTED ||
#ifdef EPROTO		/* OpenBSD does not have EPROTO */
		    errno == EPROTO ||
#endif
		    errno == EINTR)
			goto try_again;

		return NULL;
	}

	soup_set_sockopts (sockfd,
			   SOUP_SOCKET_NONBLOCKING | SOUP_SOCKET_NONBUFFERED);

	new = g_object_new (SOUP_TYPE_SOCKET, NULL);
	new->priv->sockfd = sockfd;
	new->priv->flags = socket->priv->flags;
	if ((new->priv->flags & SOUP_SOCKET_IS_SSL) &&
	    !soup_socket_start_ssl (new)) {
		g_object_unref (new);
		return NULL;
	}

	new->priv->remote_addr = soup_address_new_from_sockaddr (
		(struct sockaddr *)&sa, &new->priv->remote_port);

	return new;
}

/**
 * soup_socket_server_accept:
 * @socket: #SoupSocket to accept connections from.
 *
 * Accept a connection from the socket.  The socket must have been
 * created using soup_socket_server_new().  This function will
 * block (use soup_socket_server_try_accept() if you don't
 * want to block).  If the socket's #GIOChannel is readable, it DOES
 * NOT mean that this function will not block.
 *
 * Returns: a new #SoupSocket if there is another connect, or NULL if
 * there's an error.
 **/
SoupSocket *
soup_socket_server_accept (SoupSocket *socket)
{
	return server_accept_internal (socket, TRUE);
}

/**
 * soup_socket_server_try_accept:
 * @socket: SoupSocket to accept connections from.
 *
 * Accept a connection from the socket without blocking.  The socket
 * must have been created using soup_socket_server_new().  This
 * function is best used with the sockets #GIOChannel.  If the
 * channel is readable, then you PROBABLY have a connection.  It is
 * possible for the connection to close by the time you call this, so
 * it may return NULL even if the channel was readable.
 *
 * Returns a new SoupSocket if there is another connect, or NULL
 * otherwise.
 **/
SoupSocket *
soup_socket_server_try_accept (SoupSocket *socket)
{
	return server_accept_internal (socket, FALSE);
}

/**
 * soup_socket_start_ssl:
 * @socket: the socket
 *
 * Attempts to start using SSL on @socket.
 *
 * Return value: success or failure.
 **/
gboolean
soup_socket_start_ssl (SoupSocket *socket)
{
	GIOChannel *ssl_channel;
	SoupSSLType type;

	if (!socket->priv->iochannel) {
		soup_socket_get_iochannel (socket);
		g_io_channel_unref (socket->priv->iochannel);
	}

	if (socket->priv->flags & SOUP_SOCKET_IS_SERVER)
		type = SOUP_SSL_TYPE_SERVER;
	else
		type = SOUP_SSL_TYPE_CLIENT;
	ssl_channel = soup_ssl_get_iochannel (socket->priv->iochannel, type);
	if (!ssl_channel)
		return FALSE;

	g_io_channel_unref (socket->priv->iochannel);
	socket->priv->iochannel = ssl_channel;

	return TRUE;
}
