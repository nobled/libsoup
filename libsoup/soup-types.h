/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2000-2002, Ximian, Inc.
 */

#ifndef SOUP_TYPES_H
#define SOUP_TYPES_H 1

#include <sys/types.h>
#include <sys/socket.h>

/* Basic client-side structure typedefs. Putting these here gets
 * us out of #include loops.
 */
typedef struct _SoupAddress                 SoupAddress;
typedef struct _SoupAddressPrivate          SoupAddressPrivate;
typedef struct _SoupAddressClass            SoupAddressClass;
typedef struct _SoupAuth                    SoupAuth;
typedef struct _SoupAuthPrivate             SoupAuthPrivate;
typedef struct _SoupAuthClass               SoupAuthClass;
typedef struct _SoupAuthBasic               SoupAuthBasic;
typedef struct _SoupAuthBasicPrivate        SoupAuthBasicPrivate;
typedef struct _SoupAuthBasicClass          SoupAuthBasicClass;
typedef struct _SoupAuthDigest              SoupAuthDigest;
typedef struct _SoupAuthDigestPrivate       SoupAuthDigestPrivate;
typedef struct _SoupAuthDigestClass         SoupAuthDigestClass;
typedef struct _SoupAuthContext             SoupAuthContext;
typedef struct _SoupAuthContextPrivate      SoupAuthContextPrivate;
typedef struct _SoupAuthContextClass        SoupAuthContextClass;
typedef struct _SoupConnection              SoupConnection;
typedef struct _SoupConnectionPrivate       SoupConnectionPrivate;
typedef struct _SoupConnectionClass         SoupConnectionClass;
typedef struct _SoupContext                 SoupContext;
typedef struct _SoupProxyAuthContext        SoupProxyAuthContext;
typedef struct _SoupProxyAuthContextPrivate SoupProxyAuthContextPrivate;
typedef struct _SoupProxyAuthContextClass   SoupProxyAuthContextClass;
typedef struct _SoupMessage                 SoupMessage;
typedef struct _SoupMessagePrivate          SoupMessagePrivate;
typedef struct _SoupMessageClass            SoupMessageClass;
typedef struct _SoupServer                  SoupServer;
typedef struct _SoupServerPrivate           SoupServerPrivate;
typedef struct _SoupServerClass             SoupServerClass;
typedef struct _SoupServerCGI               SoupServerCGI;
typedef struct _SoupServerCGIPrivate        SoupServerCGIPrivate;
typedef struct _SoupServerCGIClass          SoupServerCGIClass;
typedef struct _SoupServerMessage           SoupServerMessage;
typedef struct _SoupServerMessagePrivate    SoupServerMessagePrivate;
typedef struct _SoupServerMessageClass      SoupServerMessageClass;
typedef struct _SoupServerTCP               SoupServerTCP;
typedef struct _SoupServerTCPPrivate        SoupServerTCPPrivate;
typedef struct _SoupServerTCPClass          SoupServerTCPClass;
typedef struct _SoupSocket                  SoupSocket;
typedef struct _SoupSocketPrivate           SoupSocketPrivate;
typedef struct _SoupSocketClass             SoupSocketClass;
typedef struct _SoupWWWAuthContext          SoupWWWAuthContext;
typedef struct _SoupWWWAuthContextPrivate   SoupWWWAuthContextPrivate;
typedef struct _SoupWWWAuthContextClass     SoupWWWAuthContextClass;

typedef         void                       *SoupAsyncHandle;
typedef         void                       *SoupConnectId;


/* Networking types */
typedef enum {
	SOUP_PROTOCOL_HTTP = 1,
	SOUP_PROTOCOL_HTTPS,
	SOUP_PROTOCOL_SOCKS4,
	SOUP_PROTOCOL_SOCKS5,
	SOUP_PROTOCOL_FTP,
	SOUP_PROTOCOL_FILE,
	SOUP_PROTOCOL_MAILTO
} SoupProtocol;

typedef enum {
	SOUP_ADDRESS_FAMILY_IPV4 = AF_INET,
#ifdef AF_INET6
	SOUP_ADDRESS_FAMILY_IPV6 = AF_INET6
#else
	SOUP_ADDRESS_FAMILY_IPV6 = -1
#endif
} SoupAddressFamily;

#endif /* SOUP_TYPES_H */
