/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_SERVER_MESSAGE_H
#define SOUP_SERVER_MESSAGE_H 1

#include <libsoup/soup-message.h>
#include <libsoup/soup-server.h>

#define SOUP_TYPE_SERVER_MESSAGE            (soup_server_message_get_type ())
#define SOUP_SERVER_MESSAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SOUP_TYPE_SERVER_MESSAGE, SoupServerMessage))
#define SOUP_SERVER_MESSAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SOUP_TYPE_SERVER_MESSAGE, SoupServerMessageClass))
#define SOUP_IS_SERVER_MESSAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SOUP_TYPE_SERVER_MESSAGE))
#define SOUP_IS_SERVER_MESSAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), SOUP_TYPE_SERVER_MESSAGE))

typedef enum {
	SOUP_SERVER_MESSAGE_HTTP,
	SOUP_SERVER_MESSAGE_CGI
} SoupServerMessageType;

struct _SoupServerMessage {
	SoupMessage parent;

	SoupServerMessagePrivate *priv;
};

struct _SoupServerMessageClass {
	SoupMessageClass parent_class;

};

GType        soup_server_message_get_type (void);


SoupMessage *soup_server_message_new        (SoupAddress           *client,
					     SoupServerMessageType  type);

SoupAddress *soup_server_message_get_client (SoupMessage           *msg);

void         soup_server_message_start      (SoupMessage           *msg);

void         soup_server_message_add_data   (SoupMessage           *msg,
					     SoupOwnership          owner,
					     char                  *body,
					     gulong                 length);

void         soup_server_message_finish     (SoupMessage           *msg);


void         soup_server_message_respond    (SoupMessage           *msg,
					     GIOChannel            *chan,
					     SoupCallbackFn         callback,
					     gpointer               user_data);


#endif /* SOUP_SERVER_MESSAGE_H */
