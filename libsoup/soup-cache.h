/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Igalia S.L.
 */

#ifndef SOUP_CACHE_H
#define SOUP_CACHE_H 1

#include <libsoup/soup-types.h>

G_BEGIN_DECLS

#define SOUP_TYPE_CACHE		   (soup_cache_get_type ())
#define SOUP_CACHE(obj)		   (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_CACHE, SoupCache))
#define SOUP_CACHE_CLASS(klass)	   (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_CACHE, SoupCacheClass))
#define SOUP_IS_CACHE(obj)	   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_CACHE))
#define SOUP_IS_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_CACHE))
#define SOUP_CACHE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_CACHE, SoupCacheClass))

typedef struct _SoupCachePrivate SoupCachePrivate;

typedef enum {
	SOUP_CACHE_CACHEABLE   = (1 << 0),
	SOUP_CACHE_UNCACHEABLE = (1 << 1),
	SOUP_CACHE_INVALIDATES = (1 << 2)
} SoupCacheability;

struct _SoupCache {
	GObject parent_instance;

	SoupCachePrivate *priv;
};

typedef struct {
	GObjectClass parent_class;

	/* methods */
	SoupCacheability (*get_cacheability) (SoupCache *cache, SoupMessage *msg);

	/* Padding for future expansion */
	void (*_libsoup_reserved1) (void);
	void (*_libsoup_reserved2) (void);
	void (*_libsoup_reserved3) (void);
} SoupCacheClass;

GType            soup_cache_get_type         (void);
SoupCache*       soup_cache_new              (const char  *cache_dir);
gboolean         soup_cache_has_response     (SoupCache   *cache,
					      SoupMessage *msg);
void             soup_cache_send_response    (SoupCache   *cache,
					      SoupMessage *msg);
SoupCacheability soup_cache_get_cacheability (SoupCache   *cache,
					      SoupMessage *msg);
void             soup_cache_flush            (SoupCache   *cache);
void             soup_cache_clear            (SoupCache   *cache);

G_END_DECLS


#endif /* SOUP_CACHE_H */

