/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-request.c: Protocol-independent streaming request interface
 *
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-request.h"
#include "soup-session.h"
#include "soup-uri.h"

/**
 * SECTION:soup-request
 * @short_description: Protocol-independent streaming request interface
 *
 * FIXME
 **/

/**
 * SoupRequest:
 *
 * FIXME
 *
 * Since: 2.30
 **/

/**
 * SoupRequestInterface:
 * @parent: The parent interface.
 * @send: Synchronously send a request
 * @send_async: Asynchronously begin sending a request
 * @send_finish: Get the result of asynchronously sending a request
 *
 * The interface implemented by #SoupRequest<!-- -->s.
 *
 * Since: 2.30
 **/

static void soup_request_interface_init (SoupRequestInterface *interface);

static void          send_async_default  (SoupRequest          *request,
					  GCancellable         *cancellable,
					  GAsyncReadyCallback   callback,
					  gpointer              user_data);
static GInputStream *send_finish_default (SoupRequest          *request,
					  GAsyncResult         *result,
					  GError              **error);

GType
soup_request_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile))
    {
      GType g_define_type_id =
        g_type_register_static_simple (G_TYPE_INTERFACE,
                                       g_intern_static_string ("SoupRequest"),
                                       sizeof (SoupRequestInterface),
                                       (GClassInitFunc)soup_request_interface_init,
                                       0,
                                       (GInstanceInitFunc)NULL,
                                       (GTypeFlags) 0);
      g_type_interface_add_prerequisite (g_define_type_id, G_TYPE_OBJECT);
      g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
    }
  return g_define_type_id__volatile;
}

static void
soup_request_interface_init (SoupRequestInterface *interface)
{
	interface->send_async = send_async_default;
	interface->send_finish = send_finish_default;

	g_object_interface_install_property (
		interface,
		g_param_spec_boxed (SOUP_REQUEST_URI,
				    "URI",
				    "The request URI",
				    SOUP_TYPE_URI,
				    G_PARAM_READWRITE));
	g_object_interface_install_property (
		interface,
		g_param_spec_object (SOUP_REQUEST_SESSION,
				     "Session",
				     "The request's session",
				     SOUP_TYPE_SESSION,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

/* Default implementation: assume the sync implementation doesn't block */
static void
send_async_default  (SoupRequest          *request,
		     GCancellable         *cancellable,
		     GAsyncReadyCallback   callback,
		     gpointer              user_data)
{
	GSimpleAsyncResult *simple;

	simple = g_simple_async_result_new (G_OBJECT (request),
					    callback, user_data,
					    send_async_default);
	g_simple_async_result_complete_in_idle (simple);
	g_object_unref (simple);
}

static GInputStream *
send_finish_default (SoupRequest          *request,
		     GAsyncResult         *result,
		     GError              **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (request), send_async_default), NULL);

	return soup_request_send (request, NULL, error);	
}

GInputStream *
soup_request_send (SoupRequest          *request,
		   GCancellable         *cancellable,
		   GError              **error)
{
	return SOUP_REQUEST_GET_CLASS (request)->
		send (request, cancellable, error);
}

void
soup_request_send_async (SoupRequest          *request,
			 GCancellable         *cancellable,
			 GAsyncReadyCallback   callback,
			 gpointer              user_data)
{
	SOUP_REQUEST_GET_CLASS (request)->
		send_async (request, cancellable, callback, user_data);
}

GInputStream *
soup_request_send_finish (SoupRequest          *request,
			  GAsyncResult         *result,
			  GError              **error)
{
	return SOUP_REQUEST_GET_CLASS (request)->
		send_finish (request, result, error);
}
