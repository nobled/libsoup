/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-server-message.c: Server-side message
 *
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "soup-server-message.h"
#include "soup-private.h"
#include "soup-transfer.h"

SoupMessage *
soup_server_message_new (SoupAddress *client, SoupServerMessageType type)
{
	SoupServerMessage *smsg;
	SoupMessage *msg;

	smsg = g_new0 (SoupServerMessage, 1);
	msg = (SoupMessage *)smsg;
	soup_message_construct (msg);

	smsg->client = client;
	g_object_ref (client);
	smsg->type = type;

	return msg;
}

static void
free_chunk (gpointer chunk, gpointer notused)
{
	SoupDataBuffer *buf = chunk;

	if (buf->owner == SOUP_BUFFER_SYSTEM_OWNED)
		g_free (buf->body);
	g_free (buf);
}

void
soup_server_message_free (SoupMessage *msg)
{
	SoupServerMessage *smsg = (SoupServerMessage *)msg;

	g_slist_foreach (smsg->chunks, free_chunk, NULL);
	g_slist_free (smsg->chunks);

	g_object_unref (smsg->client);

	g_free ((char *) msg->method);
	soup_message_free (msg);
}


void
soup_server_message_start (SoupMessage *msg)
{
	SoupServerMessage *smsg = (SoupServerMessage *)msg;

	g_return_if_fail (msg != NULL);

	smsg->started = TRUE;

	soup_transfer_write_unpause (msg->priv->write_tag);
}

void
soup_server_message_add_data (SoupMessage   *msg,
			      SoupOwnership  owner,
			      char          *body,
			      gulong         length)
{
	SoupServerMessage *smsg = (SoupServerMessage *)msg;
	SoupDataBuffer *buf;

	g_return_if_fail (msg != NULL);
	g_return_if_fail (body != NULL);
	g_return_if_fail (length != 0);

	buf = g_new0 (SoupDataBuffer, 1);
	buf->length = length;

	if (owner == SOUP_BUFFER_USER_OWNED) {
		buf->body = g_memdup (body, length);
		buf->owner = SOUP_BUFFER_SYSTEM_OWNED;
	} else {
		buf->body = body;
		buf->owner = owner;
	}

	smsg->chunks = g_slist_append (smsg->chunks, buf);

	if (smsg->started)
		soup_transfer_write_unpause (msg->priv->write_tag);
}

void
soup_server_message_finish  (SoupMessage *msg)
{
	SoupServerMessage *smsg = (SoupServerMessage *)msg;

	g_return_if_fail (msg != NULL);

	smsg->started = TRUE;
	smsg->finished = TRUE;

	soup_transfer_write_unpause (msg->priv->write_tag);
}


static void
write_done_cb (gpointer user_data)
{
	SoupMessage *msg = user_data;

	msg->priv->write_tag = 0;

	if (msg->priv->callback)
		(* msg->priv->callback) (msg, msg->priv->user_data);
	soup_server_message_free (msg);
}

static void 
error_cb (gboolean body_started, gpointer msg)
{
	write_done_cb (msg);
}

static void
write_header (gpointer key, gpointer value, gpointer ret)
{
	g_string_sprintfa (ret, "%s: %s\r\n", (char *)key, (char *)value);
}

static GString *
get_response_header (SoupMessage *msg, SoupTransferEncoding encoding)
{
	GString *ret = g_string_new (NULL);

	switch (((SoupServerMessage *)msg)->type) {
	case SOUP_SERVER_MESSAGE_HTTP:
		g_string_sprintfa (ret, "HTTP/1.1 %d %s\r\n", 
				   msg->errorcode, 
				   msg->errorphrase);
		break;
	case SOUP_SERVER_MESSAGE_CGI:
		g_string_sprintfa (ret, "Status: %d %s\r\n", 
				   msg->errorcode, 
				   msg->errorphrase);
		break;
	}

	if (encoding == SOUP_TRANSFER_CONTENT_LENGTH) {
		g_string_sprintfa (ret, "Content-Length: %d\r\n",  
				   msg->response.length);
	} else if (encoding == SOUP_TRANSFER_CHUNKED)
		g_string_append (ret, "Transfer-Encoding: chunked\r\n");

	soup_message_foreach_header (msg->response_headers, write_header, ret);
	g_string_append (ret, "\r\n");

	return ret;
}

static void
get_header_cb (GString  **out_hdr,
	       gpointer   user_data)
{
	SoupMessage *msg = user_data;
	SoupServerMessage *server_msg = user_data;
	SoupTransferEncoding encoding;

	if (server_msg && server_msg->started) {
		if (msg->priv->http_version == SOUP_HTTP_1_0)
			encoding = SOUP_TRANSFER_UNKNOWN;
		else
			encoding = SOUP_TRANSFER_CHUNKED;

		*out_hdr = get_response_header (msg, encoding);
	} else
		soup_transfer_write_pause (msg->priv->write_tag);
}

static SoupTransferDone
get_chunk_cb (SoupDataBuffer *out_next, gpointer user_data)
{
	SoupMessage *msg = user_data;
	SoupServerMessage *server_msg = user_data;

	if (server_msg->chunks) {
		SoupDataBuffer *next = server_msg->chunks->data;

		out_next->owner = next->owner;
		out_next->body = next->body;
		out_next->length = next->length;

		server_msg->chunks = g_slist_remove (server_msg->chunks, next);

		/*
		 * Caller will free the response body, so just free the
		 * SoupDataBuffer struct.
		 */
		g_free (next);

		return SOUP_TRANSFER_CONTINUE;
	} else if (server_msg->finished) {
		return SOUP_TRANSFER_END;
	} else {
		soup_transfer_write_pause (msg->priv->write_tag);
		return SOUP_TRANSFER_CONTINUE;
	}
}

void
soup_server_message_respond (SoupMessage *msg, GIOChannel *chan,
			     SoupCallbackFn callback, gpointer user_data)
{
	SoupServerMessage *smsg = (SoupServerMessage *)msg;

	msg->priv->callback = callback;
	msg->priv->user_data = user_data;

	if (smsg->chunks) {
		SoupTransferEncoding encoding;

		if (msg->priv->http_version == SOUP_HTTP_1_0)
			encoding = SOUP_TRANSFER_UNKNOWN;
		else
			encoding = SOUP_TRANSFER_CHUNKED;

		msg->priv->write_tag = 
			soup_transfer_write (chan,
					     encoding,
					     get_header_cb,
					     get_chunk_cb,
					     write_done_cb,
					     error_cb,
					     msg);

		/*
		 * Pause write until soup_server_message_start()
		 */
		if (!smsg->started)
			soup_transfer_write_pause (msg->priv->write_tag);
	} else {
		GString *header;
		header = get_response_header (msg, SOUP_TRANSFER_CONTENT_LENGTH);
		msg->priv->write_tag = 
			soup_transfer_write_simple (chan,
						    header,
						    &msg->response,
						    write_done_cb,
						    error_cb,
						    msg);
	}
}
