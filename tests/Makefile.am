INCLUDES =		\
	-I$(top_srcdir)	\
	$(GLIB_CFLAGS)

noinst_PROGRAMS = get timeserver simple-httpd

get_SOURCES = get.c
get_LDADD = $(top_builddir)/libsoup/libsoup-2.0.la

timeserver_SOURCES = timeserver.c
timeserver_LDADD = $(top_builddir)/libsoup/libsoup-2.0.la

simple_httpd_SOURCES = simple-httpd.c
simple_httpd_LDADD = $(top_builddir)/libsoup/libsoup-2.0.la

check_PROGRAMS = uri-parsing
TESTS = uri-parsing

uri_parsing_SOURCES = uri-parsing.c
uri_parsing_LDADD = $(top_builddir)/libsoup/libsoup-2.0.la
