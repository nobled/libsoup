/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2003, Ximian, Inc.
 */

#ifndef SOUP_SESSION_PRIVATE_H
#define SOUP_SESSION_PRIVATE_H 1

#include "soup-cache.h"
#include "soup-session.h"
#include "soup-connection.h"
#include "soup-message-queue.h"
#include "soup-proxy-uri-resolver.h"

G_BEGIN_DECLS

/* "protected" methods for subclasses */
SoupMessageQueue     *soup_session_get_queue            (SoupSession          *session);

SoupMessageQueueItem *soup_session_make_connect_message (SoupSession          *session,
							 SoupAddress          *server_addr);
SoupConnection       *soup_session_get_connection       (SoupSession          *session,
							 SoupMessageQueueItem *item,
							 gboolean             *try_pruning);
gboolean              soup_session_try_prune_connection (SoupSession          *session);
void                  soup_session_connection_failed    (SoupSession          *session,
							 SoupConnection       *conn,
							 guint                 status);

SoupProxyURIResolver *soup_session_get_proxy_resolver   (SoupSession          *session);

void                  soup_session_send_queue_item      (SoupSession          *session,
							 SoupMessageQueueItem *item,
							 SoupConnection       *conn);
SoupCache             *soup_session_get_cache           (SoupSession *session);

G_END_DECLS

#endif /* SOUP_SESSION_PRIVATE_H */
