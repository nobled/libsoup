AUTOMAKE_OPTIONS=1.5

INCLUDES =			\
	$(PYTHON_INCLUDES)	\
	$(PYGOBJECT_CFLAGS)	\
	$(XML_CFLAGS)		\
	-I..

pyexec_LTLIBRARIES = soup.la
soup_la_LDFLAGS = -module -avoid-version -export-symbols-regex initsoup
soup_la_LIBADD = $(top_builddir)/libsoup/libsoup-$(SOUP_API_VERSION).la
soup_la_SOURCES = libsoupmodule.c
nodist_soup_la_SOURCES = libsoup.c
libsoup.c: libsoup.defs libsoup.override
CLEANFILES = libsoup.c
EXTRA_DIST = libsoup.override

.defs.c:
	(cd $(srcdir)\
	 && pygtk-codegen-2.0 \
		--override $*.override \
		--prefix py$* $*.defs) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c
