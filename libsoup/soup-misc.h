/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_MISC_H
#define SOUP_MISC_H 1

#include <glib-object.h>

/* Base64 encoding/decoding */

char              *soup_base64_encode        (const char   *text,
					      int           len);

int                soup_base64_encode_close  (const guchar *in, 
					      int           inlen, 
					      gboolean      break_lines, 
					      guchar       *out, 
					      int          *state, 
					      int          *save);

int                soup_base64_encode_step   (const guchar *in, 
					      int           len, 
					      gboolean      break_lines, 
					      guchar       *out, 
					      int          *state, 
					      int          *save);

char              *soup_base64_decode        (const gchar  *text,
					      int          *out_len);

int                soup_base64_decode_step   (const guchar *in, 
					      int           len, 
					      guchar       *out, 
					      int          *state, 
					      guint        *save);

/* Non-default-GMainContext operations */
GSource           *soup_add_io_watch         (GMainContext *async_context,
					      GIOChannel   *chan,
					      GIOCondition  condition,
					      GIOFunc       function,
					      gpointer      data);
GSource           *soup_add_idle             (GMainContext *async_context,
					      GSourceFunc   function,
					      gpointer      data);
GSource           *soup_add_timeout          (GMainContext *async_context,
					      guint         interval,
					      GSourceFunc   function,
					      gpointer      data);

/* Misc utils */

guint              soup_signal_connect_once  (gpointer      instance,
					      const char   *detailed_signal,
					      GCallback     c_handler,
					      gpointer      data);

guint              soup_str_case_hash        (gconstpointer key);
gboolean           soup_str_case_equal       (gconstpointer v1,
					      gconstpointer v2);

/**
 * soup_ssl_supported:
 *
 * Can be used to test if libsoup was compiled with ssl support.
 **/
extern gboolean soup_ssl_supported;

#endif /* SOUP_MISC_H */
