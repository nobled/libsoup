/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-message-headers.c: HTTP message header arrays
 *
 * Copyright (C) 2005 Novell, Inc.
 */

#include "soup-message-headers.h"
#include "soup-misc.h"

typedef void (*SoupHeaderSetter) (SoupMessageHeaders *, const char *);
static const char *intern_header_name (const char *name, SoupHeaderSetter *setter);

typedef struct {
	const char *name;
	char *value;
} SoupHeader;

struct SoupMessageHeaders {
	GArray *array;
	SoupMessageHeadersType type;

	SoupEncoding encoding;
	gsize content_length;
	SoupExpectation expectations;
};

/**
 * soup_message_headers_new:
 * @type: the type of headers
 *
 * Creates a #SoupMessageHeaders
 *
 * Return value: a new #SoupMessageHeaders
 **/
SoupMessageHeaders *
soup_message_headers_new (SoupMessageHeadersType type)
{
	SoupMessageHeaders *hdrs;

	hdrs = g_slice_new0 (SoupMessageHeaders);
	/* FIXME: is "5" a good default? */
	hdrs->array = g_array_sized_new (TRUE, FALSE, sizeof (SoupHeader), 5);
	hdrs->type = type;
	hdrs->encoding = -1;

	return hdrs;
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
	g_array_free (hdrs->array, TRUE);
	g_slice_free (SoupMessageHeaders, hdrs);
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
	SoupHeader *hdr_array = (SoupHeader *)hdrs->array->data;
	int i;

	for (i = 0; i < hdrs->array->len; i++)
		g_free (hdr_array[i].value);
	g_array_set_size (hdrs->array, 0);

	hdrs->encoding = -1;
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
	SoupHeader header;
	SoupHeaderSetter setter;

	header.name = intern_header_name (name, &setter);
	header.value = g_strdup (value);
	if (setter)
		setter (hdrs, header.value);
	g_array_append_val (hdrs->array, header);
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

static int
find_header (SoupHeader *hdr_array, const char *interned_name, int nth)
{
	int i;

	for (i = 0; hdr_array[i].name; i++) {
		if (hdr_array[i].name == interned_name) {
			if (nth-- == 0)
				return i;
		}
	}
	return -1;
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
	SoupHeader *hdr_array = (SoupHeader *)(hdrs->array->data);
	SoupHeaderSetter setter;
	int index;

	name = intern_header_name (name, &setter);
	while ((index = find_header (hdr_array, name, 0)) != -1) {
		if (setter)
			setter (hdrs, NULL);
		g_free (hdr_array[index].value);
		g_array_remove_index (hdrs->array, index);
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
const char *
soup_message_headers_find (SoupMessageHeaders *hdrs, const char *name)
{
	return soup_message_headers_find_nth (hdrs, name, 0);
}

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
	SoupHeader *hdr_array = (SoupHeader *)(hdrs->array->data);
	int index = find_header (hdr_array, intern_header_name (name, NULL), nth);
	return index == -1 ? NULL : hdr_array[index].value;
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
	SoupHeader *hdr_array = (SoupHeader *)hdrs->array->data;
	int i;

	for (i = 0; i < hdrs->array->len; i++)
		func (hdr_array[i].name, hdr_array[i].value, user_data);
}


static GStaticMutex header_pool_mutex = G_STATIC_MUTEX_INIT;
static GHashTable *header_pool, *header_setters;

static void transfer_encoding_setter (SoupMessageHeaders *, const char *);
static void content_length_setter (SoupMessageHeaders *, const char *);
static void expectation_setter (SoupMessageHeaders *, const char *);

static char *
intern_header_locked (const char *name)
{
	char *interned;

	interned = g_hash_table_lookup (header_pool, name);
	if (!interned) {
		char *dup = g_strdup (name);
		g_hash_table_insert (header_pool, dup, dup);
		interned = dup;
	}
	return interned;
}

static const char *
intern_header_name (const char *name, SoupHeaderSetter *setter)
{
	const char *interned;

	g_static_mutex_lock (&header_pool_mutex);

	if (!header_pool) {
		header_pool = g_hash_table_new (soup_str_case_hash, soup_str_case_equal);
		header_setters = g_hash_table_new (NULL, NULL);
		g_hash_table_insert (header_setters,
				     intern_header_locked ("Transfer-Encoding"),
				     transfer_encoding_setter);
		g_hash_table_insert (header_setters,
				     intern_header_locked ("Content-Length"),
				     content_length_setter);
		g_hash_table_insert (header_setters,
				     intern_header_locked ("Expect"),
				     expectation_setter);
	}

	interned = intern_header_locked (name);
	if (setter)
		*setter = g_hash_table_lookup (header_setters, interned);

	g_static_mutex_unlock (&header_pool_mutex);
	return interned;
}


/* Specific headers */

static void
transfer_encoding_setter (SoupMessageHeaders *hdrs, const char *value)
{
	if (value) {
		if (g_ascii_strcasecmp (value, "chunked") == 0)
			hdrs->encoding = SOUP_ENCODING_CHUNKED;
		else
			hdrs->encoding = SOUP_ENCODING_UNRECOGNIZED;
	} else
		hdrs->encoding = -1;
}

static void
content_length_setter (SoupMessageHeaders *hdrs, const char *value)
{
	/* Transfer-Encoding trumps Content-Length */
	if (hdrs->encoding == SOUP_ENCODING_CHUNKED)
		return;

	if (value) {
		char *end;

		hdrs->content_length = strtoul (value, &end, 10);
		if (*end)
			hdrs->encoding = SOUP_ENCODING_UNRECOGNIZED;
		else
			hdrs->encoding = SOUP_ENCODING_CONTENT_LENGTH;
	} else
		hdrs->encoding = -1;
}

/**
 * soup_message_headers_get_encoding:
 * @hdrs: a #SoupMessageHeaders
 *
 * Gets the message body encoding that @hdrs declare. This may not
 * always correspond to the encoding used on the wire; eg, a HEAD
 * response may declare a Content-Length or Transfer-Encoding, but
 * it will never actually include a body.
 *
 * Return value: the encoding declared by @hdrs.
 **/
SoupEncoding
soup_message_headers_get_encoding (SoupMessageHeaders *hdrs)
{
	const char *header;

	if (hdrs->encoding != -1)
		return hdrs->encoding;

	/* If Transfer-Encoding was set, hdrs->encoding would already
	 * be set. So we don't need to check that possibility.
	 */
	header = soup_message_headers_find (hdrs, "Content-Length");
	if (header) {
		content_length_setter (hdrs, header);
		if (hdrs->encoding != -1)
			return hdrs->encoding;
	}

	hdrs->encoding = (hdrs->type == SOUP_MESSAGE_HEADERS_REQUEST) ?
		SOUP_ENCODING_NONE : SOUP_ENCODING_EOF;
	return hdrs->encoding;
}

/**
 * soup_message_headers_set_encoding:
 * @hdrs: a #SoupMessageHeaders
 * @encoding: a #SoupEncoding
 *
 * Sets the message body encoding that @hdrs will declare. In particular,
 * you should use this if you are going to send a request or response in
 * chunked encoding.
 **/
void
soup_message_headers_set_encoding (SoupMessageHeaders *hdrs,
				   SoupEncoding        encoding)
{
	if (encoding == hdrs->encoding)
		return;

	switch (encoding) {
	case SOUP_ENCODING_NONE:
	case SOUP_ENCODING_EOF:
		soup_message_headers_remove (hdrs, "Transfer-Encoding");
		soup_message_headers_remove (hdrs, "Content-Length");
		break;

	case SOUP_ENCODING_CONTENT_LENGTH:
		soup_message_headers_remove (hdrs, "Transfer-Encoding");
		break;

	case SOUP_ENCODING_CHUNKED:
		soup_message_headers_remove (hdrs, "Content-Length");
		soup_message_headers_replace (hdrs, "Transfer-Encoding", "chunked");
		break;

	default:
		g_return_if_reached ();
	}

	hdrs->encoding = encoding;
}

/**
 * soup_message_headers_get_content_length:
 * @hdrs: a #SoupMessageHeaders
 *
 * Gets the message body length that @hdrs declare. This will only
 * be non-0 if soup_message_headers_get_encoding() returns
 * %SOUP_ENCODING_CONTENT_LENGTH.
 *
 * Return value: the message body length declared by @hdrs.
 **/
gsize
soup_message_headers_get_content_length (SoupMessageHeaders *hdrs)
{
	return (hdrs->encoding == SOUP_ENCODING_CONTENT_LENGTH) ?
		hdrs->content_length : 0;
}

/**
 * soup_message_headers_set_content_length:
 * @hdrs: a #SoupMessageHeaders
 * @content_length: the message body length
 *
 * Sets the message body length that @hdrs will declare, and sets
 * @hdrs's encoding to %SOUP_ENCODING_CONTENT_LENGTH.
 *
 * You do not normally need to call this; if @hdrs is set to use
 * Content-Length encoding, libsoup will automatically set its
 * Content-Length header for you immediately before sending the
 * headers. One situation in which this method is useful is when
 * generating the response to a HEAD request; Calling
 * soup_message_headers_set_content_length() allows you to put the
 * correct content length into the response without needing to waste
 * memory by filling in a response body which won't actually be sent.
 **/
void
soup_message_headers_set_content_length (SoupMessageHeaders *hdrs,
					 gsize               content_length)
{
	char length[128];

	snprintf (length, sizeof (length), "%lu", (unsigned long)content_length);
	soup_message_headers_remove (hdrs, "Transfer-Encoding");
	soup_message_headers_replace (hdrs, "Content-Length", length);
}

static void
expectation_setter (SoupMessageHeaders *hdrs, const char *value)
{
	if (value) {
		if (!g_ascii_strcasecmp (value, "100-continue"))
			hdrs->expectations = SOUP_EXPECTATION_CONTINUE;
		else
			hdrs->expectations = SOUP_EXPECTATION_UNRECOGNIZED;
	} else
		hdrs->expectations = 0;
}

/**
 * soup_message_headers_get_expectations:
 * @hdrs: a #SoupMessageHeaders
 *
 * Gets the expectations declared by @hdrs's "Expect" header.
 * Currently this will either be %SOUP_EXPECTATION_CONTINUE or
 * %SOUP_EXPECTATION_UNRECOGNIZED.
 *
 * Return value: the contents of @hdrs's "Expect" header
 **/
SoupExpectation
soup_message_headers_get_expectations (SoupMessageHeaders *hdrs)
{
	return hdrs->expectations;
}

/**
 * soup_message_headers_set_expectations:
 * @hdrs: a #SoupMessageHeaders
 * @expectations: the expectations to set
 *
 * Sets @hdrs's "Expect" header according to @expectations. Currently
 * %SOUP_EXPECTATION_CONTINUE is the only known expectation value.
 **/
void
soup_message_headers_set_expectations (SoupMessageHeaders *hdrs,
				       SoupExpectation     expectations)
{
	g_return_if_fail ((expectations & ~SOUP_EXPECTATION_CONTINUE) == 0);

	if (expectations & SOUP_EXPECTATION_CONTINUE)
		soup_message_headers_replace (hdrs, "Expect", "100-continue");
	else
		soup_message_headers_remove (hdrs, "Expect");
}
