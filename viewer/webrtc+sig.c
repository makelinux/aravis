/**
 * @file
 * @brief gst webrtc demo. Session only.
 *
 * Doesn't supports rooms.
 *
 * Interfaces: signalling, videotestsrc, audiotestsrc, webrtcbin
 *
 * Demo gstreamer app for negotiating and streaming a sendrecv webrtc stream
 * with a browser JS app.
 *
 * based on https://github.com/centricular/gstwebrtc-demos/blob/master/sendrecv/gst/webrtc-sendrecv.c
 */
#include <gst/gst.h>
#include <gst/sdp/sdp.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

/* For signalling */
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include <string.h>

enum AppState
{
	APP_STATE_UNKNOWN = 0,
	APP_STATE_ERROR = 1,          /* generic error */
	SERVER_CONNECTING = 1000,
	SERVER_CONNECTION_ERROR,
	SERVER_REGISTERING = 2000,
	SERVER_CLOSED,                /* server connection closed by us or the server */
	PEER_CONNECTING = 3000,
	PEER_CONNECTION_ERROR,
	PEER_CALL_NEGOTIATING = 4000,
	PEER_CALL_STARTED,
	PEER_CALL_STOPPING,
	PEER_CALL_STOPPED,
	PEER_CALL_ERROR,
};

static GMainLoop *loop;
static GstElement *pipe1, *webrtc1;
//static GObject *send_channel, *receive_channel;

static SoupWebsocketConnection *sigsrv; /* Signal service */
static enum AppState app_state;
static const gchar *peer_id = "argos";
static const gchar *server_url = "wss://webrtc.nirbheek.in:8443";
static gboolean disable_ssl;
static gboolean remote_is_offerer;

static GOptionEntry entries[] = {
	{"peer-id", 0, 0, G_OPTION_ARG_STRING, &peer_id,
		"String ID of the peer to connect to", "ID"},
	{"server", 0, 0, G_OPTION_ARG_STRING, &server_url,
		"Signalling server to connect to", "URL"},
	{"disable-ssl", 0, 0, G_OPTION_ARG_NONE, &disable_ssl, "Disable ssl", NULL},
	{"remote-offerer", 0, 0, G_OPTION_ARG_NONE, &remote_is_offerer,
		"Request that the peer generate the offer and we'll answer", NULL},
	{NULL},
};

static gboolean
cleanup_and_quit_loop (const gchar * msg, enum AppState state)
{
	if (msg)
		g_printerr ("%s\n", msg);
	if (state > 0)
		app_state = state;

	if (sigsrv) {
		if (soup_websocket_connection_get_state (sigsrv) ==
		    SOUP_WEBSOCKET_STATE_OPEN)
			/* This will call us again */
			soup_websocket_connection_close (sigsrv, 1000, "");
		else
			g_object_unref (sigsrv);
	}

	if (loop) {
		g_main_loop_quit (loop);
		loop = NULL;
	}

	/* To allow usage as a GSourceFunc */
	return G_SOURCE_REMOVE;
}

static gchar *
get_string_from_json_object (JsonObject * object)
{
	JsonNode *root;
	JsonGenerator *generator;
	gchar *text;

	/* Make it the root node */
	root = json_node_init_object (json_node_alloc (), object);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	text = json_generator_to_data (generator, NULL);

	/* Release everything */
	g_object_unref (generator);
	json_node_free (root);
	return text;
}
#if 0
	static void
handle_media_stream (GstPad * pad, GstElement * pipe, const char *convert_name,
		     const char *sink_name)
{
	GstElement *q, *conv, *resample, *sink;
	GstPadLinkReturn ret;

	g_print ("Trying to handle stream with %s ! %s", convert_name, sink_name);

	q = gst_element_factory_make ("queue", NULL);
	g_assert_nonnull (q);
	conv = gst_element_factory_make (convert_name, NULL);
	g_assert_nonnull (conv);
	sink = gst_element_factory_make (sink_name, NULL);
	g_assert_nonnull (sink);

	if (g_strcmp0 (convert_name, "audioconvert") == 0) {
		/* Might also need to resample, so add it just in case.
		 * Will be a no-op if it's not required. */
		resample = gst_element_factory_make ("audioresample", NULL);
		g_assert_nonnull (resample);
		gst_bin_add_many (GST_BIN (pipe), q, conv, resample, sink, NULL);
		gst_element_sync_state_with_parent (q);
		gst_element_sync_state_with_parent (conv);
		gst_element_sync_state_with_parent (resample);
		gst_element_sync_state_with_parent (sink);
		gst_element_link_many (q, conv, resample, sink, NULL);
	} else {
		gst_bin_add_many (GST_BIN (pipe), q, conv, sink, NULL);
		gst_element_sync_state_with_parent (q);
		gst_element_sync_state_with_parent (conv);
		gst_element_sync_state_with_parent (sink);
		gst_element_link_many (q, conv, sink, NULL);
	}

	ret = gst_pad_link (pad, gst_element_get_static_pad (q, "sink"));

	g_assert_cmphex (ret, ==, GST_PAD_LINK_OK);
}

	static void
on_incoming_decodebin_stream (GstElement * decodebin, GstPad * pad,
			      GstElement * pipe)
{
	GstCaps *caps;
	const gchar *name;

	if (!gst_pad_has_current_caps (pad)) {
		g_printerr ("Pad '%s' has no caps, can't do anything, ignoring\n",
			    GST_PAD_NAME (pad));
		return;
	}

	caps = gst_pad_get_current_caps (pad);
	name = gst_structure_get_name (gst_caps_get_structure (caps, 0));

	if (g_str_has_prefix (name, "video")) {
		handle_media_stream (pad, pipe, "videoconvert", "autovideosink");
	} else if (g_str_has_prefix (name, "audio")) {
		handle_media_stream (pad, pipe, "audioconvert", "autoaudiosink");
	} else {
		g_printerr ("Unknown pad %s, ignoring", GST_PAD_NAME (pad));
	}
}

	static void
on_incoming_stream (GstElement * webrtc, GstPad * pad, GstElement * pipe)
{
	if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
		return;

	GstElement *decodebin = gst_element_factory_make ("decodebin", NULL);
	g_signal_connect (decodebin, "pad-added",
			  G_CALLBACK (on_incoming_decodebin_stream), pipe);
	gst_bin_add (GST_BIN (pipe), decodebin);
	gst_element_sync_state_with_parent (decodebin);

	GstPad *sinkpad = gst_element_get_static_pad (decodebin, "sink");
	gst_pad_link (pad, sinkpad);
	gst_object_unref (sinkpad);
}
#endif
	static void
send_ice_candidate_message (GstElement * webrtc G_GNUC_UNUSED, guint mlineindex,
			    gchar * candidate, gpointer user_data G_GNUC_UNUSED)
{
	JsonObject *ice, *msg;

	ice = json_object_new ();
	json_object_set_string_member (ice, "candidate", candidate);
	json_object_set_int_member (ice, "sdpMLineIndex", mlineindex);
	msg = json_object_new ();
	json_object_set_object_member (msg, "ice", ice);
	g_autofree gchar *text = get_string_from_json_object (msg);
	json_object_unref (msg);

	soup_websocket_connection_send_text (sigsrv, text);
}

	static void
send_sdp_to_peer (GstWebRTCSessionDescription * desc)
{
	gchar *text;
	JsonObject *msg, *sdp;

	if (app_state < PEER_CALL_NEGOTIATING) {
		cleanup_and_quit_loop ("Can't send SDP to peer, not in call",
				       APP_STATE_ERROR);
		return;
	}

	text = gst_sdp_message_as_text (desc->sdp);
	sdp = json_object_new ();
	char *type[] = {[GST_WEBRTC_SDP_TYPE_OFFER] = "offer", [GST_WEBRTC_SDP_TYPE_ANSWER] = "answer"};
	g_print ("Sending %s:\n%s\n", type[desc->type], text);
	json_object_set_string_member (sdp, "type", type[desc->type]);
	json_object_set_string_member (sdp, "sdp", text);
	g_free (text);

	msg = json_object_new ();
	json_object_set_object_member (msg, "sdp", sdp);
	text = get_string_from_json_object (msg);
	json_object_unref (msg);

	soup_websocket_connection_send_text (sigsrv, text);
	g_free (text);
}

/* Offer created by our pipeline, to be sent to the peer */
	static void
on_offer_created (GstPromise * promise, gpointer user_data)
{
	GstWebRTCSessionDescription *offer = NULL;

	g_assert_cmphex (app_state, ==, PEER_CALL_NEGOTIATING);

	g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
	gst_structure_get (gst_promise_get_reply (promise), "offer",
			   GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
	gst_promise_unref (promise);

	g_signal_emit_by_name (webrtc1, "set-local-description", offer, NULL);

	send_sdp_to_peer (offer);
	gst_webrtc_session_description_free (offer);
}

	static void
on_negotiation_needed (GstElement * element, gpointer user_data)
{
	app_state = PEER_CALL_NEGOTIATING;

	if (remote_is_offerer)
		soup_websocket_connection_send_text (sigsrv, "OFFER_REQUEST");
	else
		g_signal_emit_by_name (webrtc1, "create-offer", NULL,
				       gst_promise_new_with_change_func (on_offer_created, user_data, NULL));
}


#if 0

	static void
data_channel_on_error (GObject * dc, gpointer user_data)
{
	cleanup_and_quit_loop ("Data channel error", 0);
}

	static void
data_channel_on_open (GObject * dc, gpointer user_data)
{
	GBytes *bytes = g_bytes_new ("data", strlen ("data"));
	g_print ("data channel opened\n");
	g_signal_emit_by_name (dc, "send-string", "Hi! from GStreamer");
	g_signal_emit_by_name (dc, "send-data", bytes);
	g_bytes_unref (bytes);
}

	static void
data_channel_on_close (GObject * dc, gpointer user_data)
{
	cleanup_and_quit_loop ("Data channel closed", 0);
}

	static void
data_channel_on_message_string (GObject * dc, gchar * str, gpointer user_data)
{
	g_print ("Received data channel message: %s\n", str);
}

	static void
connect_data_channel_signals (GObject * data_channel)
{
	return;
	g_signal_connect (data_channel, "on-error",
			  G_CALLBACK (data_channel_on_error), NULL);
	g_signal_connect (data_channel, "on-open", G_CALLBACK (data_channel_on_open),
			  NULL);
	g_signal_connect (data_channel, "on-close",
			  G_CALLBACK (data_channel_on_close), NULL);
	g_signal_connect (data_channel, "on-message-string",
			  G_CALLBACK (data_channel_on_message_string), NULL);
}

	static void
on_data_channel (GstElement * webrtc, GObject * data_channel,
		 gpointer user_data)
{
	connect_data_channel_signals (data_channel);
	receive_channel = data_channel;
}

	static void
on_ice_gathering_state_notify (GstElement * webrtcbin, GParamSpec * pspec,
			       gpointer user_data)
{
	GstWebRTCICEGatheringState ice_gather_state;
	g_object_get (webrtcbin, "ice-gathering-state", &ice_gather_state, NULL);
	char *state[] = { [GST_WEBRTC_ICE_GATHERING_STATE_NEW] = "new",
		[GST_WEBRTC_ICE_GATHERING_STATE_GATHERING] = "gathering",
		[GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE] = "complete"
	};
	g_print ("ICE gathering state changed to %s\n", state[ice_gather_state]);
}
#endif

static gboolean start_pipeline (void)
{
	GError *error = NULL;

#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="

	char * p =
		"webrtcbin bundle-policy=max-bundle name=sendrecv "
		" stun-server=stun://stun.l.google.com:19302 "
#if __x86_64__
				  "videotestsrc ! "
#else
				  "videotestsrc ! "
				  //"aravissrc ! "
#endif
				  "videoconvert ! "
				  "queue ! "
#if __x86_64__
				  "vp8enc deadline=1 ! "
				  "rtpvp8pay ! queue ! " RTP_CAPS_VP8 "96 ! "
#else

				  //"omxh264enc ! "
				  "nvvidconv ! omxvp8enc ! "
				  //"video/x-vp8 ! vp8parse ! "
				  "video/x-vp8, mapping=/stream1 ! "
				  "rtpvp8pay ! queue ! " RTP_CAPS_VP8 "96 ! "
#endif
				  //"x264enc ! rtph264pay mtu=1400 ! "
				  "sendrecv. ";
	pipe1 = gst_parse_launch (p , &error);

	if (error) {
		g_printerr ("Failed to parse launch: %s\n%s\n", error->message, p);
		g_error_free (error);
		goto err;
	}

	webrtc1 = gst_bin_get_by_name (GST_BIN (pipe1), "sendrecv");
	g_assert_nonnull (webrtc1);

	/* This is the gstwebrtc entry point where we create the offer and so on. It
	 * will be called when the pipeline goes to PLAYING. */
	g_signal_connect (webrtc1, "on-negotiation-needed", G_CALLBACK (on_negotiation_needed), NULL);
	/* We need to transmit this ICE candidate to the browser via the websockets
	 * signalling server. Incoming ice candidates from the browser need to be
	 * added by us too, see on_server_message() */
	g_signal_connect (webrtc1, "on-ice-candidate", G_CALLBACK (send_ice_candidate_message), NULL);
	//g_signal_connect (webrtc1, "notify::ice-gathering-state", G_CALLBACK (on_ice_gathering_state_notify), NULL);

	gst_element_set_state (pipe1, GST_STATE_READY);
#if 0
	g_signal_emit_by_name (webrtc1, "create-data-channel", "channel", NULL,
			       &send_channel);
	if (send_channel) {
		g_print ("Created data channel\n");
		connect_data_channel_signals (send_channel);
	} else {
		g_print ("Could not create data channel, is usrsctp available?\n");
	}

	//g_signal_connect (webrtc1, "on-data-channel", G_CALLBACK (on_data_channel), NULL);
	/* Incoming streams will be exposed via this signal */
	g_signal_connect (webrtc1, "pad-added", G_CALLBACK (on_incoming_stream),
			  pipe1);
#endif
	/* Lifetime is the same as the pipeline itself */
	gst_object_unref (webrtc1);

	g_print ("Starting pipeline\n");
	GstStateChangeReturn ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE)
		goto err;

	return TRUE;

err:
	if (pipe1)
		g_clear_object (&pipe1);
	if (webrtc1)
		webrtc1 = NULL;
	return FALSE;
}

	static void
on_server_closed (SoupWebsocketConnection * conn G_GNUC_UNUSED,
		  gpointer user_data G_GNUC_UNUSED)
{
	app_state = SERVER_CLOSED;
	cleanup_and_quit_loop ("Server connection closed", 0);
}

/* Answer created by our pipeline, to be sent to the peer */
	static void
on_answer_created (GstPromise * promise, gpointer user_data)
{
	GstWebRTCSessionDescription *answer = NULL;

	g_assert_cmphex (app_state, ==, PEER_CALL_NEGOTIATING);

	g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
	gst_structure_get (gst_promise_get_reply (promise), "answer",
			   GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
	gst_promise_unref (promise);

	g_signal_emit_by_name (webrtc1, "set-local-description", answer, NULL);

	send_sdp_to_peer (answer);
	gst_webrtc_session_description_free (answer);
}

	static void
on_offer_received (GstSDPMessage * sdp)
{
	GstWebRTCSessionDescription *offer = NULL;

	offer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_OFFER, sdp);
	g_assert_nonnull (offer);

	/* Set remote description on our pipeline */
	g_signal_emit_by_name (webrtc1, "set-remote-description", offer, NULL);
	gst_webrtc_session_description_free (offer);

	g_signal_emit_by_name (webrtc1, "create-answer", NULL,
			       gst_promise_new_with_change_func (on_answer_created, NULL, NULL));
}

int handle_sdp(JsonObject *obj)
{
	g_assert_cmphex (app_state, ==, PEER_CALL_NEGOTIATING);

	JsonObject *child = json_object_get_object_member (obj, "sdp");

	/* In this example, we create the offer and receive one answer by default,
	 * but it's possible to comment out the offer creation and wait for an offer
	 * instead, so we handle either here.
	 *
	 * See tests/examples/webrtcbidirectional.c in gst-plugins-bad for another
	 * example how to handle offers from peers and reply with answers using webrtcbin. */
	const gchar *text = json_object_get_string_member (child, "sdp");
	GstSDPMessage *sdp;
	int  ret = gst_sdp_message_new (&sdp);
	g_assert_cmphex (ret, ==, GST_SDP_OK);
	ret = gst_sdp_message_parse_buffer ((guint8 *) text, strlen (text), sdp);
	g_assert_cmphex (ret, ==, GST_SDP_OK);
	const char * type = json_object_get_string_member (child, "type");
	if (!g_strcmp0 (type, "answer")) {
		g_signal_emit_by_name (webrtc1, "set-remote-description",
				       gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER, sdp), 0);
		app_state = PEER_CALL_STARTED;
	} else if (!g_strcmp0 (type, "offer")) {
		on_offer_received (sdp);
	}
	return 0;
}

int handle_json_message(char *text)
{
	// like handle_peer_message(peer_id, text);
	g_autoptr(JsonParser) parser = json_parser_new ();
	json_parser_load_from_data (parser, text, -1, NULL);
	JsonObject *obj = json_node_get_object (json_parser_get_root (parser));
	if (json_object_has_member (obj, "sdp")) {
		return handle_sdp(obj);
	} else if (json_object_has_member (obj, "ice")) {
		JsonObject *child = json_object_get_object_member (obj, "ice");

		g_signal_emit_by_name (webrtc1, "add-ice-candidate",
				       json_object_get_int_member (child, "sdpMLineIndex"),
				       json_object_get_string_member (child, "candidate"));
		return 0;
	}

	return -1;
}

/* One mega message handler for our asynchronous calling mechanism */
	static void
on_server_message (SoupWebsocketConnection * conn, SoupWebsocketDataType type,
		   GBytes * message, gpointer user_data)
{
	if (type != SOUP_WEBSOCKET_DATA_TEXT) {
		g_printerr ("Received unknown binary message, ignoring\n");
		return;
	}

	gsize size;
	const gchar *data = g_bytes_get_data (message, &size);
	/* Convert to NULL-terminated string */
	g_autofree gchar *text = g_strndup (data, size);
	g_printerr ("message '%s'", text);

	/* Server has accepted our registration, we are ready to send commands */
	if (!g_strcmp0 (text, "HELLO")) {
		if (app_state != SERVER_REGISTERING) {
			cleanup_and_quit_loop ("ERROR: Received HELLO when not registering",
					       APP_STATE_ERROR);
			goto out;
		}
		g_print ("Registered with server\n");
		/* Ask signalling server to connect us with a specific peer */
		g_assert (soup_websocket_connection_get_state (sigsrv) ==
			  SOUP_WEBSOCKET_STATE_OPEN);
		app_state = PEER_CONNECTING;
		g_autofree gchar *msg = g_strdup_printf ("SESSION %s", peer_id);
		soup_websocket_connection_send_text (sigsrv, msg);
		/* Call has been setup by the server, now we can start negotiation */
	} else if (!g_strcmp0 (text, "SESSION_OK")) {
		if (app_state != PEER_CONNECTING) {
			cleanup_and_quit_loop ("ERROR: Received SESSION_OK when not calling",
					       PEER_CONNECTION_ERROR);
			goto out;
		}

		/* Start negotiation (exchange SDP and ICE candidates) */
		if (!start_pipeline ())
			cleanup_and_quit_loop ("ERROR: failed to start pipeline",
					       PEER_CALL_ERROR);
		/* Handle errors */
	} else if (g_str_has_prefix (text, "ERROR")) {
		g_printerr ("%s", text);
		app_state = APP_STATE_ERROR;
		cleanup_and_quit_loop (text, 0);
		/* Look for JSON messages containing SDP and ICE candidates */
	} else {
		if (handle_json_message(text) < 0)
			g_printerr ("Unknown json message '%s', ignoring", text);
	}
out:;
}

	static void
on_server_connected (SoupSession * session, GAsyncResult * res,
		     SoupMessage * msg)
{
	g_autoptr (GError) error = NULL;

	sigsrv = soup_session_websocket_connect_finish (session, res, &error);
	if (error) {
		cleanup_and_quit_loop (error->message, SERVER_CONNECTION_ERROR);
		return;
	}
	g_signal_connect (sigsrv, "closed", G_CALLBACK (on_server_closed), NULL);
	g_signal_connect (sigsrv, "message", G_CALLBACK (on_server_message), NULL);

	gint32 our_id = g_random_int_range (10, 10000);
	g_print ("Registering id %i with server\n", our_id);
	app_state = SERVER_REGISTERING;

	//soup_websocket_connection_close (sigsrv, 1000, ""); // FIXME
	g_autofree gchar *hello = g_strdup_printf ("HELLO %i", our_id);
	soup_websocket_connection_send_text (sigsrv, hello);
	/* expect on_server_message */
}

/*
 * Connect to the signalling server. This is the entrypoint for everything else.
 */
	static void
connect_to_websocket_server_async (void)
{
	SoupLogger *logger;
	SoupMessage *message;
	SoupSession *session;
	const char *https_aliases[] = { "wss", NULL };

	session =
		soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, !disable_ssl,
					       SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
					       //SOUP_SESSION_SSL_CA_FILE, "/etc/ssl/certs/ca-bundle.crt",
					       SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

	logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
	soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
	g_object_unref (logger);

	message = soup_message_new (SOUP_METHOD_GET, server_url);

	g_print ("Connecting to server...\n");

	/* Once connected, we will register */
	soup_session_websocket_connect_async (session, message, NULL, NULL, NULL,
					      (GAsyncReadyCallback) on_server_connected, message);
	app_state = SERVER_CONNECTING;
}

	static gboolean
check_plugins (void)
{
	int i;
	gboolean ret;
	GstPlugin *plugin;
	GstRegistry *registry;
	const gchar *needed[] = { "opus", "vpx", "nice", "webrtc", "dtls", "srtp",
		"rtpmanager", "videotestsrc", NULL
	};

	registry = gst_registry_get ();
	ret = TRUE;
	for (i = 0; i < g_strv_length ((gchar **) needed); i++) {
		plugin = gst_registry_find_plugin (registry, needed[i]);
		if (!plugin) {
			g_print ("Required gstreamer plugin '%s' not found\n", needed[i]);
			ret = FALSE;
			continue;
		}
		gst_object_unref (plugin);
	}
	return ret;
}

int
main (int argc, char *argv[])
{
	GOptionContext *context;
	context = g_option_context_new ("- gstreamer webrtc sendrecv demo");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_add_group (context, gst_init_get_option_group ());
	   GError *error = NULL;
	   if (!g_option_context_parse (context, &argc, &argv, &error)) {
	   g_printerr ("Error initializing: %s\n", error->message);
	   return -1;
	   }

	if (!check_plugins ())
		return -1;

	if (!peer_id) {
		g_printerr ("--peer-id is a required argument\n");
		return -1;
	}

	/* Disable ssl when running a localhost server, because
	 * it's probably a test server with a self-signed certificate */
	{
		GstUri *uri = gst_uri_from_string (server_url);
		if (g_strcmp0 ("localhost", gst_uri_get_host (uri)) == 0 ||
		    g_strcmp0 ("127.0.0.1", gst_uri_get_host (uri)) == 0)
			disable_ssl = TRUE;
		gst_uri_unref (uri);
	}

//#if __x86_64__
	loop = g_main_loop_new (NULL, FALSE);
	connect_to_websocket_server_async ();
	g_main_loop_run (loop);
	g_main_loop_unref (loop);
//#else
//	connect_to_websocket_server_async ();
//#endif

	if (pipe1) {
		gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_NULL);
		g_print ("Pipeline stopped\n");
		gst_object_unref (pipe1);
	}

	return 0;
}
