/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-context.h: Asyncronous Callback-based HTTP Request Queue.
 *
 * Authors:
 *      Alex Graveley (alex@ximian.com)
 *
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_CONTEXT_H
#define SOUP_CONTEXT_H 1

#include <libsoup/soup-connection.h>

typedef struct _SoupContext SoupContext;

typedef void (*SoupContextConnectFn)       (SoupContext          *ctx, 
					    SoupKnownErrorCode    err,
					    SoupConnection       *conn,
					    gpointer              user_data);

SoupContext   *soup_context_get            (const gchar          *uri);

SoupContext   *soup_context_from_uri       (SoupUri              *suri);

void           soup_context_ref            (SoupContext          *ctx);

void           soup_context_unref          (SoupContext          *ctx);

SoupConnectId  soup_context_get_connection (SoupContext          *ctx,
					    SoupContextConnectFn  cb,
					    gpointer              user_data);

void           soup_context_cancel_connect (SoupConnectId         tag);

const SoupUri *soup_context_get_uri        (SoupContext          *ctx);

#endif /*SOUP_CONTEXT_H*/
