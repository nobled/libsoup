/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-gnutls.c
 *
 * Copyright (C) 2003-2006, Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SSL

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#ifndef G_OS_WIN32
#include <pthread.h>
#endif

#include <gcrypt.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include "soup-ssl.h"
#include "soup-misc.h"

/**
 * soup_ssl_supported:
 *
 * Can be used to test if libsoup was compiled with ssl support.
 **/
const gboolean soup_ssl_supported = TRUE;

#define DH_BITS 1024

struct SoupSSLCredentials {
	gnutls_certificate_credentials creds;
	gboolean have_ca_file;
};

typedef struct {
	GIOChannel channel;
	GIOChannel *real_sock;
	int sockfd;
	gboolean non_blocking, eagain;
	gnutls_session session;
	SoupSSLCredentials *creds;
	char *hostname;
	gboolean established;
	SoupSSLType type;
} SoupGNUTLSChannel;

static gboolean
verify_certificate (gnutls_session session, const char *hostname, GError **err)
{
	int status;

	status = gnutls_certificate_verify_peers (session);

	if (status == GNUTLS_E_NO_CERTIFICATE_FOUND) {
		g_set_error (err, SOUP_SSL_ERROR,
			     SOUP_SSL_ERROR_CERTIFICATE,
			     "No SSL certificate was sent.");
		return FALSE;
	}

	if (status & GNUTLS_CERT_INVALID ||
#ifdef GNUTLS_CERT_NOT_TRUSTED
	    status & GNUTLS_CERT_NOT_TRUSTED ||
#endif
	    status & GNUTLS_CERT_REVOKED)
	{
		g_set_error (err, SOUP_SSL_ERROR,
			     SOUP_SSL_ERROR_CERTIFICATE,
			     "The SSL certificate is not trusted.");
		return FALSE;
	}

	if (gnutls_certificate_expiration_time_peers (session) < time (0)) {
		g_set_error (err, SOUP_SSL_ERROR,
			     SOUP_SSL_ERROR_CERTIFICATE,
			     "The SSL certificate has expired.");
		return FALSE;
	}

	if (gnutls_certificate_activation_time_peers (session) > time (0)) {
		g_set_error (err, SOUP_SSL_ERROR,
			     SOUP_SSL_ERROR_CERTIFICATE,
			     "The SSL certificate is not yet activated.");
		return FALSE;
	}

	if (gnutls_certificate_type_get (session) == GNUTLS_CRT_X509) {
		const gnutls_datum* cert_list;
		guint cert_list_size;
		gnutls_x509_crt cert;

		if (gnutls_x509_crt_init (&cert) < 0) {
			g_set_error (err, SOUP_SSL_ERROR,
				     SOUP_SSL_ERROR_CERTIFICATE,
				     "Error initializing SSL certificate.");
			return FALSE;
		}
      
		cert_list = gnutls_certificate_get_peers (
			session, &cert_list_size);

		if (cert_list == NULL) {
			g_set_error (err, SOUP_SSL_ERROR,
				     SOUP_SSL_ERROR_CERTIFICATE,
				     "No SSL certificate was found.");
			return FALSE;
		}

		if (gnutls_x509_crt_import (cert, &cert_list[0],
					    GNUTLS_X509_FMT_DER) < 0) {
			g_set_error (err, SOUP_SSL_ERROR,
				     SOUP_SSL_ERROR_CERTIFICATE,
				     "The SSL certificate could not be parsed.");
			return FALSE;
		}

		if (!gnutls_x509_crt_check_hostname (cert, hostname)) {
			g_set_error (err, SOUP_SSL_ERROR,
				     SOUP_SSL_ERROR_CERTIFICATE,
				     "The SSL certificate does not match the hostname.");
			return FALSE;
		}
	}

	return TRUE;
}

static GIOStatus
do_handshake (SoupGNUTLSChannel *chan, GError **err)
{
	int result;

again:
	result = gnutls_handshake (chan->session);

	if (result == GNUTLS_E_AGAIN || result == GNUTLS_E_INTERRUPTED) {
		if (chan->non_blocking) {
			g_set_error (err, SOUP_SSL_ERROR,
				     (gnutls_record_get_direction (chan->session) ?
				      SOUP_SSL_ERROR_HANDSHAKE_NEEDS_WRITE :
				      SOUP_SSL_ERROR_HANDSHAKE_NEEDS_READ),
				     "Handshaking...");
			return G_IO_STATUS_AGAIN;
		} else
			goto again;
	}

	if (result < 0) {
		g_set_error (err, G_IO_CHANNEL_ERROR,
			     G_IO_CHANNEL_ERROR_FAILED,
			     "Unable to handshake");
		return G_IO_STATUS_ERROR;
	}

	if (chan->type == SOUP_SSL_TYPE_CLIENT && chan->creds->have_ca_file &&
	    !verify_certificate (chan->session, chan->hostname, err))
		return G_IO_STATUS_ERROR;

	return G_IO_STATUS_NORMAL;
}

gssize
soup_ssl_session_receive (SoupSSLSession *session,
			  gchar *buffer, gsize size,
			  GCancellable *cancellable,
			  GError **error)
{
	int result;
	gssize nread = 0;

again:
	if (!session->established) {
		if (!do_handshake (session, error))
			return -1;

		session->established = TRUE;
	}

	result = gnutls_record_recv (session->gnutls_session, buffer, size);
	if (result == GNUTLS_E_REHANDSHAKE) {
		session->established = FALSE;
		goto again;
	}

	if (result == GNUTLS_E_INTERRUPTED || result == GNUTLS_E_AGAIN) {
		if (chan->non_blocking || chan->eagain)
			return G_IO_STATUS_AGAIN;
		else
			goto again;
	}

	if (result == GNUTLS_E_UNEXPECTED_PACKET_LENGTH) {
		/* This means the connection was either corrupted or
		 * interrupted. One particular thing that it can mean
		 * is that the remote end closed the connection
		 * abruptly without doing a proper TLS Close. There
		 * are security reasons why it's bad to treat this as
		 * not-an-error, but for compatibility reasons (eg,
		 * bug 577386) we kinda have to. And it's not like
		 * we're very secure anyway.
		 */
		return G_IO_STATUS_EOF;
	}

	if (result < 0) {
		g_set_error (err, G_IO_CHANNEL_ERROR,
			     G_IO_CHANNEL_ERROR_FAILED,
			     "Received corrupted data");
		return G_IO_STATUS_ERROR;
	} else {
		*bytes_read = result;

		return (result > 0) ? G_IO_STATUS_NORMAL : G_IO_STATUS_EOF;
	}
}

static GIOStatus
soup_gnutls_write (GIOChannel   *channel,
		   const gchar  *buf,
		   gsize         count,
		   gsize        *bytes_written,
		   GError      **err)
{
	SoupGNUTLSChannel *chan = (SoupGNUTLSChannel *) channel;
	gint result;

	*bytes_written = 0;

again:
	if (!chan->established) {
		result = do_handshake (chan, err);

		if (result == G_IO_STATUS_AGAIN ||
		    result == G_IO_STATUS_ERROR)
			return result;

		chan->established = TRUE;
	}

	result = gnutls_record_send (chan->session, buf, count);

	/* I'm pretty sure this can't actually happen in response to a
	 * write, but...
	 */
	if (result == GNUTLS_E_REHANDSHAKE) {
		chan->established = FALSE;
		goto again;
	}

	if (result == GNUTLS_E_INTERRUPTED || result == GNUTLS_E_AGAIN) {
		if (chan->non_blocking || chan->eagain)
			return G_IO_STATUS_AGAIN;
		else
			goto again;
	}

	if (result < 0) {
		g_set_error (err, G_IO_CHANNEL_ERROR,
			     G_IO_CHANNEL_ERROR_FAILED,
			     "Received corrupted data");
		return G_IO_STATUS_ERROR;
	} else {
		*bytes_written = result;

		return (result > 0) ? G_IO_STATUS_NORMAL : G_IO_STATUS_EOF;
	}
}

static GIOStatus
soup_gnutls_seek (GIOChannel  *channel,
		  gint64       offset,
		  GSeekType    type,
		  GError     **err)
{
	SoupGNUTLSChannel *chan = (SoupGNUTLSChannel *) channel;

	return chan->real_sock->funcs->io_seek (chan->real_sock, offset, type, err);
}

static GIOStatus
soup_gnutls_close (GIOChannel  *channel,
		   GError     **err)
{
	SoupGNUTLSChannel *chan = (SoupGNUTLSChannel *) channel;

	if (chan->established) {
		int ret;

		do {
			ret = gnutls_bye (chan->session, GNUTLS_SHUT_WR);
		} while (ret == GNUTLS_E_INTERRUPTED);
	}

	return chan->real_sock->funcs->io_close (chan->real_sock, err);
}

static GSource *
soup_gnutls_create_watch (GIOChannel   *channel,
			  GIOCondition  condition)
{
	SoupGNUTLSChannel *chan = (SoupGNUTLSChannel *) channel;

	return chan->real_sock->funcs->io_create_watch (chan->real_sock,
							condition);
}

static void
soup_gnutls_free (GIOChannel *channel)
{
	SoupGNUTLSChannel *chan = (SoupGNUTLSChannel *) channel;
	g_io_channel_unref (chan->real_sock);
	gnutls_deinit (chan->session);
	g_free (chan->hostname);
	g_slice_free (SoupGNUTLSChannel, chan);
}

static GIOStatus
soup_gnutls_set_flags (GIOChannel  *channel,
		       GIOFlags     flags,
		       GError     **err)
{
	SoupGNUTLSChannel *chan = (SoupGNUTLSChannel *) channel;

	return chan->real_sock->funcs->io_set_flags (chan->real_sock, flags, err);
}

static GIOFlags
soup_gnutls_get_flags (GIOChannel *channel)
{
	SoupGNUTLSChannel *chan = (SoupGNUTLSChannel *) channel;

	return chan->real_sock->funcs->io_get_flags (chan->real_sock);
}

static const GIOFuncs soup_gnutls_channel_funcs = {
	soup_gnutls_read,
	soup_gnutls_write,
	soup_gnutls_seek,
	soup_gnutls_close,
	soup_gnutls_create_watch,
	soup_gnutls_free,
	soup_gnutls_set_flags,
	soup_gnutls_get_flags
};

static gnutls_dh_params dh_params = NULL;

static gboolean
init_dh_params (void)
{
	static volatile gsize inited_dh_params = 0;

	if (g_once_init_enter (&inited_dh_params)) {
		if (gnutls_dh_params_init (&dh_params) != 0 ||
		    gnutls_dh_params_generate2 (dh_params, DH_BITS) != 0) {
			if (dh_params) {
				gnutls_dh_params_deinit (dh_params);
				dh_params = NULL;
			}
		}
		g_once_init_leave (&inited_dh_params, TRUE);
	}

	return dh_params != NULL;
}

static ssize_t
soup_gnutls_pull_func (gnutls_transport_ptr_t transport_data,
		       void *buf, size_t buflen)
{
	SoupGNUTLSChannel *chan = transport_data;
	ssize_t nread;

	nread = read (chan->sockfd, buf, buflen);
	chan->eagain = (nread == -1 && errno == EAGAIN);
	return nread;
}

static ssize_t
soup_gnutls_push_func (gnutls_transport_ptr_t transport_data,
		       const void *buf, size_t buflen)
{
	SoupGNUTLSChannel *chan = transport_data;
	ssize_t nwrote;

	nwrote = write (chan->sockfd, buf, buflen);
	chan->eagain = (nwrote == -1 && errno == EAGAIN);
	return nwrote;
}

SoupSSLSession *
soup_ssl_session_new (GSocket *gsock, SoupSSLType type,
		      const char *remote_host, SoupSSLCredentials *creds)
{
	SoupSSLSession *sss = NULL;
	gnutls_session session = NULL;
	int ret;

	g_return_val_if_fail (gsock != NULL, NULL);
	g_return_val_if_fail (creds != NULL, NULL);

	ret = gnutls_init (&session,
			   (type == SOUP_SSL_TYPE_CLIENT) ? GNUTLS_CLIENT : GNUTLS_SERVER);
	if (ret)
		goto THROW_CREATE_ERROR;

	if (gnutls_set_default_priority (session) != 0)
		goto THROW_CREATE_ERROR;

	if (gnutls_credentials_set (session, GNUTLS_CRD_CERTIFICATE,
				    creds->creds) != 0)
		goto THROW_CREATE_ERROR;

	if (type == SOUP_SSL_TYPE_SERVER)
		gnutls_dh_set_prime_bits (session, DH_BITS);

	sss = g_slice_new0 (SoupSSLSession);
	sss->gsock = g_object_ref (gsock);
	sss->session = session;
	sss->creds = creds;
	sss->hostname = g_strdup (remote_host);
	sss->type = type;

	gnutls_transport_set_ptr (session, sss);
	gnutls_transport_set_push_function (session, soup_gnutls_push_func);
	gnutls_transport_set_pull_function (session, soup_gnutls_pull_func);

	return sss;

 THROW_CREATE_ERROR:
	if (session)
		gnutls_deinit (session);
	return NULL;
}

gssize              soup_ssl_session_send            (SoupSSLSession     *session,
						      const gchar        *buffer,
						      gsize               size,
						      GCancellable       *cancellable,
						      GError            **error);

#if defined(GCRY_THREAD_OPTION_PTHREAD_IMPL) && !defined(G_OS_WIN32)
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif

static void
soup_gnutls_init (void)
{
	static volatile gsize inited_gnutls = 0;

	if (g_once_init_enter (&inited_gnutls)) {
#if defined(GCRY_THREAD_OPTION_PTHREAD_IMPL) && !defined(G_OS_WIN32)
		gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
#endif
		gnutls_global_init ();
		g_once_init_leave (&inited_gnutls, TRUE);
	}
}

/**
 * soup_ssl_get_client_credentials:
 * @ca_file: path to a file containing X509-encoded Certificate
 * Authority certificates.
 *
 * Creates an opaque client credentials object which can later be
 * passed to soup_ssl_wrap_iochannel().
 *
 * If @ca_file is non-%NULL, any certificate received from a server
 * must be signed by one of the CAs in the file, or an error will
 * be returned.
 *
 * Return value: the client credentials, which must be freed with
 * soup_ssl_free_client_credentials().
 **/
SoupSSLCredentials *
soup_ssl_get_client_credentials (const char *ca_file)
{
	SoupSSLCredentials *creds;
	int status;

	soup_gnutls_init ();

	creds = g_slice_new0 (SoupSSLCredentials);
	gnutls_certificate_allocate_credentials (&creds->creds);

	if (ca_file) {
		creds->have_ca_file = TRUE;
		status = gnutls_certificate_set_x509_trust_file (
			creds->creds, ca_file, GNUTLS_X509_FMT_PEM);
		if (status < 0) {
			g_warning ("Failed to set SSL trust file (%s).",
				   ca_file);
			/* Since we set have_ca_file though, this just
			 * means that no certs will validate, so we're
			 * ok securitywise if we just return these
			 * creds to the caller.
			 */
		}
	}

	return creds;
}

/**
 * soup_ssl_free_client_credentials:
 * @creds: a client credentials structure returned by
 * soup_ssl_get_client_credentials().
 *
 * Frees @creds.
 **/
void
soup_ssl_free_client_credentials (SoupSSLCredentials *creds)
{
	gnutls_certificate_free_credentials (creds->creds);
	g_slice_free (SoupSSLCredentials, creds);
}

/**
 * soup_ssl_get_server_credentials:
 * @cert_file: path to a file containing an X509-encoded server
 * certificate
 * @key_file: path to a file containing an X509-encoded key for
 * @cert_file.
 *
 * Creates an opaque server credentials object which can later be
 * passed to soup_ssl_wrap_iochannel().
 *
 * Return value: the server credentials, which must be freed with
 * soup_ssl_free_server_credentials().
 **/
SoupSSLCredentials *
soup_ssl_get_server_credentials (const char *cert_file, const char *key_file)
{
	SoupSSLCredentials *creds;

	soup_gnutls_init ();
	if (!init_dh_params ())
		return NULL;

	creds = g_slice_new0 (SoupSSLCredentials);
	gnutls_certificate_allocate_credentials (&creds->creds);

	if (gnutls_certificate_set_x509_key_file (creds->creds,
						  cert_file, key_file,
						  GNUTLS_X509_FMT_PEM) != 0) {
		g_warning ("Failed to set SSL certificate and key files "
			   "(%s, %s).", cert_file, key_file);
		soup_ssl_free_server_credentials (creds);
		return NULL;
	}

	gnutls_certificate_set_dh_params (creds->creds, dh_params);
	return creds;
}

/**
 * soup_ssl_free_server_credentials:
 * @creds: a server credentials structure returned by
 * soup_ssl_get_server_credentials().
 *
 * Frees @creds.
 **/
void
soup_ssl_free_server_credentials (SoupSSLCredentials *creds)
{
	gnutls_certificate_free_credentials (creds->creds);
	g_slice_free (SoupSSLCredentials, creds);
}

#endif /* HAVE_SSL */
