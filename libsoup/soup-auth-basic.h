/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_AUTH_BASIC_H
#define SOUP_AUTH_BASIC_H 1

#include "soup-auth.h"

#define SOUP_TYPE_AUTH_BASIC            (soup_auth_basic_get_type ())
#define SOUP_AUTH_BASIC(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), SOUP_TYPE_AUTH_BASIC, SoupAuthBasic))
#define SOUP_AUTH_BASIC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_AUTH_BASIC, SoupAuthBasicClass))
#define SOUP_IS_AUTH_BASIC(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), SOUP_TYPE_AUTH_BASIC))
#define SOUP_IS_AUTH_BASIC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SOUP_TYPE_AUTH_BASIC))

struct _SoupAuthBasic {
	SoupAuth parent;

	SoupAuthBasicPrivate *priv;
};

struct _SoupAuthBasicClass {
	SoupAuthClass  parent_class;

};

GType soup_auth_basic_get_type (void);

#endif /*SOUP_AUTH_BASIC_H*/
