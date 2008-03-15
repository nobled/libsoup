/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-coding-deflate.c: "deflate" coding
 *
 * Copyright (C) 2005 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-coding-deflate.h"

typedef struct {
	gboolean decode_inited, havebyte;
	guchar byte[2];

} SoupCodingDeflatePrivate;
#define SOUP_CODING_DEFLATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SOUP_TYPE_CODING_DEFLATE, SoupCodingDeflatePrivate))

G_DEFINE_TYPE (SoupCodingDeflate, soup_coding_deflate, SOUP_TYPE_CODING_ZLIB)

static SoupCodingClass *zlib_class;

static void             constructed (GObject *object);
static SoupCodingStatus apply_into  (SoupCoding *coding,
				     gconstpointer input, gsize input_length,
				     gsize *input_used,
				     gpointer output, gsize output_length,
				     gsize *output_used,
				     gboolean done, GError **error);

static void
soup_coding_deflate_init (SoupCodingDeflate *deflate)
{
}

static void
soup_coding_deflate_class_init (SoupCodingDeflateClass *deflate_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (deflate_class);
	SoupCodingClass *coding_class = SOUP_CODING_CLASS (deflate_class);

	g_type_class_add_private (deflate_class, sizeof (SoupCodingDeflatePrivate));

	coding_class->name = "deflate";

	zlib_class = SOUP_CODING_CLASS (soup_coding_deflate_parent_class);

	object_class->constructed = constructed;

	coding_class->apply_into  = apply_into;
}

static void
constructed (GObject *object)
{
	if (SOUP_CODING (object)->direction == SOUP_CODING_ENCODE)
		soup_coding_zlib_init_encode (SOUP_CODING_ZLIB (object), 15);
}

static SoupCodingStatus
apply_into (SoupCoding *coding,
	    gconstpointer input, gsize input_length, gsize *input_used,
	    gpointer output, gsize output_length, gsize *output_used,
	    gboolean done, GError **error)
{
	SoupCodingDeflatePrivate *priv =
		SOUP_CODING_DEFLATE_GET_PRIVATE (coding);

	if (coding->direction == SOUP_CODING_DECODE && !priv->decode_inited) {
		/* The "deflate" content/transfer coding is supposed
		 * to be the RFC1950 "zlib" format wrapping the
		 * RFC1951 "deflate" method. However, apparently many
		 * implementations have gotten this wrong and used raw
		 * "deflate" instead. So we check the first two bytes
		 * to see if they look like a zlib header, and fall
		 * back to raw deflate if they don't. This is slightly
		 * tricky because in the pathological case, we might
		 * be called with only a single byte on the first
		 * call.
		 */

		if (input_length == 0) {
			*input_used = *output_used = 0;
			return SOUP_CODING_STATUS_OK;
		} else if (input_length == 1 && !priv->havebyte) {
			priv->byte[0] = ((guchar *)input)[0];
			priv->havebyte = TRUE;
			*input_used = 1;
			*output_used = 0;
			return SOUP_CODING_STATUS_OK;
		}

		/* OK, one way or another we have two bytes now. */
		if (priv->havebyte)
			priv->byte[1] = ((guchar *)input)[0];
		else {
			priv->byte[0] = ((guchar *)input)[0];
			priv->byte[1] = ((guchar *)input)[1];
		}

		/* A "zlib" stream starts with 0x8 in the lower three
		 * bits of the first byte, and the first two bytes
		 * together taken as a big-endian integer are
		 * divisible by 31.
		 */
		if (((priv->byte[0] & 0x8) == 0x8) &&
		    ((priv->byte[0] << 8 & priv->byte[1]) % 31 == 0)) {
			/* "zlib" */
			soup_coding_zlib_init_decode (SOUP_CODING_ZLIB (coding), 15);
		} else {
			/* "deflate" */
			soup_coding_zlib_init_decode (SOUP_CODING_ZLIB (coding), -15);
		}

		priv->decode_inited = TRUE;

		if (priv->havebyte) {
			gsize dummy_input_used;

			/* Push the previously-read byte into the
			 * coder and then force the caller to call us
			 * again with the same input data.
			 */
			*input_used = 0;
			return zlib_class->apply_into (
				coding, priv->byte, 1, &dummy_input_used,
				output, output_length, output_used,
				done, error);
		}
	}

	return zlib_class->apply_into (
		coding, input, input_length, input_used,
		output, output_length, output_used,
		done, error);
}
