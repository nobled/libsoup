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

#include "soup-private.h"
#include "soup-address.h"
#include "soup-socket.h"
#include "soup-ssl.h"

#include <unistd.h>
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

	SoupSocketConnectFn connect_func;
	gpointer connect_data;
	guint connect_source;
	SoupAddressNewId addr_id;
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

	if (sock->priv->connect_func) {
		sock->priv->connect_func (sock, SOUP_ERROR_CANCELLED,
					  sock->priv->connect_data);
	}
	if (sock->priv->connect_source)
		g_source_remove (sock->priv->connect_source);
	if (sock->priv->addr_id)
		soup_address_new_cancel (sock->priv->addr_id);

	if (sock->priv->sockfd != -1)
		close (sock->priv->sockfd);
	if (sock->priv->local_addr)
		soup_address_unref (sock->priv->local_addr);
	if (sock->priv->remote_addr)
		soup_address_unref (sock->priv->remote_addr);
	if (sock->priv->iochannel)
		g_io_channel_unref (sock->priv->iochannel);

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


/**
 * soup_socket_new:
 *
 * Return value: a new (disconnected) #SoupSocket
 **/
SoupSocket *
soup_socket_new (void)
{
	return g_object_new (SOUP_TYPE_SOCKET, NULL);
}


#define SOUP_ANY_IO_CONDITION  (G_IO_IN | G_IO_OUT | G_IO_PRI | \
                                G_IO_ERR | G_IO_HUP | G_IO_NVAL)

static void
done_connect (SoupSocket *sock, SoupKnownErrorCode status)
{
	sock->priv->connect_func (sock, status, sock->priv->connect_data);
	sock->priv->connect_func = NULL;
	sock->priv->connect_data = NULL;
}

static gboolean
soup_socket_connect_watch (GIOChannel* iochannel,
			   GIOCondition condition,
			   gpointer data)
{
	SoupSocket *sock = data;
	int error = 0, flags;
	int len = sizeof (int);

	/* Remove the watch now in case we don't return immediately */
	g_source_remove (sock->priv->connect_source);

	if (condition & ~(G_IO_IN | G_IO_OUT))
		goto cant_connect;

	if (getsockopt (sock->priv->sockfd, SOL_SOCKET, SO_ERROR,
			&error, &len) != 0)
		goto cant_connect;
	if (error)
		goto cant_connect;

	flags = fcntl (sock->priv->sockfd, F_GETFL, 0);
	if (flags == -1)
		goto cant_connect;
	if (fcntl (sock->priv->sockfd, F_SETFL, flags & ~O_NONBLOCK) == -1)
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

static gboolean
idle_cant_connect (gpointer user_data)
{
	SoupSocket *sock = user_data;

	sock->priv->connect_source = 0;
	done_connect (sock, SOUP_ERROR_CANT_CONNECT);
	return FALSE;
}

static void
async_connect (SoupSocket *sock)
{
	struct sockaddr *sa = NULL;
	int len, flags, status;
	GIOChannel *chan;

	/* Create socket */
	soup_address_make_sockaddr (sock->priv->remote_addr,
				    sock->priv->remote_port,
				    &sa, &len);
	sock->priv->sockfd = socket (sa->sa_family, SOCK_STREAM, 0);
	if (sock->priv->sockfd < 0)
		goto cant_connect;

	/* Get the flags (should all be 0?) */
	flags = fcntl (sock->priv->sockfd, F_GETFL, 0);
	if (flags == -1)
		goto cant_connect;

	if (fcntl (sock->priv->sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
		goto cant_connect;

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

	chan = g_io_channel_unix_new (sock->priv->sockfd);
	sock->priv->connect_source = g_io_add_watch (chan,
						     SOUP_ANY_IO_CONDITION,
						     soup_socket_connect_watch,
						     sock);
	g_io_channel_unref (chan);
	return;

 cant_connect:
	if (sa)
		g_free (sa);
	sock->priv->connect_source = g_idle_add (idle_cant_connect, sock);
}


/**
 * soup_socket_connect_by_addr:
 * @sock: The socket to connect
 * @addr: Address to connect to.
 * @port: Port to connect to
 * @ssl: Whether or not the connection is SSL
 * @func: Callback function.
 * @data: User data passed when callback function is called.
 *
 * Connect to a specifed address asynchronously.  When the connection
 * is complete or there is an error, it will call the callback.
 **/
void
soup_socket_connect_by_addr (SoupSocket *sock,
			     SoupAddress *addr, guint port, gboolean ssl,
			     SoupSocketConnectFn func, gpointer user_data)
{
	g_return_if_fail (addr != NULL);
	g_return_if_fail (sock->priv->remote_addr == NULL);
	g_return_if_fail (func != NULL);

	sock->priv->remote_addr = addr;
	soup_address_ref (addr);
	sock->priv->remote_port = port;
	if (ssl)
		sock->priv->flags |= SOUP_SOCKET_IS_SSL;
	else
		sock->priv->flags &= ~SOUP_SOCKET_IS_SSL;

	sock->priv->connect_func = func;
	sock->priv->connect_data = user_data;

	async_connect (sock);
}

static void
soup_socket_connect_addr_cb (SoupAddress *addr,
			     SoupKnownErrorCode status,
			     gpointer user_data)
{
	SoupSocket *sock = user_data;

	sock->priv->addr_id = NULL;

	if (status == SOUP_ERROR_OK) {
		sock->priv->remote_addr = addr;
		soup_address_ref (addr);
		async_connect (sock);
	} else
		done_connect (sock, SOUP_ERROR_CANT_RESOLVE);
}

/**
 * soup_socket_connect_by_name:
 * @sock: The socket to connect
 * @hostname: Name of host to connect to
 * @port: Port to connect to
 * @ssl: Whether or not to use SSL
 * @func: Callback function
 * @data: User data passed when callback function is called.
 *
 * A quick and easy non-blocking #SoupSocket connector. This connects
 * to the specified address and port and then calls the callback with
 * the data.
 **/
void
soup_socket_connect_by_name (SoupSocket *sock,
			     const char *hostname, guint port, gboolean ssl,
			     SoupSocketConnectFn func, gpointer data)
{
	g_return_if_fail (hostname != NULL);
	g_return_if_fail (sock->priv->remote_addr == NULL);
	g_return_if_fail (func != NULL);

	sock->priv->remote_port = port;
	if (ssl)
		sock->priv->flags |= SOUP_SOCKET_IS_SSL;
	else
		sock->priv->flags &= ~SOUP_SOCKET_IS_SSL;

	sock->priv->connect_func = func;
	sock->priv->connect_data = data;

	sock->priv->addr_id = soup_address_new (hostname,
						soup_socket_connect_addr_cb,
						sock);
}

/**
 * soup_socket_connect_cancel:
 * @sock: the socket to cancel
 *
 * Cancels an asynchronous connection. @sock's callback will not be called.
 **/
void
soup_socket_connect_cancel (SoupSocket *sock)
{
	g_return_if_fail (sock->priv->connect_func != NULL);

	sock->priv->connect_func = NULL;
	sock->priv->connect_data = NULL;

	if (sock->priv->connect_source) {
		g_source_remove (sock->priv->connect_source);
		sock->priv->connect_source = 0;
	}
	if (sock->priv->addr_id) {
		soup_address_new_cancel (sock->priv->addr_id);
		sock->priv->addr_id = NULL;
	}

	if (sock->priv->remote_addr) {
		soup_address_unref (sock->priv->remote_addr);
		sock->priv->remote_addr = NULL;
	}
}

/**
 * soup_socket_get_iochannel:
 * @socket: SoupSocket to get #GIOChannel from.
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
 * There is one channel for every socket. This function refs the
 * channel before returning it. You should unref the channel when you
 * are done with it. However, you should not close the channel - this
 * is done when you destroy the socket.
 *
 * Returns: A #GIOChannel; %NULL on failure.
 **/
GIOChannel*
soup_socket_get_iochannel (SoupSocket* socket)
{
	g_return_val_if_fail (socket != NULL, NULL);

	if (socket->priv->iochannel == NULL)
		socket->priv->iochannel = g_io_channel_unix_new (socket->priv->sockfd);

	g_io_channel_ref (socket->priv->iochannel);

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
 * Get the local address of the socket.
 *
 * Returns: #SoupAddress of the local end of the socket; %NULL on
 * failure. The caller must unref the address when it is done with it.
 **/
SoupAddress *
soup_socket_get_local_address (SoupSocket *socket)
{
	g_return_val_if_fail (socket != NULL, NULL);

	if (!socket->priv->local_addr)
		get_local_addr (socket);

	soup_address_ref (socket->priv->local_addr);
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
	g_return_val_if_fail (socket != NULL, 0);

	if (!socket->priv->local_port)
		get_local_addr (socket);

	return socket->priv->local_port;
}

/**
 * soup_socket_get_remote_address:
 * @socket: #SoupSocket to get remote address of.
 *
 * Get the remote address of the socket. For a listening socket,
 * this will be %NULL.
 *
 * Returns: #SoupAddress of the remote end of the socket; %NULL on
 * failure. The caller must unref the address when it is done with it.
 **/
SoupAddress *
soup_socket_get_remote_address (SoupSocket *socket)
{
	g_return_val_if_fail (socket != NULL, NULL);

	soup_address_ref (socket->priv->remote_addr);
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
	g_return_val_if_fail (socket != NULL, 0);

	return socket->priv->remote_port;
}

/**
 * soup_socket_server_new:
 * @local_addr: Local address to bind to. (soup_address_ipv4_any() to
 * accept connections on any local IPv4 address)
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
	const int on = 1;
	gint flags;

	g_return_val_if_fail (local_addr != NULL, NULL);

	/* Create an appropriate sockaddr */
	soup_address_make_sockaddr (local_addr, local_port, &sa, &sa_len);

	sockfd = socket (sa->sa_family, SOCK_STREAM, 0);
	if (sockfd < 0)
		goto cant_listen;

	/* Set REUSEADDR so we can reuse the port */
	if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR,
			&on, sizeof (on)) != 0)
		g_warning ("Can't set reuse on tcp socket\n");

	/* Make the socket non-blocking */
	flags = fcntl (sockfd, F_GETFL, 0);
	if (flags == -1)
		goto cant_listen;
	if (fcntl (sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
		goto cant_listen;

	/* Bind */
	if (bind (sockfd, sa, sa_len) != 0)
		goto cant_listen;
	g_free (sa);
	sa = NULL;

	/* Listen */
	if (listen (sockfd, 10) != 0)
		goto cant_listen;

	/* Create socket */
	sock = soup_socket_new ();
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
	int flags;
	struct soup_sockaddr_max sa;
	socklen_t n;
	fd_set fdset;
	SoupSocket *new;

	g_return_val_if_fail (socket != NULL, NULL);
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

	/* Make the socket non-blocking */
	flags = fcntl (sockfd, F_GETFL, 0);
	if (flags == -1) {
		close (sockfd);
		return NULL;
	}
	if (fcntl (sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
		close (sockfd);
		return NULL;
	}

	new = soup_socket_new ();
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
