## Process this file with automake to produce Makefile.in

INCLUDES = 				\
	-DG_LOG_DOMAIN=\"SOUP\" 	\
	-DSYSCONFDIR=\"$(sysconfdir)\" 	\
	-DLIBEXECDIR=\"$(libexecdir)\"	\
	-I$(top_srcdir)			\
	$(SOUP_DEBUG_FLAGS)		\
	$(GLIB_CFLAGS)			\
	$(NSS_CFLAGS)			\
	$(OPENSSL_CFLAGS)

MARSHAL_GENERATED = soup-marshal.c soup-marshal.h

soup-marshal.h: soup-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=soup_marshal soup-marshal.list --header > soup-marshal.tmp \
	&& mv soup-marshal.tmp soup-marshal.h ) \
	|| ( rm -f soup-marshal.tmp && exit 1 )

soup-marshal.c: soup-marshal.h
	( (echo '#include "soup-marshal.h"'; @GLIB_GENMARSHAL@ --prefix=soup_marshal soup-marshal.list --body) > soup-marshal.tmp \
	&& mv soup-marshal.tmp soup-marshal.c ) \
	|| ( rm -f soup-marshal.tmp && exit 1 )

BUILT_SOURCES = $(MARSHAL_GENERATED)

CLEANFILES = $(MARSHAL_GENERATED)

libsoupincludedir = $(includedir)/soup-2.0/libsoup

libsoupinclude_HEADERS =		\
	soup.h				\
	soup-address.h			\
	soup-auth.h			\
	soup-auth-context.h		\
	soup-connection.h		\
	soup-context.h			\
	soup-dns.h			\
	soup-error.h			\
	soup-headers.h			\
	soup-message.h			\
	soup-method.h			\
	soup-misc.h     		\
	soup-proxy-auth-context.h	\
	soup-server-auth.h		\
	soup-server.h			\
	soup-server-cgi.h		\
	soup-server-message.h		\
	soup-server-tcp.h		\
	soup-socket.h			\
	soup-types.h			\
	soup-uri.h			\
	soup-www-auth-context.h

lib_LTLIBRARIES = libsoup-2.0.la

libsoup_2_0_la_LDFLAGS =	\
	-version-info $(SOUP_CURRENT):$(SOUP_REVISION):$(SOUP_AGE)

libsoup_2_0_la_LIBADD =		\
	$(GLIB_LIBS)		\
	$(NSS_LIBS)

libsoup_2_0_la_SOURCES =		\
	$(MARSHAL_GENERATED)		\
	md5-utils.h			\
	md5-utils.c			\
	soup-address.c			\
	soup-auth.c			\
	soup-auth.h			\
	soup-auth-basic.c		\
	soup-auth-basic.h		\
	soup-auth-context.c		\
	soup-auth-digest.c		\
	soup-auth-digest.h		\
	soup-connection.c		\
	soup-context.c			\
	soup-dns.c			\
	soup-error.c			\
	soup-headers.c			\
	soup-message.c			\
	soup-method.c			\
	soup-misc.c     		\
	soup-nss.h			\
	soup-nss.c			\
	soup-private.h     		\
	soup-proxy-auth-context.c	\
	soup-queue.h			\
	soup-queue.c			\
	soup-server.c			\
	soup-server-auth.c		\
	soup-server-cgi.c		\
	soup-server-message.c		\
	soup-server-tcp.c		\
	soup-socket.c			\
	soup-socks.h			\
	soup-socks.c			\
	soup-ssl.h			\
	soup-ssl.c			\
	soup-transfer.h			\
	soup-transfer.c			\
	soup-uri.c			\
	soup-www-auth-context.c

noinst_PROGRAMS = libsoup-ssl-proxy

libsoup_ssl_proxy_LDADD =	\
	$(LINK_STATIC)		\
	$(OPENSSL_LIBS)		\
	$(GLIB_LIBS)		\
	$(LINK_DYNAMIC)

libsoup_ssl_proxy_SOURCES =	\
	soup-openssl.h		\
	soup-openssl.c		\
	soup-ssl-proxy.c

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(libexecdir)
	$(INSTALL_PROGRAM) libsoup-ssl-proxy $(DESTDIR)$(libexecdir)/$(SSL_PROXY_NAME)
