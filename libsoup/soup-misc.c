/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-misc.c: Miscellaneous settings and configuration file handling.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "soup-misc.h"
#include "soup-private.h"
#include "soup-queue.h"

gboolean soup_initialized = FALSE;

/* This is just here so that stuff that currently calls soup_get_proxy
 * doesn't need to be changed, so that as that code is being moved, I
 * remember where to think about proxy stuff.
 */
SoupContext *
soup_get_proxy (void)
{
	return NULL;
}

guint
soup_str_case_hash (gconstpointer key)
{
	const char *p = key;
	guint h = toupper(*p);

	if (h)
		for (p += 1; *p != '\0'; p++)
			h = (h << 5) - h + toupper(*p);

	return h;
}

gboolean
soup_str_case_equal (gconstpointer v1,
		     gconstpointer v2)
{
	const gchar *string1 = v1;
	const gchar *string2 = v2;

	return g_strcasecmp (string1, string2) == 0;
}

gint
soup_substring_index (gchar *str, gint len, gchar *substr)
{
	int i, sublen = strlen (substr);

	for (i = 0; i <= len - sublen; ++i)
		if (str[i] == substr[0])
			if (memcmp (&str[i], substr, sublen) == 0)
				return i;

	return -1;
}

/* Base64 utils (straight from camel-mime-utils.c) */
#define d(x)

static char *base64_alphabet =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* 
 * call this when finished encoding everything, to
 * flush off the last little bit 
 */
int
soup_base64_encode_close (const guchar  *in, 
			  int            inlen, 
			  gboolean       break_lines, 
			  guchar        *out, 
			  int           *state, 
			  int           *save)
{
	int c1, c2;
	unsigned char *outptr = out;

	if (inlen > 0)
		outptr += soup_base64_encode_step (in, 
						   inlen, 
						   break_lines, 
						   outptr, 
						   state, 
						   save);

	c1 = ((unsigned char *) save) [1];
	c2 = ((unsigned char *) save) [2];
	
	d(printf("mode = %d\nc1 = %c\nc2 = %c\n",
		 (int)((char *) save) [0],
		 (int)((char *) save) [1],
		 (int)((char *) save) [2]));

	switch (((char *) save) [0]) {
	case 2:
		outptr [2] = base64_alphabet[ ( (c2 &0x0f) << 2 ) ];
		g_assert (outptr [2] != 0);
		goto skip;
	case 1:
		outptr[2] = '=';
	skip:
		outptr [0] = base64_alphabet [ c1 >> 2 ];
		outptr [1] = base64_alphabet [ c2 >> 4 | ( (c1&0x3) << 4 )];
		outptr [3] = '=';
		outptr += 4;
		break;
	}
	if (break_lines)
		*outptr++ = '\n';

	*save = 0;
	*state = 0;

	return outptr-out;
}

/*
 * performs an 'encode step', only encodes blocks of 3 characters to the
 * output at a time, saves left-over state in state and save (initialise to
 * 0 on first invocation).
 */
int
soup_base64_encode_step (const guchar  *in, 
			 int            len, 
			 gboolean       break_lines, 
			 guchar        *out, 
			 int           *state, 
			 int           *save)
{
	register guchar *outptr;
	register const guchar *inptr;

	if (len <= 0)
		return 0;

	inptr = in;
	outptr = out;

	d (printf ("we have %d chars, and %d saved chars\n", 
		   len, 
		   ((char *) save) [0]));

	if (len + ((char *) save) [0] > 2) {
		const guchar *inend = in+len-2;
		register int c1, c2, c3;
		register int already;

		already = *state;

		switch (((char *) save) [0]) {
		case 1:	c1 = ((unsigned char *) save) [1]; goto skip1;
		case 2:	c1 = ((unsigned char *) save) [1];
			c2 = ((unsigned char *) save) [2]; goto skip2;
		}
		
		/* 
		 * yes, we jump into the loop, no i'm not going to change it, 
		 * it's beautiful! 
		 */
		while (inptr < inend) {
			c1 = *inptr++;
		skip1:
			c2 = *inptr++;
		skip2:
			c3 = *inptr++;
			*outptr++ = base64_alphabet [ c1 >> 2 ];
			*outptr++ = base64_alphabet [ c2 >> 4 | 
						      ((c1&0x3) << 4) ];
			*outptr++ = base64_alphabet [ ((c2 &0x0f) << 2) | 
						      (c3 >> 6) ];
			*outptr++ = base64_alphabet [ c3 & 0x3f ];
			/* this is a bit ugly ... */
			if (break_lines && (++already)>=19) {
				*outptr++='\n';
				already = 0;
			}
		}

		((char *)save)[0] = 0;
		len = 2-(inptr-inend);
		*state = already;
	}

	d(printf("state = %d, len = %d\n",
		 (int)((char *)save)[0],
		 len));

	if (len>0) {
		register char *saveout;

		/* points to the slot for the next char to save */
		saveout = & (((char *)save)[1]) + ((char *)save)[0];

		/* len can only be 0 1 or 2 */
		switch(len) {
		case 2:	*saveout++ = *inptr++;
		case 1:	*saveout++ = *inptr++;
		}
		((char *)save)[0]+=len;
	}

	d(printf("mode = %d\nc1 = %c\nc2 = %c\n",
		 (int)((char *)save)[0],
		 (int)((char *)save)[1],
		 (int)((char *)save)[2]));

	return outptr-out;
}

/**
 * soup_base64_encode:
 * @text: the binary data to encode.
 * @inlen: the length of @text.
 *
 * Encode a sequence of binary data into it's Base-64 stringified
 * representation.
 *
 * Return value: The Base-64 encoded string representing @text.
 */
gchar *
soup_base64_encode (const gchar *text, gint inlen)
{
        unsigned char *out;
        int state = 0, outlen;
        unsigned int save = 0;
        
        out = g_malloc (inlen * 4 / 3 + 5);
        outlen = soup_base64_encode_close (text, 
					   inlen, 
					   FALSE,
					   out, 
					   &state, 
					   &save);
        out[outlen] = '\0';
        return (char *) out;
}

static unsigned char camel_mime_base64_rank[256] = {
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
	 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
	255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
	255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
	255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
};

/**
 * base64_decode_step: decode a chunk of base64 encoded data
 * @in: input stream
 * @len: max length of data to decode
 * @out: output stream
 * @state: holds the number of bits that are stored in @save
 * @save: leftover bits that have not yet been decoded
 *
 * Decodes a chunk of base64 encoded data
 **/
int
soup_base64_decode_step (const guchar  *in, 
			 int            len, 
			 guchar        *out, 
			 int           *state, 
			 guint         *save)
{
	register const guchar *inptr;
	register guchar *outptr;
	const guchar *inend;
	guchar c;
	register unsigned int v;
	int i;

	inend = in+len;
	outptr = out;

	/* convert 4 base64 bytes to 3 normal bytes */
	v=*save;
	i=*state;
	inptr = in;
	while (inptr < inend) {
		c = camel_mime_base64_rank [*inptr++];
		if (c != 0xff) {
			v = (v<<6) | c;
			i++;
			if (i==4) {
				*outptr++ = v>>16;
				*outptr++ = v>>8;
				*outptr++ = v;
				i=0;
			}
		}
	}

	*save = v;
	*state = i;

	/* quick scan back for '=' on the end somewhere */
	/* fortunately we can drop 1 output char for each trailing = (upto 2) */
	i=2;
	while (inptr > in && i) {
		inptr--;
		if (camel_mime_base64_rank [*inptr] != 0xff) {
			if (*inptr == '=')
				outptr--;
			i--;
		}
	}

	/* if i!= 0 then there is a truncation error! */
	return outptr - out;
}

gchar *
soup_base64_decode (const gchar   *text,
		    gint          *out_len)
{
	gchar *ret;
	gint inlen, state = 0, save = 0;

	inlen = strlen (text);
	ret = g_malloc0 (inlen);

	*out_len = soup_base64_decode_step (text, inlen, ret, &state, &save);

	return ret; 
}

#define ALLOW_UNLESS_DENIED TRUE
#define DENY_UNLESS_ALLOWED FALSE

static gboolean allow_policy = ALLOW_UNLESS_DENIED;
static GSList *allow_tokens = NULL;
static GSList *deny_tokens = NULL;

static void
soup_config_ssl_ca_file (gchar *key, gchar *value)
{
	soup_set_ssl_ca_file (value);
}

static void
soup_config_ssl_ca_directory (gchar *key, gchar *value)
{
	soup_set_ssl_ca_dir (value);
}

static void
soup_config_ssl_certificate (gchar *key, gchar *value)
{
	gint idx;

	idx = strcspn (value, " \t");
	if (!idx) return;
	
	value [idx] = '\0';

	idx += strspn (value + idx + 1, " \t");
	if (!idx) return;

	soup_set_ssl_cert_files (value, value + idx);
}

typedef void (*SoupConfigFunc) (gchar *key, gchar *value);

struct SoupConfigFuncs {
	gchar          *key;
	SoupConfigFunc  func;
} soup_config_funcs [] = {
	{ "ssl-ca-file",      soup_config_ssl_ca_file },
	{ "ssl-ca-directory", soup_config_ssl_ca_directory },
	{ "ssl-certificate",  soup_config_ssl_certificate },
	{ NULL }
};

static void
soup_config_reset_allow_deny (void)
{
	GSList *iter;

	for (iter = allow_tokens; iter; iter = iter->next) g_free (iter->data);
	for (iter = deny_tokens; iter; iter = iter->next) g_free (iter->data);

	g_slist_free (allow_tokens);
	g_slist_free (deny_tokens);

	allow_tokens = deny_tokens = NULL;
}

static gboolean
soup_config_allow_deny (gchar *key)
{
	GSList **list;
	gchar **iter, **split;

	key = g_strchomp (key);

	if (!g_strncasecmp (key, "allow", 5)) list = &allow_tokens;
	else if (!g_strncasecmp (key, "deny", 4)) list = &deny_tokens;
	else return FALSE;

	iter = split = g_strsplit (key, " ", 0);
	if (!split || !split [1]) return TRUE;

	while (*(++iter)) {
		if (!g_strcasecmp (iter [0], "all")) {
			GSList *iter;
			allow_policy = (*list == allow_tokens);
			for (iter = *list; iter; iter = iter->next)
				g_free (iter->data);
			g_slist_free (*list);
			*list = NULL;
			*list = g_slist_prepend (*list, NULL);
			break;
		}

		*list = g_slist_prepend (*list, g_strdup (iter [0]));
	}

	g_strfreev (split);
	return TRUE;
}

static gboolean
soup_config_token_allowed (gchar *key)
{
	gboolean allow;
	GSList *list;

	list = (allow_policy == ALLOW_UNLESS_DENIED) ? deny_tokens:allow_tokens;
	allow = (allow_policy == ALLOW_UNLESS_DENIED) ? TRUE : FALSE;

	if (!list) return allow;

	for (; list; list = list->next)
		if (!list->data ||
		    !g_strncasecmp (key,
				    (gchar *) list->data,
				    strlen ((gchar *) list->data)))
			return !allow;

	return allow;
}

static void
soup_load_config_internal (gchar *config_file, gboolean admin)
{
	struct SoupConfigFuncs *funcs;
	FILE *cfg;
	char buf[128];

	cfg = fopen (config_file, "r");
	if (!cfg) return;

	if (admin) soup_config_reset_allow_deny();

	while (fgets (buf, sizeof (buf), cfg)) {
		char *key, *value, *iter, *iter2, **split;

		iter = g_strstrip (buf);
		if (!*iter || *iter == '#') continue;

		iter2 = strchr (iter, '#');
		if (iter2) *iter2 = '\0';

		if (admin && soup_config_allow_deny (iter)) continue;

		if (!admin && !soup_config_token_allowed (iter)) {
			g_warning ("Configuration item \"%s\" in file \"%s\" "
				   "disallowed by system configuration.\n",
				   iter,
				   config_file);
			continue;
		}

		split = g_strsplit (g_strchomp (iter), "=", 2);

		if (!split) continue;
		if (!split[1] || split[2]) {
			g_strfreev (split);
			continue;
		}

		key = g_strchomp (split[0]);
		value = g_strchug (split[1]);

		for (funcs = soup_config_funcs; funcs && funcs->key; funcs++)
			if (!g_strcasecmp (key, funcs->key)) {
				funcs->func (key, value);
				break;
			}

		g_strfreev (split);
	}
}

/**
 * soup_load_config:
 * @config_file: The file to load configuration from. If NULL, load from .souprc
 * in user's home directory.
 *
 * Load the Soup configuration from file. First attempt to load the system
 * configuration from SYSCONFDIR/souprc, then from either the config file name
 * passed in config_file, or from .souprc in the user's home directory.
 *
 * The first time a message is sent using Soup, the configuration is loaded from
 * the system souprc file, and the user's souprc file.
 *
 * soup_load_config can be called multiple times. Each time settings will be
 * reset and reread from scratch.
 */
void
soup_load_config (gchar *config_file)
{
#ifdef SYSCONFDIR
	/* Load system global config */
	soup_load_config_internal (SYSCONFDIR G_DIR_SEPARATOR_S "souprc",
				   TRUE);
#endif

	/* Load requested file or user local config */
	if (!config_file) {
		gchar *dfile = g_strconcat (g_get_home_dir(),
					    G_DIR_SEPARATOR_S ".souprc",
					    NULL);
		soup_load_config_internal (dfile, FALSE);
		g_free (dfile);
	} else
		soup_load_config_internal (config_file, FALSE);

	soup_initialized = TRUE;
}

/**
 * soup_shutdown:
 *
 * Shut down the Soup engine.
 *
 * The pending message queue is flushed by calling %soup_message_cancel on all
 * active requests.
 */
void
soup_shutdown ()
{
	soup_queue_shutdown ();
}

static char *ssl_ca_file   = NULL;
static char *ssl_ca_dir    = NULL;
static char *ssl_cert_file = NULL;
static char *ssl_key_file  = NULL;

/**
 * soup_set_ca_file:
 * @ca_file: the path to a CA file
 *
 * Specify a file containing CA certificates to be used to verify
 * peers.
 */
void
soup_set_ssl_ca_file (const gchar *ca_file)
{
	g_free (ssl_ca_file);

	ssl_ca_file = g_strdup (ca_file);
}

/**
 * soup_set_ca_dir
 * @ca_dir: the directory containing CA certificate files
 *
 * Specify a directory containing CA certificates to be used to verify
 * peers.
 */
void
soup_set_ssl_ca_dir (const gchar *ca_dir)
{
	g_free (ssl_ca_dir);

	ssl_ca_dir = g_strdup (ca_dir);
}

/**
 * soup_set_ssl_cert_files
 * @cert_file: the file containing the SSL client certificate
 * @key_file: the file containing the SSL private key
 *
 * Specify a SSL client certificate to be used for client
 * authentication with the HTTP server
 */
void
soup_set_ssl_cert_files (const gchar *cert_file, const gchar *key_file)
{
	g_free (ssl_cert_file);
	g_free (ssl_key_file);

	ssl_cert_file = g_strdup (cert_file);
	ssl_key_file  = g_strdup (key_file);
}

/**
 * soup_get_ca_file:
 *
 * Return value: A file containing CA certificates to be used to verify
 * peers.
 */
const char *
soup_get_ssl_ca_file (void)
{
	return ssl_ca_file;
}

/**
 * soup_get_ca_dir
 *
 * Return value: A directory containing CA certificates to be used to verify
 * peers.
 */
const char *
soup_get_ssl_ca_dir (void)
{
	return ssl_ca_dir;
}

/**
 * soup_get_ssl_cert_files
 * @cert_file: the file containing the SSL client certificate
 * @key_file: the file containing the SSL private key
 *
 * Specify a SSL client certificate to be used for client
 * authentication with the HTTP server
 */
void
soup_get_ssl_cert_files (const gchar **cert_file, const gchar **key_file)
{
	if (cert_file)
		*cert_file = ssl_cert_file;

	if (key_file)
		*key_file = ssl_key_file;
}
