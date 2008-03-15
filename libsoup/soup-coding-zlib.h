/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 */

#ifndef SOUP_CODING_ZLIB_H
#define SOUP_CODING_ZLIB_H 1

#include "soup-coding.h"

#define SOUP_TYPE_CODING_ZLIB            (soup_coding_zlib_get_type ())
#define SOUP_CODING_ZLIB(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_CODING_ZLIB, SoupCodingZLib))
#define SOUP_CODING_ZLIB_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_CODING_ZLIB, SoupCodingZLibClass))
#define SOUP_IS_CODING_ZLIB(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_CODING_ZLIB))
#define SOUP_IS_CODING_ZLIB_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_CODING_ZLIB))
#define SOUP_CODING_ZLIB_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_CODING_ZLIB, SoupCodingZLibClass))

typedef struct {
	SoupCoding parent;

} SoupCodingZLib;

typedef struct {
	SoupCodingClass  parent_class;

} SoupCodingZLibClass;

GType soup_coding_zlib_get_type (void);

void soup_coding_zlib_init_encode (SoupCodingZLib *coding, int flags);
void soup_coding_zlib_init_decode (SoupCodingZLib *coding, int flags);

#endif /* SOUP_CODING_ZLIB_H */
