/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_TYPES_H
#define SOUP_TYPES_H 1

/* Basic client-side structure typedefs. Putting these here gets
 * us out of #include loops.
 */

typedef struct _SoupAddress           SoupAddress;
typedef struct _SoupAuth              SoupAuth;
typedef struct _SoupConnection        SoupConnection;
typedef struct _SoupContext           SoupContext;
typedef struct _SoupMessage           SoupMessage;
typedef struct _SoupSocket            SoupSocket;

typedef         void                 *SoupConnectId;

#endif /* SOUP_TYPES_H */
