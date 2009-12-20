/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-request-base.c: base: URI request object
 *
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include "soup-request-base.h"
#include "soup-session-feature.h"
#include "soup-session.h"
#include "soup-uri.h"

static void soup_request_base_initable_interface_init (GInitableIface *initable_interface);

G_DEFINE_TYPE_WITH_CODE (SoupRequestBase, soup_request_base, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_REQUEST, NULL)
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
						soup_request_base_initable_interface_init))

struct _SoupRequestBasePrivate {
	SoupURI *uri;
	SoupSession *session;

};

enum {
	PROP_0,

	PROP_URI,
	PROP_SESSION
};

static void soup_request_base_set_property (GObject *object, guint prop_id,
					    const GValue *value, GParamSpec *pspec);
static void soup_request_base_get_property (GObject *object, guint prop_id,
					    GValue *value, GParamSpec *pspec);
static void soup_request_base_finalize (GObject *object);

static gboolean soup_request_base_initable_init (GInitable     *initable,
						 GCancellable  *cancellable,
						 GError       **error);

static gboolean soup_request_base_validate_uri (SoupRequestBase  *req_base,
						SoupURI          *uri,
						GError          **error);

static void
soup_request_base_class_init (SoupRequestBaseClass *request_base_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (request_base_class);

	g_type_class_add_private (request_base_class, sizeof (SoupRequestBasePrivate));

	request_base_class->validate_uri = soup_request_base_validate_uri;

	object_class->finalize = soup_request_base_finalize;
	object_class->set_property = soup_request_base_set_property;
	object_class->get_property = soup_request_base_get_property;

	g_object_class_override_property (object_class, PROP_URI, "uri");
	g_object_class_override_property (object_class, PROP_SESSION, "session");
}

static void
soup_request_base_initable_interface_init (GInitableIface *initable_interface)
{
	initable_interface->init = soup_request_base_initable_init;
}

static void
soup_request_base_init (SoupRequestBase *req_base)
{
	req_base->priv = G_TYPE_INSTANCE_GET_PRIVATE (req_base, SOUP_TYPE_REQUEST_BASE, SoupRequestBasePrivate);
}

static gboolean
soup_request_base_initable_init (GInitable     *initable,
				 GCancellable  *cancellable,
				 GError       **error)
{
	SoupRequestBase *req_base = SOUP_REQUEST_BASE (initable);
	gboolean ok;

	ok = SOUP_REQUEST_BASE_GET_CLASS (initable)->
		validate_uri (req_base, req_base->priv->uri, error);

	if (!ok && error) {
		char *uri_string = soup_uri_to_string (req_base->priv->uri, FALSE);
		g_set_error (error, SOUP_ERROR, SOUP_ERROR_BAD_URI,
			     _("Invalid '%s' URI: %s"),
			     req_base->priv->uri->scheme,
			     uri_string);
		g_free (uri_string);
	}

	return ok;
}

static gboolean
soup_request_base_validate_uri (SoupRequestBase  *req_base,
				SoupURI          *uri,
				GError          **error)
{
	return TRUE;
}

static void
soup_request_base_finalize (GObject *object)
{
	SoupRequestBase *req_base = SOUP_REQUEST_BASE (object);

	if (req_base->priv->uri)
		soup_uri_free (req_base->priv->uri);

	G_OBJECT_CLASS (soup_request_base_parent_class)->finalize (object);
}

static void
soup_request_base_set_property (GObject *object, guint prop_id,
				const GValue *value, GParamSpec *pspec)
{
	SoupRequestBase *req_base = SOUP_REQUEST_BASE (object);

	switch (prop_id) {
	case PROP_URI:
		if (req_base->priv->uri)
			soup_uri_free (req_base->priv->uri);
		req_base->priv->uri = g_value_dup_boxed (value);
		break;
	case PROP_SESSION:
		if (req_base->priv->session)
			g_object_unref (req_base->priv->session);
		req_base->priv->session = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
soup_request_base_get_property (GObject *object, guint prop_id,
				GValue *value, GParamSpec *pspec)
{
	SoupRequestBase *req_base = SOUP_REQUEST_BASE (object);

	switch (prop_id) {
	case PROP_URI:
		g_value_set_boxed (value, req_base->priv->uri);
		break;
	case PROP_SESSION:
		g_value_set_object (value, req_base->priv->session);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

SoupURI *
soup_request_base_get_uri (SoupRequestBase *req_base)
{
	return req_base->priv->uri;
}

SoupSession *
soup_request_base_get_session (SoupRequestBase *req_base)
{
	return req_base->priv->session;
}

