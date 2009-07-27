/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-cache.c
 *
 * Copyright (C) 2009 Igalia S.L.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-cache.h"
#include "soup-date.h"
#include "soup-headers.h"
#include "soup-message.h"
#include "soup-session-feature.h"
#include "soup-uri.h"

#include <gio/gio.h>

static void soup_cache_session_feature_init (SoupSessionFeatureInterface *feature_interface, gpointer interface_data);

typedef struct _SoupCacheEntry
{
	char *key;
	char *filename;
	guint freshness_lifetime;
	gboolean must_revalidate;
	GString *data;
	gsize pos;
	guint date;
	gboolean writing;
	gboolean dirty;
	SoupMessageHeaders *headers;
	GOutputStream *stream;
	GError *error;
} SoupCacheEntry;

struct _SoupCachePrivate {
	char *cache_dir;
	GHashTable *cache;
};

enum {
	PROP_0,
	PROP_CACHE_DIR
};

typedef enum {
	SOUP_CACHE_CACHEABLE   = (1 << 0),
	SOUP_CACHE_UNCACHEABLE = (1 << 1),
	SOUP_CACHE_INVALIDATES = (1 << 2)
} SoupCacheability;

#define SOUP_CACHE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SOUP_TYPE_CACHE, SoupCachePrivate))

G_DEFINE_TYPE_WITH_CODE (SoupCache, soup_cache, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_SESSION_FEATURE,
						soup_cache_session_feature_init))

static SoupCacheability
get_cacheability (SoupMessage *msg)
{
	SoupCacheability cacheability;
	const char *cache_control;

	/* 1. The request method must be cacheable */
        if (msg->method == SOUP_METHOD_GET)
                cacheability = SOUP_CACHE_CACHEABLE;
        else if (msg->method == SOUP_METHOD_HEAD ||
                 msg->method == SOUP_METHOD_TRACE ||
                 msg->method == SOUP_METHOD_CONNECT)
                return SOUP_CACHE_UNCACHEABLE;
        else
                return (SOUP_CACHE_UNCACHEABLE | SOUP_CACHE_INVALIDATES);

	cache_control = soup_message_headers_get (msg->response_headers, "Cache-Control");
	if (cache_control) {
		GHashTable *hash;

		hash = soup_header_parse_param_list (cache_control);
		/* 2. The 'no-store' cache directive does not appear in the
		   headers */
		if (g_hash_table_lookup (hash, "no-store")) {
			soup_header_free_param_list (hash);
			return SOUP_CACHE_UNCACHEABLE;
		}

		/* This does not appear in section 2.1, but I think it makes
		   sense to check it too? */
		if (g_hash_table_lookup (hash, "no-cache")) {
			soup_header_free_param_list (hash);
			return SOUP_CACHE_UNCACHEABLE;
		}
	}

        switch (msg->status_code) {
        case SOUP_STATUS_PARTIAL_CONTENT:
        case SOUP_STATUS_NOT_MODIFIED:
                /* We don't cache partial responses, but they only
                 * invalidate cached full responses if the headers
                 * don't match. Likewise with 304 Not Modified.
                 */
                cacheability = SOUP_CACHE_UNCACHEABLE;
                break;

        case SOUP_STATUS_MULTIPLE_CHOICES:
        case SOUP_STATUS_MOVED_PERMANENTLY:
        case SOUP_STATUS_GONE:
                /* FIXME: cacheable unless indicated otherwise */
                cacheability = SOUP_CACHE_UNCACHEABLE;
                break;

        case SOUP_STATUS_FOUND:
        case SOUP_STATUS_TEMPORARY_REDIRECT:
                /* FIXME: cacheable if explicitly indicated */
                cacheability = SOUP_CACHE_UNCACHEABLE;
                break;

        case SOUP_STATUS_SEE_OTHER:
        case SOUP_STATUS_FORBIDDEN:
        case SOUP_STATUS_NOT_FOUND:
        case SOUP_STATUS_METHOD_NOT_ALLOWED:
                return (SOUP_CACHE_UNCACHEABLE | SOUP_CACHE_INVALIDATES);

        default:
                /* Any 5xx status or any 4xx status not handled above
                 * is uncacheable but doesn't break the cache.
                 */
                if ((msg->status_code >= SOUP_STATUS_BAD_REQUEST &&
                     msg->status_code <= SOUP_STATUS_FAILED_DEPENDENCY) ||
                    msg->status_code >= SOUP_STATUS_INTERNAL_SERVER_ERROR)
                        return SOUP_CACHE_UNCACHEABLE;

                /* An unrecognized 2xx, 3xx, or 4xx response breaks
                 * the cache.
                 */
                if ((msg->status_code > SOUP_STATUS_PARTIAL_CONTENT &&
                     msg->status_code < SOUP_STATUS_MULTIPLE_CHOICES) ||
                    (msg->status_code > SOUP_STATUS_TEMPORARY_REDIRECT &&
                     msg->status_code < SOUP_STATUS_INTERNAL_SERVER_ERROR))
                        return (SOUP_CACHE_UNCACHEABLE | SOUP_CACHE_INVALIDATES);
                break;
        }

	return cacheability;
}

static void
soup_cache_entry_free (SoupCacheEntry *entry)
{
	g_free (entry->filename);
	entry->filename = NULL;
	g_free (entry->key);
	entry->key = NULL;
	soup_message_headers_free (entry->headers);
	entry->headers = NULL;
	g_string_free (entry->data, TRUE);
	entry->data = NULL;
	if (entry->error) {
		g_error_free (entry->error);
		entry->error = NULL;
	}
	g_slice_free (SoupCacheEntry, entry);
}

static void
copy_headers (const char *name, const char *value, SoupMessageHeaders *headers)
{
	soup_message_headers_append (headers, name, value);
}

static guint
soup_cache_entry_get_current_age (SoupCacheEntry *entry)
{
	time_t now = time (NULL);

	return now - entry->date;
}

static gboolean
soup_cache_entry_is_fresh_enough (SoupCacheEntry *entry, int min_fresh)
{
	int limit = (min_fresh == -1) ? soup_cache_entry_get_current_age (entry) : min_fresh;

	g_debug ("IS ENTRY FRESH ENOUGH? LIFETIME %d, CURRENT AGE %d, MIN-FRESH %d, FRESH: %d (Key: %s)", entry->freshness_lifetime, soup_cache_entry_get_current_age (entry), min_fresh, (gboolean) entry->freshness_lifetime > limit, entry->key);
	return entry->freshness_lifetime > limit;
}

static char *
soup_message_get_cache_key (SoupMessage *msg)
{
	SoupURI *uri = soup_message_get_uri (msg);
	return soup_uri_to_string (uri, FALSE);
}

static void
soup_cache_entry_set_freshness (SoupCacheEntry *entry, SoupMessage *msg)
{
	const char *cache_control;
	const char *max_age, *expires, *date, *last_modified;
	GHashTable *hash;

	hash = NULL;

	cache_control = soup_message_headers_get (entry->headers, "Cache-Control");
	if (cache_control) {
		hash = soup_header_parse_param_list (cache_control);

		/* Should we re-validate the entry when it goes stale */
		entry->must_revalidate = (gboolean)g_hash_table_lookup (hash, "must-revalidate");

		/* If 'max-age' cache directive is present, use that */
		max_age = g_hash_table_lookup (hash, "max-age");
		if (max_age) {
			gint64 freshness_lifetime;

			freshness_lifetime = g_ascii_strtoll (max_age, NULL, 10);
			if (freshness_lifetime) {
				entry->freshness_lifetime = (guint) MIN (freshness_lifetime, G_MAXUINT32);
				soup_header_free_param_list (hash);
				return;
			}
		}
	}

	if (hash != NULL)
		soup_header_free_param_list (hash);

	/* If the 'Expires' response header is present, use its value
	   minus the value of the 'Date' response header */
	expires = soup_message_headers_get (entry->headers, "Expires");
	date = soup_message_headers_get (entry->headers, "Date");
	if (expires && date) {
		SoupDate *expires_d, *date_d;
		time_t expires_t, date_t;

		expires_d = soup_date_new_from_string (expires);
		date_d = soup_date_new_from_string (date);

		expires_t = soup_date_to_time_t (expires_d);
		date_t = soup_date_to_time_t (date_d);

		soup_date_free (expires_d);
		soup_date_free (date_d);

		if (expires_t && date_t) {
			entry->freshness_lifetime = (guint) MAX (expires_t - date_t, G_MAXUINT32);
			return;
		}
	}

	/* Otherwise an heuristic may be used */

	last_modified = soup_message_headers_get (entry->headers, "Last-Modified");
	if (last_modified &&
	    (msg->status_code == SOUP_STATUS_OK ||
	     msg->status_code == SOUP_STATUS_NON_AUTHORITATIVE ||
	     /*msg->status_code == SOUP_STATUS_PARTIAL_CONTENT || */
	     msg->status_code == SOUP_STATUS_MULTIPLE_CHOICES ||
	     msg->status_code == SOUP_STATUS_MOVED_PERMANENTLY ||
	     msg->status_code == SOUP_STATUS_GONE)) {
		SoupDate *soup_date;
		time_t now, last_modified_t;

		soup_date = soup_date_new_from_string (last_modified);
		last_modified_t = soup_date_to_time_t (soup_date);
		now = time (NULL);

#define HEURISTIC_FACTOR 0.1 /* From Section 2.3.1.1 */

		entry->freshness_lifetime = MAX (0, (now - last_modified_t) * HEURISTIC_FACTOR);
		soup_date_free (soup_date);
		return;
	}

	/* If all else fails, make the entry expire immediately */
	entry->freshness_lifetime = 0;
}

static SoupCacheEntry *
soup_cache_entry_new (SoupCache *cache, SoupMessage *msg)
{
	SoupCacheEntry *entry;
	SoupMessageHeaders *headers;
	const char *date;
	char *md5;

	entry = g_slice_new0 (SoupCacheEntry);
	entry->dirty = TRUE;
	entry->writing = FALSE;
	entry->data = g_string_new (NULL);
	entry->pos = 0;
	entry->error = NULL;

	/* key & filename */
	entry->key = soup_message_get_cache_key (msg);
	md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, entry->key, -1);
	entry->filename = g_build_filename (cache->priv->cache_dir, md5, NULL);
	g_free (md5);

	/* Headers */
	headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
	soup_message_headers_foreach (msg->response_headers,
				      (SoupMessageHeadersForeachFunc)copy_headers,
				      headers);
	entry->headers = headers;

	/* Section 2.3.1, Freshness Lifetime */
	soup_cache_entry_set_freshness (entry, msg);

	/* Section 2.3.2, Calculating Age */
	date = soup_message_headers_get (entry->headers, "Date");

	if (date) {
		SoupDate *soup_date;
		soup_date = soup_date_new_from_string (date);
	
		entry->date = soup_date_to_time_t (soup_date);
		soup_date_free (soup_date);
	} else {
		entry->date = time (NULL);
	}

	return entry;
}

static SoupCacheEntry *
soup_cache_lookup_uri (SoupCache *cache, const char *uri)
{
	SoupCachePrivate *priv;
	SoupCacheEntry *entry;

	priv = cache->priv;

	entry = g_hash_table_lookup (priv->cache, uri);

	return entry;
}

static void
close_ready_cb (GObject *source, GAsyncResult *result, SoupCacheEntry *entry)
{
	GOutputStream *stream = G_OUTPUT_STREAM (source);

	g_output_stream_close_finish (stream, result, NULL);
	g_object_unref (stream);

	entry->stream = NULL;
	entry->dirty = FALSE;
	entry->writing = FALSE;
	entry->pos = 0;
}

static void
write_ready_cb (GObject *source, GAsyncResult *result, SoupCacheEntry *entry)
{
	GOutputStream *stream = G_OUTPUT_STREAM (source);
	GError *error = NULL;
	gssize write_size;

	write_size = g_output_stream_write_finish (stream, result, &error);
	if (write_size <= 0 || error) {
		if (error)
			entry->error = error;
		g_output_stream_close_async (stream,
					     G_PRIORITY_DEFAULT,
					     NULL,
					     (GAsyncReadyCallback)close_ready_cb,
					     entry);
		/* FIXME: We should completely stop caching the
		   resource at this point */
	} else {
		entry->pos += write_size;

		/* Is there new data to write already ? */
		if (entry->pos < entry->data->len) {
			g_output_stream_write_async (entry->stream,
						     entry->data->str + entry->pos,
						     entry->data->len - entry->pos,
						     G_PRIORITY_DEFAULT,
						     NULL,
						     (GAsyncReadyCallback)write_ready_cb,
						     entry);
		} else
			entry->writing = FALSE;
	}
}

static void
msg_got_chunk_cb (SoupMessage *msg, SoupBuffer *chunk, SoupCacheEntry *entry)
{
	g_return_if_fail (chunk->data && chunk->length);
	g_return_if_fail (entry);
	g_return_if_fail (G_IS_OUTPUT_STREAM (entry->stream));

	g_string_append_len (entry->data, chunk->data, chunk->length);

	/* FIXME: remove the error check when we cancel the caching at
	   the first write error */
	if (entry->writing == FALSE && entry->error == NULL) {
		GString *data = entry->data;
		entry->writing = TRUE;
		g_output_stream_write_async (entry->stream,
					     data->str + entry->pos,
					     data->len - entry->pos,
					     G_PRIORITY_DEFAULT,
					     NULL,
					     (GAsyncReadyCallback)write_ready_cb,
					     entry);
	}
}

static void
msg_got_body_cb (SoupMessage *msg, SoupCacheEntry *entry)
{
	g_return_if_fail (entry);
	g_return_if_fail (G_IS_OUTPUT_STREAM (entry->stream));

	g_output_stream_close_async (entry->stream,
				     G_PRIORITY_DEFAULT,
				     NULL,
				     (GAsyncReadyCallback)close_ready_cb,
				     entry);
}

static void
soup_cache_entry_delete_by_key (SoupCache *cache, const char *key)
{
	GFile *file;
	char *md5, *filename;

	/* Delete cache file */
	md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, key, -1);
	filename = g_build_filename (cache->priv->cache_dir, md5, NULL);
	file = g_file_new_for_path (filename);
	g_file_delete (file, NULL, NULL);
	g_free (md5);
	g_free (filename);
	g_object_unref (file);

	/* Remove from cache */
	g_hash_table_remove (cache->priv->cache, key);

}

static void
msg_restarted_cb (SoupMessage *msg, SoupCacheEntry *entry)
{
	/* FIXME: What should we do here exactly? */
}

static void
msg_got_headers_cb (SoupMessage *msg, SoupCache *cache)
{
	SoupCacheability cacheable;

	cacheable = get_cacheability (msg);

	if (cacheable & SOUP_CACHE_CACHEABLE) {
		SoupCacheEntry *entry;
		char *key;
		GFile *file;

		/* Check if we are already caching this resource */
		key = soup_message_get_cache_key (msg);
		entry = soup_cache_lookup_uri (cache, key);
		g_free (key);

		if (entry && entry->dirty) {
			g_debug ("ALREADY GOT AN ENTRY, IS IT DIRTY?: %d (%s)", entry->dirty, entry->key);
			return;
		}

		/* Create a new entry, deleting any old one if
		   present */
		entry = soup_cache_entry_new (cache, msg);
		soup_cache_entry_delete_by_key (cache, entry->key);

		g_hash_table_insert (cache->priv->cache, g_strdup (entry->key), entry);

		/* Prepare entry */
		file = g_file_new_for_path (entry->filename);
		entry->stream = (GOutputStream*)g_file_append_to (file, 0, NULL, NULL);
		g_object_unref (file);

		g_signal_connect (msg, "got-chunk", G_CALLBACK (msg_got_chunk_cb), entry);
		g_signal_connect (msg, "got-body", G_CALLBACK (msg_got_body_cb), entry);
		g_signal_connect (msg, "restarted", G_CALLBACK (msg_restarted_cb), entry);
	} else if (cacheable & SOUP_CACHE_INVALIDATES) {
		char *key;

		key = soup_message_get_cache_key (msg);
		soup_cache_entry_delete_by_key (cache, key);
		g_free (key);
	}
}

gboolean
soup_cache_has_response (SoupCache *cache, SoupSession *session, SoupMessage *msg)
{
	char *key;
	SoupCacheEntry *entry;
	const char *cache_control;
	GHashTable *hash;
	gpointer value;
	gboolean must_revalidate;
	int max_age, max_stale, min_fresh;
	SoupURI *uri = soup_message_get_uri (msg);

	key = soup_message_get_cache_key (msg);
	entry = soup_cache_lookup_uri (cache, key);

	/* 1. The presented Request-URI and that of stored response
	   match */
	if (!entry) return FALSE;

	if (entry->dirty) return FALSE;

	/* 2. The request method associated with the stored response
	   allows it to be used for the presented request */

	/* In practice this means we only return our resource for GET,
	   cacheability for other methods is a TODO in the RFC
	   (TODO: although we could return the headers for HEAD
	   probably). */
	if (msg->method != SOUP_METHOD_GET) return FALSE;

	/* 3. Selecting request-headers nominated by the stored
	   response (if any) match those presented. */

	/* TODO */

	/* 4. The presented request and stored response are free from
	   directives that would prevent its use. */

	must_revalidate = FALSE;
	max_age = max_stale = min_fresh = -1;

	cache_control = soup_message_headers_get (msg->request_headers, "Cache-Control");
	g_debug ("Cache-Control for %s: %s", soup_uri_to_string (uri, FALSE), cache_control);
	if (cache_control) {
		hash = soup_header_parse_param_list (cache_control);

		if (g_hash_table_lookup_extended (hash, "no-store", NULL, NULL)) {
			g_debug ("NO-STORE, OUT");
			g_hash_table_destroy (hash);
			return FALSE;
		}

		if (g_hash_table_lookup_extended (hash, "no-cache", NULL, NULL)) {
			entry->must_revalidate = TRUE;
		}

		value = g_hash_table_lookup (hash, "max-age");
		if (value) {
			SoupURI *uri = soup_message_get_uri (msg);
			max_age = (int)MIN (g_ascii_strtoll (value, NULL, 10), G_MAXINT32);
			g_debug ("Set max-age to %d (resource %s)", max_age, soup_uri_to_string (uri, FALSE));
		}

		/* max-stale can have no value set, we need to use _extended */
		if (g_hash_table_lookup_extended (hash, "max-stale", NULL, &value)) {
			if (value)
				max_stale = (int)MIN (g_ascii_strtoll (value, NULL, 10), G_MAXINT32);
			else
				max_stale = G_MAXINT32;
		}

		value = g_hash_table_lookup (hash, "min-fresh");
		if (value)
			min_fresh = (int)MIN (g_ascii_strtoll (value, NULL, 10), G_MAXINT32);

		g_hash_table_destroy (hash);

		if (max_age != -1) {
			guint current_age = soup_cache_entry_get_current_age (entry);

			/* If we are over max-age and max-stale is not set, do
			   not use the value from the cache */
			if (max_age <= current_age && max_stale == -1) {
				g_debug ("OVER MAX-AGE (%d) AND NO MAX-STALE (%d), OUT", max_age, max_stale);
				return FALSE;
			}
		}
	}

	/* 5. The stored response is either: fresh, allowed to be
	   served stale or succesfully validated */
	if (entry->must_revalidate) {
		return FALSE;
	}

	if (!soup_cache_entry_is_fresh_enough (entry, min_fresh)) {
		/* Not fresh, can it be served stale? */
		if (max_stale != -1) {
			/* G_MAXINT32 means we accept any staleness */
			if (max_stale == G_MAXINT32)
				return TRUE;

			if ((soup_cache_entry_get_current_age (entry) - entry->freshness_lifetime) <= max_stale)
				return TRUE;
		}

		return FALSE;
	}

	g_debug ("RETURNING FROM CACHE: %s (lifetime %d)", soup_uri_to_string (uri, FALSE), entry->freshness_lifetime);
	return TRUE;
}

void
soup_cache_send_response (SoupCache *cache, SoupSession *session, SoupMessage *msg)
{
	char *key;
	SoupCacheEntry *entry;
	SoupBuffer *buffer;
	char *current_age;

	key = soup_message_get_cache_key (msg);
	entry = soup_cache_lookup_uri (cache, key);
	g_return_if_fail (entry);

	/* Headers */
	soup_message_headers_foreach (entry->headers,
				      (SoupMessageHeadersForeachFunc)copy_headers,
				      msg->response_headers);

	/* Add 'Age' header with the current age */
	current_age = g_strdup_printf ("%d", soup_cache_entry_get_current_age (entry));
	soup_message_headers_replace (msg->response_headers,
				      "Age",
				      current_age);
	g_free (current_age);

	g_signal_emit_by_name (msg, "got-headers", NULL);

	/* Data */
	/* Do not try to read anything if the length of the
	   resource is 0 */
	if (entry->data->len) {
		char *data;
		gsize length;

		g_file_get_contents (entry->filename, &data, &length, NULL);

		if (data && length) {
			buffer = soup_buffer_new (SOUP_MEMORY_TEMPORARY, data, length);
			soup_message_body_append_buffer (msg->response_body, buffer);
			g_signal_emit_by_name (msg, "got-chunk", buffer, NULL);
			soup_buffer_free (buffer);
		}
	}

	soup_message_got_body (msg);
	soup_message_finished (msg);
}

static void
request_started (SoupSessionFeature *feature, SoupSession *session,
		 SoupMessage *msg, SoupSocket *socket)
{
	char *key;
	key = soup_message_get_cache_key (msg);
	g_debug ("REQUEST-STARTED %s", key);
	g_free (key);

	g_signal_connect (msg, "got-headers", G_CALLBACK (msg_got_headers_cb), feature);
}

static void
soup_cache_session_feature_init (SoupSessionFeatureInterface *feature_interface,
                                 gpointer interface_data)
{
	feature_interface->request_started = request_started;
}

static void
soup_cache_init (SoupCache *cache)
{
	SoupCachePrivate *priv;

	priv = cache->priv = SOUP_CACHE_GET_PRIVATE (cache);

	priv->cache = g_hash_table_new_full (g_str_hash,
					     g_str_equal,
					     (GDestroyNotify)g_free,
					     (GDestroyNotify)soup_cache_entry_free);
}

static void
soup_cache_finalize (GObject *object)
{
	SoupCachePrivate *priv;

	priv = SOUP_CACHE (object)->priv;

	g_hash_table_destroy (priv->cache);
	g_free (priv->cache_dir);

	G_OBJECT_CLASS (soup_cache_parent_class)->finalize (object);
}

static void
soup_cache_set_property (GObject *object, guint prop_id,
			 const GValue *value, GParamSpec *pspec)
{
	SoupCachePrivate *priv = SOUP_CACHE (object)->priv;

	switch (prop_id) {
	case PROP_CACHE_DIR:
		priv->cache_dir = g_value_dup_string (value);
		/* Create directory if it does not exist (FIXME: should we?) */
		if (!g_file_test (priv->cache_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
			g_mkdir_with_parents (priv->cache_dir, 0700);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
soup_cache_get_property (GObject *object, guint prop_id,
			 GValue *value, GParamSpec *pspec)
{
	SoupCachePrivate *priv = SOUP_CACHE (object)->priv;

	switch (prop_id) {
	case PROP_CACHE_DIR:
		g_value_set_string (value, priv->cache_dir);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
soup_cache_constructed (GObject *object)
{
	SoupCachePrivate *priv;

	priv = SOUP_CACHE (object)->priv;

	if (!priv->cache_dir) {
		/* Set a default cache dir, different for each user */
		priv->cache_dir = g_build_filename (g_get_user_cache_dir (),
						    "httpcache",
						    NULL);
		if (!g_file_test (priv->cache_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
			g_mkdir_with_parents (priv->cache_dir, 0700);
	}

	if (G_OBJECT_CLASS (soup_cache_parent_class)->constructed)
		G_OBJECT_CLASS (soup_cache_parent_class)->constructed (object);
}

static void
soup_cache_class_init (SoupCacheClass *cache_class)
{
	GObjectClass *gobject_class = (GObjectClass*)cache_class;

	gobject_class->finalize = soup_cache_finalize;
	gobject_class->constructed = soup_cache_constructed;
	gobject_class->set_property = soup_cache_set_property;
	gobject_class->get_property = soup_cache_get_property;

	g_object_class_install_property(gobject_class, PROP_CACHE_DIR,
					g_param_spec_string("cache-dir",
							    "Cache directory",
							    "The directory to store the cache files",
							    NULL,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private(cache_class, sizeof (SoupCachePrivate));
}

/**
 * soup_cache_new:
 * @cache_dir: the directory to store the cached data, or %NULL to use the default one
 *
 * Creates a new #SoupCache.
 *
 * Returns: a new #SoupCache
 *
 * Since: 2.28
 **/
SoupCache *
soup_cache_new (const char *cache_dir)
{
	return g_object_new (SOUP_TYPE_CACHE,
			     "cache-dir", cache_dir,
			     NULL);
}

