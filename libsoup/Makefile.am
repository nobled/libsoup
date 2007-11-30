## Process this file with automake to produce Makefile.in

if OS_WIN32
LIBWS2_32 = -lws2_32
endif

INCLUDES = 				\
	-DG_LOG_DOMAIN=\"libsoup\" 	\
	-I$(top_srcdir)			\
	$(SOUP_DEBUG_FLAGS)		\
	$(GLIB_CFLAGS)			\
	$(XML_CFLAGS)			\
	$(LIBGCRYPT_CFLAGS)		\
	$(LIBGNUTLS_CFLAGS)

MARSHAL_GENERATED = soup-marshal.c soup-marshal.h

soup-marshal.h: soup-marshal.list
	( $(GLIB_GENMARSHAL) --prefix=soup_marshal $(srcdir)/soup-marshal.list --header > soup-marshal.tmp \
	&& mv soup-marshal.tmp soup-marshal.h ) \
	|| ( rm -f soup-marshal.tmp && exit 1 )

soup-marshal.c: soup-marshal.h
	( (echo '#include "soup-marshal.h"'; $(GLIB_GENMARSHAL) --prefix=soup_marshal $(srcdir)/soup-marshal.list --body) > soup-marshal.tmp \
	&& mv soup-marshal.tmp soup-marshal.c ) \
	|| ( rm -f soup-marshal.tmp && exit 1 )

soup-enum-types.h: $(soup_headers)
	( cd $(srcdir) && $(GLIB_MKENUMS) \
			--fhead "#ifndef __SOUP_ENUM_TYPES_H__\n#define __SOUP_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
			--fprod "/* enumerations from \"@filename@\" */\n" \
			--vhead "GType @enum_name@_get_type (void);\n#define SOUP_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
			--ftail "G_END_DECLS\n\n#endif /* __SOUP_ENUM_TYPES_H__ */" \
		$(soup_headers) ) > soup-enum-types.h.tmp \
	&& mv soup-enum-types.h.tmp soup-enum-types.h \
	|| rm -f soup-enum-type.h.tmp

soup-enum-types.c: $(libsoupinclude_HEADERS)
	( cd $(srcdir) && $(GLIB_MKENUMS) \
			--fhead "#include <libsoup/soup.h>" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
			--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
		$(soup_headers) ) > soup-enum-types.c.tmp \
	&& mv soup-enum-types.c.tmp soup-enum-types.c \
	|| rm -f soup-enum-type.c.tmp

BUILT_SOURCES = $(MARSHAL_GENERATED)

CLEANFILES = $(MARSHAL_GENERATED)

DISTCLEANFILES = soup-enum-types.h soup-enum-types.c

libsoupincludedir = $(includedir)/libsoup-2.2/libsoup

soup_headers =			\
	soup.h			\
	soup-address.h		\
	soup-connection.h	\
	soup-date.h		\
	soup-headers.h		\
	soup-message.h		\
	soup-message-filter.h	\
	soup-message-headers.h	\
	soup-message-queue.h	\
	soup-method.h		\
	soup-misc.h     	\
	soup-portability.h	\
	soup-server-auth.h	\
	soup-server-message.h	\
	soup-server.h		\
	soup-session.h		\
	soup-session-async.h	\
	soup-session-sync.h	\
	soup-soap-message.h	\
	soup-soap-response.h	\
	soup-socket.h		\
	soup-status.h		\
	soup-types.h		\
	soup-uri.h		\
	soup-xmlrpc-message.h	\
	soup-xmlrpc-response.h

libsoupinclude_HEADERS =	\
	$(soup_headers)		\
	soup-enum-types.h

lib_LTLIBRARIES = libsoup-2.2.la

libsoup_2_2_la_LDFLAGS =	\
	-version-info $(SOUP_CURRENT):$(SOUP_REVISION):$(SOUP_AGE) -no-undefined

libsoup_2_2_la_LIBADD =			\
	$(GLIB_LIBS)			\
	$(XML_LIBS)			\
	$(LIBGNUTLS_LIBS_STATIC)	\
	$(LIBGNUTLS_LIBS)		\
	$(LIBGCRYPT_LIBS)		\
	$(LIBWS2_32)

libsoup_2_2_la_SOURCES =		\
	$(MARSHAL_GENERATED)		\
	soup-address.c			\
	soup-auth.h			\
	soup-auth.c			\
	soup-auth-basic.h		\
	soup-auth-basic.c		\
	soup-auth-digest.h		\
	soup-auth-digest.c		\
	soup-connection.c		\
	soup-connection-ntlm.h		\
	soup-connection-ntlm.c		\
	soup-date.c			\
	soup-enum-types.c		\
	soup-dns.h			\
	soup-dns.c			\
	soup-gnutls.c			\
	soup-headers.c			\
	soup-md5-utils.h		\
	soup-md5-utils.c		\
	soup-message.c			\
	soup-message-client-io.c	\
	soup-message-filter.c		\
	soup-message-handlers.c		\
	soup-message-headers.c		\
	soup-message-io.c		\
	soup-message-private.h		\
	soup-message-queue.c		\
	soup-message-server-io.c	\
	soup-method.c			\
	soup-misc.c     		\
	soup-nossl.c     		\
	soup-server.c			\
	soup-server-auth.c		\
	soup-server-message.c		\
	soup-session.c			\
	soup-session-async.c		\
	soup-session-sync.c		\
	soup-soap-message.c		\
	soup-soap-response.c		\
	soup-socket.c			\
	soup-ssl.h			\
	soup-status.c			\
	soup-uri.c			\
	soup-xmlrpc-message.c		\
	soup-xmlrpc-response.c

EXTRA_DIST= soup-marshal.list
