/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_CONNECTION_H
#define SOUP_CONNECTION_H 1

#include <libsoup/soup-socket.h>
#include <libsoup/soup-uri.h>

#define SOUP_TYPE_CONNECTION            (soup_connection_get_type ())
#define SOUP_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_CONNECTION, SoupConnection))
#define SOUP_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_CONNECTION, SoupConnectionClass))
#define SOUP_IS_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_CONNECTION))
#define SOUP_IS_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_CONNECTION))
#define SOUP_CONNECTION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SOUP_TYPE_CONNECTION, SoupConnectionClass))


struct _SoupConnection {
	SoupSocket parent;

	SoupConnectionPrivate *priv;
};

struct _SoupConnectionClass {
	SoupSocketClass parent_class;

	SoupAddress *(*get_remote_address) (SoupConnection *);
	void         (*start_request)      (SoupConnection *, SoupMessage *);
};

GType           soup_connection_get_type          (void);


SoupConnection *soup_connection_new                (SoupUri         *uri,
						    SoupAuthContext *ac);

GIOChannel     *soup_connection_get_iochannel      (SoupConnection  *conn);

void            soup_connection_set_in_use         (SoupConnection  *conn, 
						    gboolean         in_use);
gboolean        soup_connection_is_in_use          (SoupConnection  *conn);

SoupAddress    *soup_connection_get_remote_address (SoupConnection *conn);

void            soup_connection_start_request      (SoupConnection  *conn,
						    SoupMessage     *msg);

void            soup_connection_close              (SoupConnection  *conn);

#endif /*SOUP_CONNECTION_H*/
