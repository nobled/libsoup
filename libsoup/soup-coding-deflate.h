/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
 */

#ifndef SOUP_CODING_DEFLATE_H
#define SOUP_CODING_DEFLATE_H 1

#include "soup-coding-zlib.h"

#define SOUP_TYPE_CODING_DEFLATE            (soup_coding_deflate_get_type ())
#define SOUP_CODING_DEFLATE(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_CODING_DEFLATE, SoupCodingDeflate))
#define SOUP_CODING_DEFLATE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_CODING_DEFLATE, SoupCodingDeflateClass))
#define SOUP_IS_CODING_DEFLATE(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_CODING_DEFLATE))
#define SOUP_IS_CODING_DEFLATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_CODING_DEFLATE))
#define SOUP_CODING_DEFLATE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_CODING_DEFLATE, SoupCodingDeflateClass))

typedef struct {
	SoupCodingZLib parent;

} SoupCodingDeflate;

typedef struct {
	SoupCodingZLibClass  parent_class;

} SoupCodingDeflateClass;

GType soup_coding_deflate_get_type (void);

SoupCoding *soup_coding_deflate_new (SoupCodingDirection direction);

#endif /* SOUP_CODING_DEFLATE_H */
