
libsoup-api.raw: libsoup.sources
	gapi2-parser libsoup.sources

libsoup-api.xml: libsoup-api.raw Soup.metadata
	cp libsoup-api.raw libsoup-api.xml
	gapi2-fixup --api=libsoup-api.xml --metadata=Soup.metadata

generated.stamp: libsoup-api.xml
	rm -f generated/*.cs; \
	gapi2-codegen --outdir=generated \
		--gluelib-name=soupsharpglue \
		--glue-filename=soup-glue.c \
		--glue-includes=libsoup/soup.h \
		--generate libsoup-api.xml && \
	touch generated.stamp

soup-sharp.dll: generated.stamp
	mcs -pkg:gtk-sharp-2.0 -target:library -unsafe -out:soup-sharp.dll generated/*.cs
