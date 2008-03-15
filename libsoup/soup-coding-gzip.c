/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-coding-gzip.c: "gzip" coding
 *
 * Copyright (C) 2005 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-coding-gzip.h"

G_DEFINE_TYPE (SoupCodingGzip, soup_coding_gzip, SOUP_TYPE_CODING_ZLIB)

static void constructed (GObject *object);

static void
soup_coding_gzip_init (SoupCodingGzip *gzip)
{
}

static void
soup_coding_gzip_class_init (SoupCodingGzipClass *gzip_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (gzip_class);
	SoupCodingClass *coding_class = SOUP_CODING_CLASS (gzip_class);

	coding_class->name = "gzip";

	object_class->constructed = constructed;
}

static void
constructed (GObject *object)
{
	guint flags;

	/* More zlib magic numbers: 15 just because, and 16 to indicate
	 * gzip format.
	 */
	flags = 15 | 16;

	if (SOUP_CODING (object)->direction == SOUP_CODING_ENCODE)
		soup_coding_zlib_init_encode (SOUP_CODING_ZLIB (object), flags);
	else
		soup_coding_zlib_init_decode (SOUP_CODING_ZLIB (object), flags);
}
