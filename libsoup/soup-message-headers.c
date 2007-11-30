/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-message-headers.c: HTTP message header arrays
 *
 * Copyright (C) 2005 Novell, Inc.
 */

#include "soup-message-headers.h"
#include "soup-misc.h"

GType
soup_message_headers_get_type (void)
{
	static GType type = 0;

	if (type == 0)
		type = g_pointer_type_register_static ("SoupMessageHeaders");
	return type;
}

/**
 * soup_message_headers_new:
 *
 * Creates a #SoupMessageHeaders
 *
 * Return value: a new #SoupMessageHeaders
 **/
SoupMessageHeaders *
soup_message_headers_new (void)
{
	GHashTable *hdrs = g_hash_table_new (soup_str_case_hash,
					     soup_str_case_equal);

	return (SoupMessageHeaders *)hdrs;
}

/**
 * soup_message_headers_free:
 * @hdrs: a #SoupMessageHeaders
 *
 * Frees @hdrs.
 **/
void
soup_message_headers_free (SoupMessageHeaders *hdrs)
{
	soup_message_headers_clear (hdrs);
	g_hash_table_destroy ((GHashTable *)hdrs);
}

static gboolean
free_header_list (gpointer name, gpointer vals, gpointer user_data)
{
	g_free (name);
	g_slist_foreach (vals, (GFunc) g_free, NULL);
	g_slist_free (vals);

	return TRUE;
}

/**
 * soup_message_headers_clear:
 * @hdrs: a #SoupMessageHeaders
 *
 * Clears @hdrs.
 **/
void
soup_message_headers_clear (SoupMessageHeaders *hdrs)
{
	g_hash_table_foreach_remove ((GHashTable *)hdrs,
				     free_header_list, NULL);
}

/**
 * soup_message_headers_append:
 * @hdrs: a #SoupMessageHeaders
 * @name: the header name to add
 * @value: the new value of @name
 *
 * Appends a new header with name @name and value @value to @hdrs. If
 * there were already other instances of header @name in @hdrs, they
 * are preserved.
 **/
void
soup_message_headers_append (SoupMessageHeaders *hdrs,
			     const char *name, const char *value)
{
	GHashTable *hash = (GHashTable *)hdrs;
	GSList *old_value;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (name != NULL && name [0] != '\0');
	g_return_if_fail (value != NULL);

	old_value = g_hash_table_lookup (hash, name);

	if (old_value)
		old_value = g_slist_append (old_value, g_strdup (value));
	else {
		g_hash_table_insert (hash, g_strdup (name),
				     g_slist_append (NULL, g_strdup (value)));
	}
}

/**
 * soup_message_headers_replace:
 * @hdrs: a #SoupMessageHeaders
 * @name: the header name to replace
 * @value: the new value of @name
 *
 * Replaces the value of the header @name in @hdrs with @value. If
 * there were previously multiple values for @name, all of the other
 * values are removed.
 **/
void
soup_message_headers_replace (SoupMessageHeaders *hdrs,
			      const char *name, const char *value)
{
	soup_message_headers_remove (hdrs, name);
	soup_message_headers_append (hdrs, name, value);
}

/**
 * soup_message_headers_remove:
 * @hdrs: a #SoupMessageHeaders
 * @name: the header name to remove
 *
 * Removes @name from @hdrs. If there are multiple values for @name,
 * they are all removed.
 **/
void
soup_message_headers_remove (SoupMessageHeaders *hdrs, const char *name)
{
	GHashTable *hash = (GHashTable *)hdrs;
	gpointer old_key, old_vals;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (name != NULL && name[0] != '\0');

	if (g_hash_table_lookup_extended (hash, name, &old_key, &old_vals)) {
		g_hash_table_remove (hash, name);
		free_header_list (old_key, old_vals, NULL);
	}
}

/**
 * soup_message_headers_find:
 * @hdrs: a #SoupMessageHeaders
 * @name: header name
 * 
 * Finds the first header in @hdrs with name @name.
 * 
 * Return value: the header's value or %NULL if not found.
 **/

/**
 * soup_message_headers_find_nth:
 * @hdrs: a #SoupMessageHeaders
 * @name: header name
 * @nth: which instance of header @name to find
 * 
 * Finds the @nth header in @hdrs with name @name (counting from 0).
 * 
 * Return value: the header's value or %NULL if not found.
 **/
const char *
soup_message_headers_find_nth (SoupMessageHeaders *hdrs,
			       const char *name, int nth)
{
	GHashTable *hash = (GHashTable *)hdrs;
	GList *vals;

	g_return_val_if_fail (hash != NULL, NULL);
	g_return_val_if_fail (name != NULL && name [0] != '\0', NULL);

	vals = g_hash_table_lookup (hash, name);
	vals = g_list_nth (vals, nth);
	return vals ? vals->data : NULL;
}

typedef struct {
	SoupMessageHeadersForeachFunc func;
	gpointer user_data;
} SoupMessageHeadersForeachData;

static void
foreach_value_in_list (gpointer name, gpointer value, gpointer user_data)
{
	GSList *vals = value;
	SoupMessageHeadersForeachData *data = user_data;

	while (vals) {
		(*data->func) (name, vals->data, data->user_data);
		vals = vals->next;
	}
}

/**
 * soup_message_headers_foreach:
 * @hdrs: a #SoupMessageHeaders
 * @func: callback function to run for each header
 * @user_data: data to pass to @func
 * 
 * Calls @func once for each header value in @hdrs. (If there are
 * headers with multiple values, @func will be called once on each
 * value.)
 **/
void
soup_message_headers_foreach (SoupMessageHeaders *hdrs,
			      SoupMessageHeadersForeachFunc func,
			      gpointer            user_data)
{
	GHashTable *hash = (GHashTable *)hdrs;
	SoupMessageHeadersForeachData data;

	g_return_if_fail (hash != NULL);
	g_return_if_fail (func != NULL);

	data.func = func;
	data.user_data = user_data;
	g_hash_table_foreach (hash, foreach_value_in_list, &data);
}
