/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-ftp-connection.c: ftp connection
 *
 * Copyright (C) 2009 FIXME
 * Copyright (C) 2009 Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "soup-ftp-connection.h"
#include "soup-ftp-input-stream.h"
#include "soup-misc.h"
#include "soup-uri.h"
#include "ParseFTPList.h"

/* TODO:
 * Remove GCancellable parameter from internal methods and use
 * context->cancellable
 */

struct _SoupFTPConnectionPrivate
{
	SoupURI           *uri;

	guint16            features;

        /* not needed but, if unref, it close control_input & control_output streams */
	GSocketConnection *control;
	GDataInputStream  *control_input;
	GOutputStream     *control_output;

	/* not needed but, if unref, it close input returned to caller */
	GSocketConnection *data;

	char              *working_directory;

	GCancellable      *cancellable;
};

typedef enum {
	SOUP_FTP_CONNECTION_FEATURE_MDTM = 1 << 0,
	SOUP_FTP_CONNECTION_FEATURE_SIZE = 1 << 1,
	SOUP_FTP_CONNECTION_FEATURE_REST = 1 << 2,
	SOUP_FTP_CONNECTION_FEATURE_TVFS = 1 << 3,
	SOUP_FTP_CONNECTION_FEATURE_MLST = 1 << 4,
	SOUP_FTP_CONNECTION_FEATURE_MLSD = 1 << 5,
	SOUP_FTP_CONNECTION_FEATURE_EPRT = 1 << 6,
	SOUP_FTP_CONNECTION_FEATURE_EPSV = 1 << 7,
	SOUP_FTP_CONNECTION_FEATURE_UTF8 = 1 << 8
} SoupFTPConnectionFeature;

typedef struct {
	guint16                code;
	char                 *message;
} SoupFTPConnectionReply;

typedef enum {
	SOUP_FTP_NONE,
	SOUP_FTP_BAD_ANSWER,
	SOUP_FTP_INVALID_PATH,
	SOUP_FTP_ACTIVE_NOT_IMPLEMENTED,
	SOUP_FTP_LOGIN_ERROR,
	SOUP_FTP_SERVICE_UNAVAILABLE
} SoupFTPConnectionError;

#define SOUP_PARSE_FTP_STATUS(buffer)         (g_ascii_digit_value (buffer[0]) * 100 \
					       + g_ascii_digit_value (buffer[1]) * 10 \
					       + g_ascii_digit_value (buffer[2]))
#define REPLY_IS_POSITIVE_PRELIMINARY(reply)  (reply->code / 100 == 1 ? TRUE : FALSE)
#define REPLY_IS_POSITIVE_COMPLETION(reply)   (reply->code / 100 == 2 ? TRUE : FALSE)
#define REPLY_IS_POSITIVE_INTERMEDIATE(reply) (reply->code / 100 == 3 ? TRUE : FALSE)
#define REPLY_IS_NEGATIVE_TRANSIENT(reply)    (reply->code / 100 == 4 ? TRUE : FALSE)
#define REPLY_IS_NEGATIVE_PERMANENT(reply)    (reply->code / 100 == 5 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_SYNTAX(reply)          ((reply->code % 100) / 10 == 0 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_INFORMATION(reply)     ((reply->code % 100) / 10 == 1 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_CONNECTION(reply)      ((reply->code % 100) / 10 == 2 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_AUTHENTICATION(reply)  ((reply->code % 100) / 10 == 3 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_UNSPECIFIED(reply)     ((reply->code % 100) / 10 == 4 ? TRUE : FALSE)
#define REPLY_IS_ABOUT_FILE_SYSTEM(reply)     ((reply->code % 100) / 10 == 5 ? TRUE : FALSE)

G_DEFINE_TYPE (SoupFTPConnection, soup_ftp_connection, G_TYPE_OBJECT);

/* async callbacks */
void ftp_callback_pass (GObject *source_object,
    		        GAsyncResult *res,
    		        gpointer user_data);
void ftp_callback_feat (GObject *source_object,
    		        GAsyncResult *res,
    		        gpointer user_data);
void ftp_callback_pasv (GObject *source_object,
    		        GAsyncResult *res,
    		        gpointer user_data);
void ftp_callback_data (GObject *source_object,
    		        GAsyncResult *res,
    		        gpointer user_data);
void ftp_callback_retr (GObject *source_object,
    		        GAsyncResult *res,
    		        gpointer user_data);

static void
soup_ftp_connection_finalize (GObject *object)
{
	SoupFTPConnection *ftp = SOUP_FTP_CONNECTION (object);

	// TODO : close correctly the connection (QUIT)

	if (ftp->priv->uri)
		soup_uri_free (ftp->priv->uri);
	if (ftp->priv->control)
		g_object_unref (ftp->priv->control);
	if (ftp->priv->data)
		g_object_unref (ftp->priv->data);

	G_OBJECT_CLASS (soup_ftp_connection_parent_class)->finalize (object);
}

static void
soup_ftp_connection_class_init (SoupFTPConnectionClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (SoupFTPConnectionPrivate));

	gobject_class->finalize = soup_ftp_connection_finalize;
}

static void
soup_ftp_connection_init (SoupFTPConnection *ftp)
{

	ftp->priv = G_TYPE_INSTANCE_GET_PRIVATE (ftp, SOUP_TYPE_FTP_CONNECTION, SoupFTPConnectionPrivate);
}

SoupFTPConnection *
soup_ftp_connection_new (void)
{
	return g_object_new (SOUP_TYPE_FTP_CONNECTION, NULL);
}

static void
ftp_connection_reply_free (SoupFTPConnectionReply *reply)
{
	g_free (reply->message);
	g_free (reply);
}

static SoupFTPConnectionReply *
ftp_connection_reply_copy (SoupFTPConnectionReply *reply)
{
	SoupFTPConnectionReply *dup;

	dup = g_malloc0 (sizeof (SoupFTPConnectionReply));
	dup->message = g_strdup (reply->message);
	dup->code = reply->code;

	return dup;
}

static gboolean
ftp_connection_check_reply (SoupFTPConnection       *ftp,
			    SoupFTPConnectionReply  *reply,
			    GError                 **error)
{
	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), FALSE);
	g_return_val_if_fail (reply != NULL, FALSE);

	if (REPLY_IS_POSITIVE_PRELIMINARY (reply) ||
	    REPLY_IS_POSITIVE_COMPLETION (reply) ||
	    REPLY_IS_POSITIVE_INTERMEDIATE (reply))
		return TRUE;
	else if (REPLY_IS_NEGATIVE_TRANSIENT (reply)) {
		if (REPLY_IS_ABOUT_CONNECTION (reply)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "FTP : Try again later (connection)");
		} else if (REPLY_IS_ABOUT_FILE_SYSTEM (reply)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "FTP : Try again later (file system)");
		} else {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "FTP : Try again later (%u - %s)",
				     reply->code,
				     reply->message);
		}
		return FALSE;
	} else if (REPLY_IS_NEGATIVE_PERMANENT (reply)) {
		if (REPLY_IS_ABOUT_SYNTAX (reply)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "FTP : Command failed (syntax)");
		}
		if (REPLY_IS_ABOUT_AUTHENTICATION (reply)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "FTP : Authentication failed");
		}
		if (REPLY_IS_ABOUT_FILE_SYSTEM (reply)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "FTP : File action failed (invalid path or no access allowed)");
		} else {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "FTP : Fatal error (%u - %s)",
				     reply->code,
				     reply->message);
		}
		return FALSE;
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Fatal error (%u - %s)",
			     reply->code,
			     reply->message);
		return FALSE;
	}
}

static gboolean
ftp_parse_feat_reply (SoupFTPConnection      *ftp,
		      SoupFTPConnectionReply *reply)
{
	char **split, *feature;
	int i, j;
	const struct {
		const char                 *name;
		SoupFTPConnectionFeature         enable;
	} features [] = {
		{ "MDTM", SOUP_FTP_CONNECTION_FEATURE_MDTM },
		{ "SIZE", SOUP_FTP_CONNECTION_FEATURE_SIZE },
		{ "REST", SOUP_FTP_CONNECTION_FEATURE_REST },
		{ "TVFS", SOUP_FTP_CONNECTION_FEATURE_TVFS },
		{ "MLST", SOUP_FTP_CONNECTION_FEATURE_MLST },
		{ "MLSD", SOUP_FTP_CONNECTION_FEATURE_MLSD },
		{ "EPRT", SOUP_FTP_CONNECTION_FEATURE_EPRT },
		{ "EPSV", SOUP_FTP_CONNECTION_FEATURE_EPSV },
		{ "UTF8", SOUP_FTP_CONNECTION_FEATURE_UTF8 },
	};

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), FALSE);
	g_return_val_if_fail (reply != NULL, FALSE);

	if (reply->code != 211)
		return FALSE;
	split = g_strsplit (reply->message, "\n", 0);
	for (i = 1; split[i + 1]; ++i) {
		if (!g_ascii_isspace (split[i][0])) {
			ftp->priv->features = 0;
			g_strfreev (split);
			return FALSE;
		}
		feature = g_strstrip (split[i]);
		for (j = 0; j < G_N_ELEMENTS (features); ++j) {
			if (g_ascii_strncasecmp (feature, features[j].name, 4) == 0)
				ftp->priv->features |= features[j].enable;
		}
	}
	g_strfreev (split);

	return TRUE;
}

static GSocketConnectable *
ftp_connection_parse_pasv_reply (SoupFTPConnection       *ftp,
				 SoupFTPConnectionReply  *reply,
				 GError                 **error)
{
	GSocketConnectable *conn;
	char **split;
	char *hostname;
	guint16 port;

	if (!ftp_connection_check_reply (ftp, reply, error))
		return NULL;

	if (reply->code != 227) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Unexpected reply (%u - %s)",
			     reply->code,
			     reply->message);
		return NULL;
	} else {
		// TODO : how to check if the split is fine
		split = g_regex_split_simple ("([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*),([0-9]*)",
					      reply->message,
					      G_REGEX_CASELESS,
					      G_REGEX_MATCH_NOTEMPTY);
		hostname = g_strdup_printf ("%s.%s.%s.%s", split[1], split[2], split[3], split[4]);
		port = 256 * atoi (split[5]) + atoi (split[6]);
		conn = g_network_address_new (hostname, port);
		g_strfreev (split);
		g_free (hostname);

		return conn;
	}
}

static char *
ftp_connection_parse_pwd_reply (SoupFTPConnection       *ftp,
				SoupFTPConnectionReply  *reply,
				GError                 **error)
{
	char *current_path;

	if (!ftp_connection_check_reply (ftp, reply, error))
		return NULL;

	if (reply->code != 257) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Unexpected reply (%u - %s)",
			     reply->code,
			     reply->message);
		return NULL;
	}
	current_path = g_strndup (reply->message + 1,
				  g_strrstr (reply->message, "\"") - reply->message - 1);

	return current_path;
}

static gboolean
ftp_connection_parse_welcome_reply (SoupFTPConnection       *ftp,
				    SoupFTPConnectionReply  *reply,
				    GError                 **error)
{
	if (!ftp_connection_check_reply (ftp, reply, error))
		return FALSE;

	if (reply->code == 120) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "FTP : Try again later (connection)");
		return FALSE;
	} else if (reply->code != 220) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Unexpected reply (%u - %s)",
			     reply->code,
			     reply->message);
		return FALSE;
	} else
		return TRUE;
}

static gboolean
ftp_connection_parse_user_reply (SoupFTPConnection       *ftp,
				 SoupFTPConnectionReply  *reply,
				 GError                 **error)
{
	if (!ftp_connection_check_reply (ftp, reply, error))
		return FALSE;

	if (reply->code == 332) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "FTP : Account not implemented");
		return FALSE;
	} else if (reply->code != 230 && reply->code != 331) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Unexpected reply (%u - %s)",
			     reply->code,
			     reply->message);
		return FALSE;
	} else
		return TRUE;
}

static gboolean
ftp_connection_parse_pass_reply (SoupFTPConnection       *ftp,
				 SoupFTPConnectionReply  *reply,
				 GError                 **error)
{
	if (!ftp_connection_check_reply (ftp, reply, error))
		return FALSE;

	if (reply->code == 332) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "FTP : Account not implemented");
		return FALSE;
	} else if (reply->code != 202 && reply->code != 230) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Unexpected reply (%u - %s)",
			     reply->code,
			     reply->message);
		return FALSE;
	} else
		return TRUE;
}

static gboolean
ftp_connection_parse_cwd_reply (SoupFTPConnection       *ftp,
				SoupFTPConnectionReply  *reply,
				GError                 **error)
{
	if (!ftp_connection_check_reply (ftp, reply, error))
		return FALSE;

	if (reply->code != 250) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Unexpected reply (%u - %s)",
			     reply->code,
			     reply->message);
		return FALSE;
	} else
		return TRUE;
}

static gboolean
ftp_connection_parse_retr_reply (SoupFTPConnection       *ftp,
				 SoupFTPConnectionReply  *reply,
				 GError                 **error)
{
	if (!ftp_connection_check_reply (ftp, reply, error))
		return FALSE;

	if (reply->code != 125 && reply->code != 150) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Unexpected reply (%u - %s)",
			     reply->code,
			     reply->message);
		return FALSE;
	} else
		return TRUE;
}

static gboolean
ftp_connection_parse_quit_reply (SoupFTPConnection       *ftp,
				 SoupFTPConnectionReply  *reply,
				 GError                 **error)
{
	if (!ftp_connection_check_reply (ftp, reply, error))
		return FALSE;

        if (reply->code != 221) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "FTP : Unexpected reply (%u - %s)",
			     reply->code,
			     reply->message);
		return FALSE;
	} else
		return TRUE;
}

static SoupFTPConnectionReply *
ftp_connection_receive_reply (SoupFTPConnection  *ftp,
			      GError            **error)
{
	SoupFTPConnectionReply *reply = g_malloc0 (sizeof (SoupFTPConnectionReply));
	char *buffer, *tmp;
	gsize len;
	gboolean multi = FALSE;

	buffer = g_data_input_stream_read_line (ftp->priv->control_input, &len, ftp->priv->cancellable, error);
	if (buffer == NULL)
		return NULL;
	if (len < 4) {
		g_set_error_literal (error,
				     SOUP_FTP_CONNECTION_ERROR,
				     SOUP_FTP_BAD_ANSWER,
				     "Bad FTP answer (less than 4 character)");
		return NULL;
	}
	reply->code = SOUP_PARSE_FTP_STATUS (buffer);
	if (buffer[3] == '-')
		multi = TRUE;
	reply->message = g_strdup (buffer + 4);
	g_free (buffer);
	while (multi) {
		buffer = g_data_input_stream_read_line (ftp->priv->control_input, &len, ftp->priv->cancellable, error);
		tmp = reply->message;
		if (SOUP_PARSE_FTP_STATUS (buffer) == reply->code) {
			if (g_ascii_isspace (buffer[3])) {
				multi = FALSE;
				reply->message = g_strjoin ("\n", tmp, buffer + 4, NULL);
			} else if (buffer[3] == '-')
				reply->message = g_strjoin ("\n", tmp, buffer + 4, NULL);
			else
				reply->message = g_strjoin ("\n", tmp, buffer, NULL);
		} else
			reply->message = g_strjoin ("\n", tmp, buffer, NULL);
		g_free (tmp);
		g_free (buffer);
	}

	return reply;
}

static void
ftp_connection_receive_reply_async_cb (GObject      *source_object,
				     GAsyncResult *read_res,
				     gpointer      user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;
	GSimpleAsyncResult *simple;
	GError *error = NULL;
	gsize len;
	char *buffer, *tmp;
	gboolean multi = FALSE;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	ftp = SOUP_FTP_CONNECTION (g_async_result_get_source_object (G_ASYNC_RESULT (simple)));
	g_object_unref (ftp);
	buffer = g_data_input_stream_read_line_finish (ftp->priv->control_input,
						       read_res,
						       &len,
						       &error);
	if (buffer) {
		reply = g_simple_async_result_get_op_res_gpointer (simple);
		if (reply->message == NULL) {
			if (len < 4) {
				g_simple_async_result_set_error (simple,
								 SOUP_FTP_CONNECTION_ERROR,
								 SOUP_FTP_BAD_ANSWER,
								 "Server answer too short");
				g_simple_async_result_complete (simple);
				g_object_unref (simple);
				g_free (buffer);
				return;
			} else if (!g_ascii_isdigit (buffer[0])         ||
				   g_ascii_digit_value (buffer[0]) > 5  ||
				   g_ascii_digit_value (buffer[0]) == 0 ||
				   !g_ascii_isdigit (buffer[1])         ||
				   g_ascii_digit_value (buffer[1]) > 5  ||
				   !g_ascii_isdigit (buffer[2])) {
				g_simple_async_result_set_error (simple,
								 SOUP_FTP_CONNECTION_ERROR,
								 SOUP_FTP_BAD_ANSWER,
								 "Server answer code not recognized");
				g_simple_async_result_complete (simple);
				g_object_unref (simple);
				g_free (buffer);
				return;
			} else {
				reply->code = SOUP_PARSE_FTP_STATUS (buffer);
				reply->message = g_strdup (buffer + 4);
				if (buffer[3] == '-')
					multi = TRUE;
				g_free (buffer);
			}
		} else {
			multi = TRUE;
			if (SOUP_PARSE_FTP_STATUS (buffer) == reply->code &&
			    g_ascii_isspace (buffer[3]))
				multi = FALSE;
			tmp = reply->message;
			reply->message = g_strjoin ("\n", tmp, buffer, NULL);
			g_free (tmp);
			g_free (buffer);
		}
		if (multi) {
			g_data_input_stream_read_line_async (ftp->priv->control_input,
							     G_PRIORITY_DEFAULT,
							     ftp->priv->cancellable,
							     ftp_connection_receive_reply_async_cb,
							     simple);
			return;
		} else {
			g_simple_async_result_complete (simple);
			g_object_unref (simple);
			return;
		}
	} else {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		g_error_free (error);
		return;
	}
}

static void
ftp_connection_receive_reply_async (SoupFTPConnection   *ftp,
				    GAsyncReadyCallback  callback,
				    gpointer             user_data)
{
	SoupFTPConnectionReply *reply;
	GSimpleAsyncResult *simple;

	reply = g_malloc0 (sizeof (SoupFTPConnectionReply));
	simple = g_simple_async_result_new (G_OBJECT (ftp),
					    callback,
					    user_data,
					    ftp_connection_receive_reply_async);
	g_simple_async_result_set_op_res_gpointer (simple,
						   reply,
						   (GDestroyNotify) ftp_connection_reply_free);
	g_data_input_stream_read_line_async (ftp->priv->control_input,
					     G_PRIORITY_DEFAULT,
					     ftp->priv->cancellable,
					     ftp_connection_receive_reply_async_cb,
					     simple);
}

static SoupFTPConnectionReply *
ftp_connection_receive_reply_finish (SoupFTPConnection  *ftp,
				     GAsyncResult       *result,
				     GError            **error)
{
	SoupFTPConnectionReply *reply;

	reply = ftp_connection_reply_copy (g_simple_async_result_get_op_res_gpointer ((GSimpleAsyncResult *) result));

	return reply;
}

static gboolean
ftp_connection_send_command (SoupFTPConnection  *ftp,
			     const char         *str,
			     GError            **error)
{
	char *request;
	gssize bytes_written;
	gboolean success;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), FALSE);
	g_return_val_if_fail (str != NULL, FALSE);

	request = g_strconcat (str, "\r\n", NULL);
	bytes_written = g_output_stream_write (ftp->priv->control_output,
					       request,
					       strlen (request),
					       ftp->priv->cancellable,
					       error);
	success = bytes_written == strlen (request);
	g_free (request);

	return success;
}

static void
ftp_connection_send_command_cb (GObject      *source_object,
				GAsyncResult *result,
				gpointer      user_data)
{
	SoupFTPConnection *ftp;
	GSimpleAsyncResult *simple;
	GError *error = NULL;
	gboolean success;
	gssize bytes_to_write, bytes_written;

	g_return_if_fail (G_IS_OUTPUT_STREAM (source_object));
	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result));
	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (user_data));

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	ftp = SOUP_FTP_CONNECTION (g_async_result_get_source_object (G_ASYNC_RESULT (simple)));
	g_object_unref (ftp);
	bytes_to_write = g_simple_async_result_get_op_res_gssize (simple);
	bytes_written = g_output_stream_write_finish (G_OUTPUT_STREAM (source_object),
						      result,
						      &error);
	success = (bytes_to_write == bytes_written);
	if (bytes_written == -1) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
	} else {
		g_simple_async_result_set_op_res_gboolean (simple, success);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
	}
}

static void
ftp_connection_send_command_async (SoupFTPConnection   *ftp,
				   const char          *str,
				   GAsyncReadyCallback  callback,
				   gpointer             user_data)
{
	GSimpleAsyncResult *simple;
	char *request;

	g_return_if_fail (SOUP_IS_FTP_CONNECTION (ftp));
	g_return_if_fail (str != NULL);

	simple = g_simple_async_result_new (G_OBJECT (ftp),
					    callback,
					    user_data,
					    ftp_connection_send_command_async);
	request = g_strconcat (str, "\r\n", NULL);
	g_simple_async_result_set_op_res_gssize (simple, strlen (request));
	g_output_stream_write_async (G_OUTPUT_STREAM (ftp->priv->control_output),
				     request,
				     strlen (request),
				     G_PRIORITY_DEFAULT,
				     ftp->priv->cancellable,
				     ftp_connection_send_command_cb,
				     simple);
	g_free (request);
}

static gboolean
ftp_connection_send_command_finish (SoupFTPConnection  *ftp,
				    GAsyncResult       *result,
				    GError            **error)
{
	GSimpleAsyncResult *simple;
	gboolean success;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (!g_simple_async_result_is_valid (result,
					     G_OBJECT (ftp),
					     ftp_connection_send_command_async))
		g_critical ("ftp_send_command_finish FAILED");
	success = g_simple_async_result_get_op_res_gboolean (simple);

	return success;
}

static SoupFTPConnectionReply *
ftp_connection_send_and_recv (SoupFTPConnection  *ftp,
			      const char         *str,
			      GError            **error)
{
	gboolean success;
	SoupFTPConnectionReply *reply;

	success = ftp_connection_send_command (ftp, str, error);
	if (success) {
		reply = ftp_connection_receive_reply (ftp, error);
		if (reply)
			return reply;
		else
			return NULL;
	} else
		return NULL;
}

static void
ftp_connection_send_and_recv_async_cb_b (GObject      *source_object,
					 GAsyncResult *result,
					 gpointer      user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;
	GError *error = NULL;
	GSimpleAsyncResult *simple;

	ftp = SOUP_FTP_CONNECTION (source_object);
	reply = ftp_connection_receive_reply_finish (ftp, result, &error);
	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	if (reply) {
		g_simple_async_result_set_op_res_gpointer (simple, reply, (GDestroyNotify) ftp_connection_reply_free);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
	} else {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		g_error_free (error);
	}
}

static void
ftp_connection_send_and_recv_async_cb_a (GObject      *source_object,
					 GAsyncResult *result,
					 gpointer      user_data)
{
	SoupFTPConnection *ftp;
	GSimpleAsyncResult *simple;
	GError *error = NULL;
	gboolean success;

	g_warn_if_fail (SOUP_IS_FTP_CONNECTION (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (result));

	ftp = SOUP_FTP_CONNECTION (source_object);
	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	success = ftp_connection_send_command_finish (ftp, result, &error);
	if (success) {
		ftp_connection_receive_reply_async (ftp,
						    ftp_connection_send_and_recv_async_cb_b,
						    simple);
	} else {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		g_error_free (error);
	}
}

static void
ftp_connection_send_and_recv_async (SoupFTPConnection   *ftp,
				    const char          *str,
				    GAsyncReadyCallback  callback,
				    gpointer             user_data)
{
	GSimpleAsyncResult *simple;

	g_return_if_fail (SOUP_IS_FTP_CONNECTION (ftp));

	simple = g_simple_async_result_new (G_OBJECT (ftp),
					    callback,
					    user_data,
					    ftp_connection_send_and_recv_async);
	ftp_connection_send_command_async (ftp, str, ftp_connection_send_and_recv_async_cb_a, simple);
}

static SoupFTPConnectionReply *
ftp_connection_send_and_recv_finish (SoupFTPConnection  *ftp,
				     GAsyncResult       *result,
				     GError            **error)
{
	SoupFTPConnectionReply *reply;
	GSimpleAsyncResult *simple;

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;
	reply = ftp_connection_reply_copy (g_simple_async_result_get_op_res_gpointer (simple));
	return reply;
}

/**
 * gboolean ftp_connection_auth (SoupFTPConnection *ftp, GError **error)
 * void ftp_connection_auth_async (SoupFTPConnection *ftp, GCancellable *cancellable, GAsyncReadyCallback callback)
 * gboolean ftp_connection_auth_finish (SoupFTPConnection *ftp, GAsyncResult *result, GError **error)
 *
 * GInputStream * ftp_connection_list (SoupFTPConnection *ftp, char *path, GError **error)
 * void ftp_connection_list_async (SoupFTPConnection *ftp, char *path, GCancellable *cancellable, GAsyncReadyCallback callback)
 * GInputStream * ftp_connection_list_finish (SoupFTPConnection *ftp, GAsyncResult *result, GError **error)
 *
 * GInputStream * ftp_connection_retr (SoupFTPConnection *ftp, char *path, GError **error)
 * void ftp_connection_retr_async (SoupFTPConnection *ftp, char *path, GCancellable *cancellable, GAsyncReadyCallback callback)
 * GInputStream * ftp_connection_retr_finish (SoupFTPConnection *ftp, GAsyncResult *result, GError **error)
 *
 * ftp_connection_cd (SoupFTPConnection *ftp, char *path)
 * char * ftp_connection_cwd (SoupFTPConnection *ftp)
 **/

static gboolean
ftp_connection_auth (SoupFTPConnection  *ftp,
		     GError            **error)
{
	SoupFTPConnectionReply *reply;
	char *msg;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), FALSE);

	if (ftp->priv->uri->user == NULL)
		msg = "USER anonymous";
	else
		msg = g_strdup_printf ("USER %s", ftp->priv->uri->user);
	reply = ftp_connection_send_and_recv (ftp, msg, error);
	if (ftp->priv->uri->user != NULL)
		g_free (msg);
	if (reply == NULL)
		return FALSE;
	else if (!ftp_connection_parse_user_reply (ftp, reply, error)) {
		ftp_connection_reply_free (reply);
		return FALSE;
	} else if (reply->code == 230) {
		ftp_connection_reply_free (reply);
		return TRUE;
	}
	ftp_connection_reply_free (reply);
	if (ftp->priv->uri->user == NULL)
		msg = "PASS libsoup@example.com";
	else
		msg = g_strdup_printf ("PASS %s", ftp->priv->uri->password);
	reply = ftp_connection_send_and_recv (ftp, msg, error);
	if (ftp->priv->uri->user != NULL)
		g_free (msg);
	if (!reply)
		return FALSE;
	if (!ftp_connection_parse_pass_reply (ftp, reply, error)) {
		ftp_connection_reply_free (reply);
		return FALSE;
	} else
		return TRUE;
}

static void
ftp_connection_auth_pass_cb (GObject      *source,
			     GAsyncResult *res,
			     gpointer      user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	g_return_if_fail (SOUP_IS_FTP_CONNECTION (source));
	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res));

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	ftp = SOUP_FTP_CONNECTION (source);
	reply = ftp_connection_send_and_recv_finish (ftp, res, &error);

	if (!reply) {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_set_op_res_gboolean (simple, FALSE);
		g_simple_async_result_complete (simple);
		return;
	}
	if (!ftp_connection_check_reply (ftp, reply, &error)) {
		ftp_connection_reply_free (reply);
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_set_op_res_gboolean (simple, FALSE);
		g_simple_async_result_complete (simple);
		return;
	} else if (reply->code == 230) {
		ftp_connection_reply_free (reply);
		g_simple_async_result_set_op_res_gboolean (simple, TRUE);
		g_simple_async_result_complete (simple);
		return;
	} else if (reply->code == 332) {
		ftp_connection_reply_free (reply);
		g_simple_async_result_set_error (simple,
						 SOUP_FTP_CONNECTION_ERROR,
						 0,
						 "Authentication : ACCT not implemented");
		g_simple_async_result_set_op_res_gboolean (simple, FALSE);
		g_simple_async_result_complete (simple);
		return;
	} else {
		ftp_connection_reply_free (reply);
		g_simple_async_result_set_error (simple,
						 SOUP_FTP_CONNECTION_ERROR,
						 0,
						 "Authentication : Unexpected reply received");
		g_simple_async_result_set_op_res_gboolean (simple, FALSE);
		g_simple_async_result_complete (simple);
		return;
	}
}

static void
ftp_connection_auth_user_cb (GObject      *source,
			     GAsyncResult *result,
			     gpointer      user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;
	GSimpleAsyncResult *simple;
	GError *error = NULL;
	char *msg;

	ftp = SOUP_FTP_CONNECTION (source);
	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	reply = ftp_connection_send_and_recv_finish (ftp, result, &error);
	if (reply == NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		g_error_free (error);
	}
	if (!ftp_connection_parse_user_reply (ftp, reply, &error)) {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		ftp_connection_reply_free (reply);
	}
	if (reply->code == 230) {
		ftp_connection_reply_free (reply);
		g_simple_async_result_set_op_res_gboolean (simple, TRUE);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
	} else if (reply->code == 331) {
		ftp_connection_reply_free (reply);
		if (ftp->priv->uri->user == NULL)
			msg = "PASS libsoup@example.com";
		else
			msg = g_strdup_printf ("PASS %s", ftp->priv->uri->password);
		ftp_connection_send_and_recv_async (ftp, msg, ftp_connection_auth_pass_cb, simple);
		if (ftp->priv->uri->user != NULL)
			g_free (msg);
	}
}

static void
ftp_connection_auth_async (SoupFTPConnection   *ftp,
			   GCancellable        *cancellable,
			   GAsyncReadyCallback  callback,
			   gpointer             user_data)
{
	GSimpleAsyncResult *simple;
	char *msg;

	g_return_if_fail (SOUP_IS_FTP_CONNECTION (ftp));

	simple = g_simple_async_result_new (G_OBJECT (ftp),
					    callback,
					    user_data,
					    ftp_connection_auth_async);
	if (ftp->priv->uri->user == NULL)
		msg = "USER anonymous";
	else
		msg = g_strdup_printf ("USER %s", ftp->priv->uri->user);
	ftp_connection_send_and_recv_async (ftp, msg, ftp_connection_auth_user_cb, simple);
	if (ftp->priv->uri->user != NULL)
		g_free (msg);
}

static gboolean
ftp_connection_auth_finish (SoupFTPConnection  *ftp,
			    GAsyncResult       *result,
			    GError            **error)
{
	GSimpleAsyncResult *simple;
	gboolean res;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (simple, error))
		res = FALSE;
	else
		res = g_simple_async_result_get_op_res_gboolean (simple);

	return res;
}

/**
 * GInputStream * ftp_connection_list (SoupFTPConnection *ftp, char *path, GError **error)
 * void ftp_connection_list_async (SoupFTPConnection *ftp, char *path, GCancellable *cancellable, GAsyncReadyCallback callback)
 * GInputStream * ftp_connection_list_finish (SoupFTPConnection *ftp, GAsyncResult *result, GError **error)
 **/

static void
ftp_connection_list_complete (SoupFTPInputStream *sfstream,
			      gpointer            user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;

	/* FIXME : need to clean ftp->priv->data */

	g_return_if_fail (SOUP_IS_FTP_INPUT_STREAM (sfstream));
	g_return_if_fail (SOUP_IS_FTP_CONNECTION (user_data));

	ftp = SOUP_FTP_CONNECTION (user_data);
	g_signal_handlers_disconnect_by_func (sfstream,
					      ftp_connection_list_complete,
					      ftp);

	reply = ftp_connection_receive_reply (ftp, NULL);
	if (reply)
		ftp_connection_reply_free (reply);
}

static int
ftp_connection_file_info_list_compare (gconstpointer data,
				     gconstpointer user_data)
{
	GFileInfo *info;
	char *name;

	g_return_val_if_fail (G_IS_FILE_INFO (data), -1);
	g_return_val_if_fail (user_data != NULL,-1);

	info = G_FILE_INFO (data);
	name = (char *) user_data;

	return g_strcmp0 (g_file_info_get_name (info), name);
}

static int
ftp_connection_info_list_sort (gconstpointer        data1,
			     gconstpointer        data2)
{
	/* FIXME : This code is duplicated (see protocol_file) */
	GFileInfo *info1, *info2;

	g_return_val_if_fail (G_IS_FILE_INFO (data1), -1);
	g_return_val_if_fail (G_IS_FILE_INFO (data2), -1);

	info1 = G_FILE_INFO (data1);
	info2 = G_FILE_INFO (data2);

	if (g_file_info_get_file_type (info1) == G_FILE_TYPE_DIRECTORY &&
	    g_file_info_get_file_type (info2) != G_FILE_TYPE_DIRECTORY)
		return -1;
	else if (g_file_info_get_file_type (info1) != G_FILE_TYPE_DIRECTORY &&
		 g_file_info_get_file_type (info2) == G_FILE_TYPE_DIRECTORY)
		return 1;
	else
		return g_ascii_strcasecmp (g_file_info_get_name (info1),
					     g_file_info_get_name (info2));
}

static GList *
ftp_connection_list_parse (SoupFTPConnection *ftp,
			   GInputStream      *stream)
{
	GDataInputStream *dstream;
	struct list_state state = { 0, };
	GList *file_list = NULL;
	GFileInfo *file_info;
	char *buffer;
	GError *error = NULL;
	gsize len = 0;
	int type;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), NULL);
	g_return_val_if_fail (G_IS_INPUT_STREAM (stream), NULL);

	dstream = g_data_input_stream_new (stream);
	g_data_input_stream_set_newline_type (dstream, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

	while ((buffer = g_data_input_stream_read_line (dstream, &len, NULL, &error))) {
		struct list_result result = { 0, };
		GTimeVal tv = { 0, 0 };
		type = ParseFTPList (buffer, &state, &result);
		file_info = g_file_info_new();
		if (result.fe_type == 'f') {
			g_file_info_set_file_type (file_info, G_FILE_TYPE_REGULAR);
			g_file_info_set_name (file_info, g_strdup (result.fe_fname));
		} else if (result.fe_type == 'd') {
			g_file_info_set_file_type (file_info, G_FILE_TYPE_DIRECTORY);
			g_file_info_set_name (file_info, g_strdup (result.fe_fname));
		} else if (result.fe_type == 'l' && result.fe_lname) {
			g_file_info_set_file_type (file_info, G_FILE_TYPE_SYMBOLIC_LINK);
			g_file_info_set_name (file_info, g_strndup (result.fe_fname,
								    result.fe_lname - result.fe_fname - 4));
			g_file_info_set_symlink_target (file_info, g_strdup (result.fe_lname));
		} else {
			g_object_unref (file_info);
			continue;
		}
		g_file_info_set_size (file_info, atoi (result.fe_size));
		if (result.fe_time.tm_year >= 1900)
			result.fe_time.tm_year -= 1900;
		tv.tv_sec = mktime (&result.fe_time);
		if (tv.tv_sec != -1)
			g_file_info_set_modification_time (file_info, &tv);
		file_list = g_list_prepend (file_list, file_info);
	}

	g_object_unref (dstream);
	file_list = g_list_sort (file_list, ftp_connection_info_list_sort);

	return file_list;
}

static GInputStream *
ftp_connection_list (SoupFTPConnection  *ftp,
		     char               *path,
		     GError            **error)
{
	SoupFTPConnectionReply *reply;
	GInputStream *istream, *sfstream;
	GSocketConnectable *conn;
	GSocketClient *client;
	GList *file_list = NULL;
	GFileInfo *dir_info;
	char *msg;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	reply = ftp_connection_send_and_recv (ftp, "PASV", error);
	if (!reply)
		return NULL;
	if (!ftp_connection_check_reply (ftp, reply, error)) {
		ftp_connection_reply_free (reply);
		return NULL;
	}
	if (reply->code != 227) {
		ftp_connection_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_FTP_CONNECTION_ERROR,
				     0,
				     "Directory listing : Unexpected reply received");
		return NULL;
	}
	conn = ftp_connection_parse_pasv_reply (ftp, reply, error);
	ftp_connection_reply_free (reply);
	client = g_socket_client_new ();
	ftp->priv->data = g_socket_client_connect (client,
						   conn,
						   ftp->priv->cancellable,
						   error);
	g_object_unref (client);
	g_object_unref (conn);
	if (!ftp->priv->data)
		return NULL;
	msg = g_strdup_printf ("LIST -a %s", path);
	reply = ftp_connection_send_and_recv (ftp, msg, error);
	g_free (msg);
	if (!reply)
		return NULL;
	if (!ftp_connection_check_reply (ftp, reply, error)) {
		ftp_connection_reply_free (reply);
		return NULL;
	}
	if (reply->code != 125 && reply->code != 150) {
		ftp_connection_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_FTP_CONNECTION_ERROR,
				     0,
				     "Directory listing : Unexpected reply received");
		return NULL;
	}
	ftp_connection_reply_free (reply);

	istream = g_io_stream_get_input_stream (G_IO_STREAM (ftp->priv->data));

	dir_info = g_file_info_new ();
	g_file_info_set_name (dir_info, path);
	g_file_info_set_file_type (dir_info, G_FILE_TYPE_DIRECTORY);
	sfstream = soup_ftp_input_stream_new (istream, dir_info, NULL);
	g_object_unref (dir_info);
	g_signal_connect (sfstream, "eof",
			  G_CALLBACK (ftp_connection_list_complete), ftp);

	file_list = ftp_connection_list_parse (ftp, sfstream);
	g_object_set (sfstream, "children", file_list, NULL);

	return sfstream;
}

static void
ftp_connection_retr_complete (SoupFTPInputStream *sfstream,
			      gpointer            user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;

	g_return_if_fail (SOUP_IS_FTP_INPUT_STREAM (sfstream));
	g_return_if_fail (SOUP_IS_FTP_CONNECTION (user_data));

	/* FIXME, load_uri_complete, clean up ftp_connection */

	ftp = user_data;
	g_signal_handlers_disconnect_by_func (sfstream, ftp_connection_retr_complete, ftp);

	g_object_unref (ftp->priv->data);
	ftp->priv->data = NULL;
	reply = ftp_connection_receive_reply (ftp, NULL);
	if (reply)
		ftp_connection_reply_free (reply);
}

static GInputStream *
ftp_connection_retr (SoupFTPConnection  *ftp,
		     char               *path,
		     GFileInfo          *info,
		     GError            **error)
{
	SoupFTPConnectionReply *reply;
	GInputStream *istream, *sfstream;
	GSocketConnectable *conn;
	GSocketClient *client;
	char *msg;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	reply = ftp_connection_send_and_recv (ftp, "PASV", error);
	if (!reply)
		return NULL;
	if (!ftp_connection_check_reply (ftp, reply, error)) {
		ftp_connection_reply_free (reply);
		return NULL;
	}
	if (reply->code != 227) {
		ftp_connection_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_FTP_CONNECTION_ERROR,
				     0,
				     "Directory listing : Unexpected reply received");
		return NULL;
	}
	conn = ftp_connection_parse_pasv_reply (ftp, reply, error);
	ftp_connection_reply_free (reply);
	client = g_socket_client_new ();
	ftp->priv->data = g_socket_client_connect (client,
						   conn,
						   ftp->priv->cancellable,
						   error);
	g_object_unref (client);
	g_object_unref (conn);
	if (!ftp->priv->data)
		return NULL;
	msg = g_strdup_printf ("RETR %s", path);
	reply = ftp_connection_send_and_recv (ftp, msg, error);
	g_free (msg);
	if (!reply)
		return NULL;
	if (!ftp_connection_check_reply (ftp, reply, error)) {
		ftp_connection_reply_free (reply);
		return NULL;
	}
	if (reply->code != 125 && reply->code != 150) {
		ftp_connection_reply_free (reply);
		g_set_error_literal (error,
				     SOUP_FTP_CONNECTION_ERROR,
				     0,
				     "Directory listing : Unexpected reply received");
		return NULL;
	}
	ftp_connection_reply_free (reply);
	istream = g_io_stream_get_input_stream (G_IO_STREAM (ftp->priv->data));
	sfstream = soup_ftp_input_stream_new (istream, info, NULL);
	g_signal_connect (sfstream, "eof",
			  G_CALLBACK (ftp_connection_retr_complete), ftp);

	return sfstream;
}

GInputStream *
soup_ftp_connection_load_uri (SoupFTPConnection  *ftp,
			      SoupURI            *uri,
			      GCancellable       *cancellable,
			      GError            **error)
{
	SoupFTPConnectionReply *reply;
	GInputStream *istream;
	GList *listing = NULL;
	GFileInfo *info;
	char *needed_directory, *msg;
	GSocketClient *client;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	ftp->priv->uri = soup_uri_copy (uri);
	ftp->priv->cancellable = cancellable;
	if (ftp->priv->control == NULL) {
		client = g_socket_client_new ();
		ftp->priv->control = g_socket_client_connect_to_host (client,
								      uri->host,
								      uri->port,
								      ftp->priv->cancellable,
								      error);
		g_object_unref (client);
		if (ftp->priv->control == NULL)
			return NULL;
		ftp->priv->control_input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (ftp->priv->control)));
		g_data_input_stream_set_newline_type (ftp->priv->control_input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
		ftp->priv->control_output = g_io_stream_get_output_stream (G_IO_STREAM (ftp->priv->control));
		reply = ftp_connection_receive_reply (ftp, error);
		if (reply == NULL)
			return NULL;
		else if (!ftp_connection_parse_welcome_reply (ftp, reply, error)) {
			ftp_connection_reply_free (reply);
			return NULL;
		}
		ftp_connection_reply_free (reply);
		if (!ftp_connection_auth (ftp, error))
			return NULL;
	}
	if (ftp->priv->working_directory == NULL) {
		reply = ftp_connection_send_and_recv (ftp, "PWD", error);
		if (reply == NULL)
			return NULL;
		ftp->priv->working_directory = ftp_connection_parse_pwd_reply (ftp, reply, error);
		if (ftp->priv->working_directory == NULL) {
			ftp_connection_reply_free (reply);
			return NULL;
		}
	}
	needed_directory = g_strndup (uri->path, g_strrstr (uri->path, "/") - uri->path + 1);
	if (g_strcmp0 (ftp->priv->working_directory, needed_directory)) {
		msg = g_strdup_printf ("CWD %s", needed_directory);
		reply = ftp_connection_send_and_recv (ftp, msg, error);
		g_free (msg);
		if (reply == NULL)
			return NULL;
		if (!ftp_connection_parse_cwd_reply (ftp, reply, error)) {
			ftp_connection_reply_free (reply);
			return NULL;
		}
		g_free (ftp->priv->working_directory);
		ftp->priv->working_directory = g_strdup (needed_directory);
	}
	g_free (needed_directory);
	istream = ftp_connection_list (ftp, ".", error);
	if (istream == NULL)
		return NULL;
	if (g_str_has_suffix (uri->path, "/"))
		return istream;
	else {
		g_object_get (istream,
			      "file-info", &info,
			      "children", &listing,
			      NULL);
		listing = g_list_find_custom (listing,
					      g_strrstr (uri->path, "/") + 1,
					      ftp_connection_file_info_list_compare);
		if (listing == NULL) {
			g_object_unref (istream);
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "FTP : File or directory not found");
			return NULL;
		}
		if (g_file_info_get_file_type (listing->data) == G_FILE_TYPE_DIRECTORY) {
			msg = g_strconcat ("CWD ", ftp->priv->working_directory, g_file_info_get_name (listing->data), NULL);
			reply = ftp_connection_send_and_recv (ftp, msg, error);
			if (reply == NULL) {
				g_object_unref (istream);
				g_free (msg);
				return NULL;
			}
			g_free (ftp->priv->working_directory);
			ftp->priv->working_directory = msg;
			g_object_unref (istream);
			istream = ftp_connection_list (ftp, ".", error);
			if (istream == NULL)
				return NULL;
			return istream;
		} else if (g_file_info_get_file_type (listing->data) == G_FILE_TYPE_REGULAR) {
			g_object_unref (istream);
			istream = ftp_connection_retr (ftp, uri->path, listing->data, error);
			if (istream == NULL)
				return NULL;
			return istream;
		} else {
			g_object_unref (istream);
			return NULL;
		}
		// FIXME : add the symlink case
	}
}

static void
ftp_connection_welcome_cb (GObject      *source_object,
			   GAsyncResult *res,
			   gpointer      user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	ftp = SOUP_FTP_CONNECTION (source_object);
	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	reply = ftp_connection_receive_reply_finish (ftp,
						     res,
						     &error);
	if (reply == NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		g_error_free (error);
	}
	if (!ftp_connection_parse_welcome_reply (ftp, reply, &error)) {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		ftp_connection_reply_free (reply);
	}
	ftp_connection_auth_async (ftp, ftp->priv->cancellable, ftp_callback_pass, simple);
}

static void
ftp_connection_connection_cb (GObject      *source_object,
			      GAsyncResult *result,
			      gpointer      user_data)
{
	SoupFTPConnection *ftp;
	GSocketClient *client;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	g_return_if_fail (G_IS_SOCKET_CLIENT (source_object));
	g_return_if_fail (G_IS_ASYNC_RESULT (result));
	g_return_if_fail (G_IS_SIMPLE_ASYNC_RESULT (user_data));

	client = G_SOCKET_CLIENT (source_object);
	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	ftp = SOUP_FTP_CONNECTION (g_async_result_get_source_object (G_ASYNC_RESULT (simple)));
	g_object_unref (ftp);
	ftp->priv->control = g_socket_client_connect_to_host_finish (client,
								result,
								&error);
	if (ftp->priv->control == NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		g_error_free (error);
	}
	ftp->priv->control_input = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (ftp->priv->control)));
	g_data_input_stream_set_newline_type (ftp->priv->control_input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
	ftp->priv->control_output = g_io_stream_get_output_stream (G_IO_STREAM (ftp->priv->control));
	ftp_connection_receive_reply_async (ftp,
					    ftp_connection_welcome_cb,
					    simple);
}

void
soup_ftp_connection_load_uri_async (SoupFTPConnection   *ftp,
				    SoupURI             *uri,
				    GCancellable        *cancellable,
				    GAsyncReadyCallback  callback,
				    gpointer             user_data)
{
	GSimpleAsyncResult *simple;
	GSocketClient *client;

	g_return_if_fail (SOUP_IS_FTP_CONNECTION (ftp));
	g_return_if_fail (uri != NULL);

	ftp->priv->uri = soup_uri_copy (uri);
	ftp->priv->cancellable = cancellable;
	simple = g_simple_async_result_new (G_OBJECT (ftp),
					    callback,
					    user_data,
					    soup_ftp_connection_load_uri_async);
	if (ftp->priv->control == NULL) {
		client = g_socket_client_new ();
		g_socket_client_connect_to_host_async (client,
						       ftp->priv->uri->host,
						       ftp->priv->uri->port,
						       ftp->priv->cancellable,
						       ftp_connection_connection_cb,
						       simple);
	}
	/* needed_directory = g_strndup (uri->path, g_strrstr (uri->path, "/") - uri->path + 1); */
}

GInputStream *
soup_ftp_connection_load_uri_finish (SoupFTPConnection  *ftp,
				     GAsyncResult       *result,
				     GError            **error)
{
	GInputStream *input_stream;
	GSimpleAsyncResult *simple;
	GInputStream *sfstream;

	g_return_val_if_fail (SOUP_IS_FTP_CONNECTION (ftp), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;
	input_stream = g_io_stream_get_input_stream (G_IO_STREAM (ftp->priv->data));
	sfstream = soup_ftp_input_stream_new (input_stream, NULL, NULL);
	g_signal_connect (sfstream, "eof",
			  G_CALLBACK (ftp_connection_retr_complete), ftp);

	return sfstream;
}

GQuark
soup_ftp_connection_error_quark (void)
{
	static GQuark error;
	if (!error)
		error = g_quark_from_static_string ("soup_ftp_connection_error_quark");
	return error;
}

GQuark
soup_ftp_error_quark (void)
{
	static GQuark error;
	if (!error)
		error = g_quark_from_static_string ("soup_ftp_error_quark");
	return error;
}

/**
 * async callbacks
 **/

void
ftp_callback_pass (GObject      *source_object,
		   GAsyncResult *res,
		   gpointer      user_data)
{
	SoupFTPConnection *ftp;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_FTP_CONNECTION (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	ftp = SOUP_FTP_CONNECTION (source_object);
	simple = G_SIMPLE_ASYNC_RESULT (user_data);

	if (ftp_connection_auth_finish (ftp, res, &error))
		ftp_connection_send_and_recv_async (ftp, "FEAT", ftp_callback_feat, simple);
}

void
ftp_callback_feat (GObject      *source_object,
		   GAsyncResult *res,
		   gpointer      user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_FTP_CONNECTION (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	ftp = SOUP_FTP_CONNECTION (source_object);
	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	reply = ftp_connection_receive_reply_finish (ftp, res, &error);
	if (reply) {
		if (ftp_connection_check_reply (ftp, reply, &error)) {
			if (REPLY_IS_POSITIVE_COMPLETION (reply)) {
				ftp_parse_feat_reply (ftp, reply);
				ftp_connection_send_and_recv_async (ftp,
								  "PASV",
								  ftp_callback_pasv,
								  simple);
			}
		} else {
			g_simple_async_result_set_from_error (simple, error);
			g_simple_async_result_complete (simple);
			g_error_free (error);
		}
		ftp_connection_reply_free (reply);
	} else {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_error_free (error);
	}
}

void
ftp_callback_pasv (GObject      *source_object,
		   GAsyncResult *res,
		   gpointer      user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;
	GSocketConnectable *conn;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_FTP_CONNECTION (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	ftp = SOUP_FTP_CONNECTION (source_object);
	reply = ftp_connection_receive_reply_finish (ftp, res, &error);
	if (reply) {
		if (ftp_connection_check_reply (ftp, reply, &error)) {
			if (REPLY_IS_POSITIVE_COMPLETION (reply)) {
				conn = ftp_connection_parse_pasv_reply (ftp, reply, &error);
				if (conn) {
					g_socket_client_connect_async (g_socket_client_new (),
								       conn,
								       ftp->priv->cancellable,
								       ftp_callback_data,
								       simple);
				} else {
					g_simple_async_result_set_error (simple,
									 SOUP_FTP_CONNECTION_ERROR,
									 0,
									 "Passive failed");
					g_simple_async_result_complete (simple);
				}
			}
		} else {
			g_simple_async_result_set_from_error (simple, error);
			g_simple_async_result_complete (simple);
			g_error_free (error);
		}
		ftp_connection_reply_free (reply);
	} else {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_error_free (error);
	}
}

void
ftp_callback_data (GObject      *source_object,
		   GAsyncResult *res,
		   gpointer      user_data)
{
	GSocketClient *client;
	SoupFTPConnection *ftp;
	GSimpleAsyncResult *simple;
	GError *error = NULL;
	char *uri_decode, *msg;

	g_warn_if_fail (G_IS_SOCKET_CLIENT (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (res));
	g_warn_if_fail (user_data != NULL);

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	client = G_SOCKET_CLIENT (source_object);
	ftp = SOUP_FTP_CONNECTION (g_async_result_get_source_object (G_ASYNC_RESULT (simple)));
	g_object_unref (ftp);
	ftp->priv->data = g_socket_client_connect_finish (client,
							  res,
							  &error);
	if (ftp->priv->data) {
		uri_decode = soup_uri_decode (ftp->priv->uri->path);
		if (uri_decode) {
			msg = g_strdup_printf ("RETR %s", uri_decode);
			ftp_connection_send_and_recv_async (ftp, msg, ftp_callback_retr, simple);
			g_free (uri_decode);
			g_free (msg);
		} else {
			g_simple_async_result_set_error (simple,
							 SOUP_FTP_CONNECTION_ERROR,
							 SOUP_FTP_INVALID_PATH,
							 "Path decode failed");
			g_simple_async_result_complete (simple);
			g_object_unref (simple);
		}
	} else {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		g_error_free (error);
	}
}

void
ftp_callback_retr (GObject      *source_object,
		   GAsyncResult *result,
		   gpointer      user_data)
{
	SoupFTPConnection *ftp;
	SoupFTPConnectionReply *reply;
	GSimpleAsyncResult *simple;
	GError *error = NULL;

	g_warn_if_fail (SOUP_IS_FTP_CONNECTION (source_object));
	g_warn_if_fail (G_IS_ASYNC_RESULT (result));

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	ftp = SOUP_FTP_CONNECTION (source_object);
	reply = ftp_connection_receive_reply_finish (ftp, result, &error);
	if (reply == NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		g_error_free (error);
	} else if (!ftp_connection_parse_retr_reply (ftp, reply, &error)) {
		g_simple_async_result_set_from_error (simple, error);
		g_simple_async_result_complete (simple);
		g_object_unref (simple);
		ftp_connection_reply_free (reply);
	}
	g_simple_async_result_complete (simple);
	g_object_unref (simple);
}
