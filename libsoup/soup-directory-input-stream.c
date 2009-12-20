/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-cookie-stream.c
 *
 * Copyright (C) 2008 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-directory-input-stream.h"

#include <stdio.h>
#include <string.h>

#include "soup-message-body.h"
#include "soup-uri.h"


#define INIT_STRING "<html><head><title>OMG!</title></head><body><table>"
#define EXIT_STRING "</table></html>"

G_DEFINE_TYPE (SoupDirectoryInputStream, soup_directory_input_stream, G_TYPE_INPUT_STREAM)

static SoupBuffer *
soup_directory_input_stream_parse_info (SoupDirectoryInputStream *stream,
                                        GFileInfo                *info)
{
        SoupBuffer *buffer;
        GString *string;
        const char *s;
        char *escaped, *path, *xml_string;

        if (!g_file_info_get_name (info))
                return NULL;

        s = g_file_info_get_display_name (info);
        if (!s) {
                s = g_file_info_get_name (info);
                /* FIXME: convert somehow? */
                if (!g_utf8_validate (s, -1, NULL))
                        return NULL;
        }
        string = g_string_new ("<tr>");

        xml_string = g_markup_escape_text (s, -1);
        escaped = g_uri_escape_string (g_file_info_get_name (info), NULL, FALSE);
        path = g_strconcat (stream->uri, "/", escaped, NULL);
        g_free (escaped);
        g_string_append_printf (string, "<td><a href=\"%s\">%s</a></td>", path, xml_string);
        g_free (path);
        g_free (xml_string);
        g_string_append (string, "</tr>");

        buffer = soup_buffer_new (SOUP_MEMORY_TAKE, string->str, string->len);
        g_string_free (string, FALSE);

        return buffer;
}

static SoupBuffer *
soup_directory_input_stream_read_next_file (SoupDirectoryInputStream  *stream,
                                            GCancellable              *cancellable,
                                            GError                   **error)
{
        GFileInfo *info;
        SoupBuffer *buffer;
        GError *err = NULL;

        do {
                info = g_file_enumerator_next_file (stream->enumerator, cancellable, &err);
                if (info == NULL) {
                      if (err) {
                              g_propagate_error (error, err);
                              return NULL;
                      } else if (!stream->done) {
                              stream->done = TRUE;
                              return soup_buffer_new (SOUP_MEMORY_STATIC,
                                                      EXIT_STRING,
                                                      sizeof (EXIT_STRING));
                      } else {
                              return NULL;
                      }
                }

                buffer = soup_directory_input_stream_parse_info (stream, info);
        } while (buffer == NULL);

        return buffer;
}

static gssize
soup_directory_input_stream_read (GInputStream  *input,
                                  void          *buffer,
                                  gsize          count,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
	SoupDirectoryInputStream *stream = SOUP_DIRECTORY_INPUT_STREAM (input);
        gssize total, size;
        
        for (total = 0; total < count; total += size) {
                if (stream->buffer == NULL) {
                        stream->buffer = soup_directory_input_stream_read_next_file (stream, cancellable, error);
                        if (stream->buffer == NULL) {
                                /* FIXME: Is this correct or should we forward the error? */
                                if (total)
                                        g_clear_error (error);
                                return total;
                        }
                }

                size = MIN (stream->buffer->length, count - total);
                memcpy ((char *) buffer + total, stream->buffer->data, size);
                if (size == stream->buffer->length) {
                        soup_buffer_free (stream->buffer);
                        stream->buffer = NULL;
                } else {
                        SoupBuffer *sub = soup_buffer_new_subbuffer (stream->buffer,
                                                                     size,
                                                                     stream->buffer->length - size);
                        soup_buffer_free (stream->buffer);
                        stream->buffer = sub;
                }
        }

        return total;
}

static gboolean
soup_directory_input_stream_close (GInputStream  *input,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
	SoupDirectoryInputStream *stream = SOUP_DIRECTORY_INPUT_STREAM (input);
        gboolean result;
        
        if (stream->buffer) {
                soup_buffer_free (stream->buffer);
                stream->buffer = NULL;
        }

        result = g_file_enumerator_close (stream->enumerator,
                                          cancellable,
                                          error);
        g_object_unref (stream->enumerator);
        stream->enumerator = NULL;
        
        g_free (stream->uri);
        stream->uri = NULL;

        return result;
}

static void
soup_directory_input_stream_class_init (SoupDirectoryInputStreamClass *stream_class)
{
	GInputStreamClass *inputstream_class = G_INPUT_STREAM_CLASS (stream_class);

        inputstream_class->read_fn = soup_directory_input_stream_read;
        inputstream_class->close_fn = soup_directory_input_stream_close;
}

static void
soup_directory_input_stream_init (SoupDirectoryInputStream *stream)
{
        stream->buffer = soup_buffer_new (SOUP_MEMORY_STATIC,
                                          INIT_STRING,
                                          sizeof (INIT_STRING));
}

GInputStream *
soup_directory_input_stream_new (GFileEnumerator *enumerator,
                                 SoupURI         *uri)
{
	GInputStream *stream;
        
        g_return_val_if_fail (G_IS_FILE_ENUMERATOR (enumerator), NULL);
        g_return_val_if_fail (uri != NULL, NULL);

        stream = g_object_new (SOUP_TYPE_DIRECTORY_INPUT_STREAM, NULL);

        SOUP_DIRECTORY_INPUT_STREAM (stream)->enumerator = g_object_ref (enumerator);
        SOUP_DIRECTORY_INPUT_STREAM (stream)->uri = soup_uri_to_string (uri, FALSE);

        return stream;
}

