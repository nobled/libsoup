/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_SERVER_MESSAGE_H
#define SOUP_SERVER_MESSAGE_H 1

#include <libsoup/soup-message.h>
#include <libsoup/soup-server.h>

typedef enum {
	SOUP_SERVER_MESSAGE_HTTP,
	SOUP_SERVER_MESSAGE_CGI
} SoupServerMessageType;

typedef struct {
	SoupMessage msg;

	SoupAddress *client;
	SoupServerMessageType type;

	GSList *chunks;           /* CONTAINS: SoupDataBuffer* */
	gboolean started;
	gboolean finished;
} SoupServerMessage;

SoupMessage *soup_server_message_new      (SoupAddress           *client,
					   SoupServerMessageType  type);

void         soup_server_message_free     (SoupMessage           *msg);


void         soup_server_message_start    (SoupMessage           *msg);

void         soup_server_message_add_data (SoupMessage           *msg,
					   SoupOwnership          owner,
					   char                  *body,
					   gulong                 length);

void         soup_server_message_finish   (SoupMessage           *msg);


void         soup_server_message_respond  (SoupMessage           *msg,
					   GIOChannel            *chan,
					   SoupCallbackFn         callback,
					   gpointer               user_data);


#endif /* SOUP_SERVER_MESSAGE_H */
