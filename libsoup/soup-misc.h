/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-misc.h: Miscellaneous settings and configuration file handling.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_MISC_H
#define SOUP_MISC_H 1

#include <glib.h>
#include <libsoup/soup-types.h>
#include <libsoup/soup-uri.h>

/* Configuration routines */

void               soup_load_config          (gchar       *config_file);

void               soup_shutdown             (void);

/* SSL setup routines */

void               soup_set_ssl_ca_file      (const gchar *ca_file);

void               soup_set_ssl_ca_dir       (const gchar *ca_dir);

void               soup_set_ssl_cert_files   (const gchar *cert_file, 
					      const gchar *key_file);

const char        *soup_get_ssl_ca_file      (void);
const char        *soup_get_ssl_ca_dir       (void);
void               soup_get_ssl_cert_files   (const gchar **cert_file,
					      const gchar **key_file);

/* Base64 encoding/decoding */

gchar             *soup_base64_encode          (const gchar    *text,
						gint            len);

int                soup_base64_encode_close    (const guchar   *in, 
						int             inlen, 
						gboolean        break_lines, 
						guchar         *out, 
						int            *state, 
						int            *save);

int                soup_base64_encode_step     (const guchar   *in, 
						int             len, 
						gboolean        break_lines, 
						guchar         *out, 
						int            *state, 
						int            *save);

gchar             *soup_base64_decode          (const gchar    *text,
						gint           *out_len);

int                soup_base64_decode_step     (const guchar   *in, 
						int             len, 
						guchar         *out, 
						int            *state, 
						guint          *save);

#endif /* SOUP_MISC_H */
