//#define test_mode 1
//#define soft_enc 1
//#define small 1
//#define videotestsrc 1
//#define test_limit 3
/* Aravis - Digital camera library
 *
 * Copyright © 2009-2019 Emmanuel Pacaud
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Emmanuel Pacaud <emmanuel@gnome.org>
 */
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>

#include <assert.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/gstregistry.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <../gst/gstaravis.h>
#include <arv.h>
#include <math.h>
#include <memory.h>
#include <libnotify/notify.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>  // for GDK_WINDOW_XID
#endif
#ifdef GDK_WINDOWING_WIN32
#include <gdk/gdkwin32.h>  // for GDK_WINDOW_HWND
#endif
#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <gst/gststructure.h>
#include "../gst/gstaravis.h"

//#define ORIG 1

static gdouble x, y;

static gboolean
_event (GstBaseSrc * src, GstEvent * event)
{	trllog();

	const GstStructure *s;
	const gchar *type;
	switch (GST_EVENT_TYPE (event)) {
	case GST_EVENT_NAVIGATION:
		s = gst_event_get_structure (event);
		type = gst_structure_get_string (s, "event");
		if (g_str_equal(type, "mouse-move")) {
			break;
		}
		gst_structure_get_double (s, "pointer_x", &x);
		gst_structure_get_double (s, "pointer_y", &y);
		trvs_(type);
		trvf_(x);
		trvf_(y);
		/*
		   gint fps_n, fps_d;
		   gint fps_n, fps_d;
		   GstVideoInfo *info = &GST_VIDEO_FILTER (src)->in_info;
		   fps_n = GST_VIDEO_INFO_FPS_N (info);
		   fps_d = GST_VIDEO_INFO_FPS_D (info);
		   trvd_(fps_n);
		   trvd_(fps_d);
		 */
		trln();
		if (g_str_equal (type, "mouse-button-press")) {
		} else if (g_str_equal (type, "mouse-button-release")) {
		}
		break;
	default:;
	}
	return ((GstBaseSrcClass*)g_type_class_peek_parent(GST_BASE_SRC_GET_CLASS(src)))->event(src, event);
}

#if ORIG
static gboolean has_autovideo_sink = FALSE;
static gboolean has_gtksink = FALSE;
static gboolean has_gtkglsink = FALSE;

static gboolean
gstreamer_plugin_check (void)
{	_entry:;
	static gsize check_done = 0;
	static gboolean check_success = FALSE;

	if (g_once_init_enter (&check_done)) {
		GstRegistry *registry;
		GstPluginFeature *feature;
		unsigned int i;
		gboolean success = TRUE;

		static char *plugins[] = {
			"appsrc",
			"videoconvert",
			"videoflip",
			"bayer2rgb"
		};

		registry = gst_registry_get ();

		for (i = 0; i < G_N_ELEMENTS (plugins); i++) {
			feature = gst_registry_lookup_feature (registry, plugins[i]);
			if (!GST_IS_PLUGIN_FEATURE (feature)) {
				g_print ("Gstreamer plugin '%s' is missing.\n", plugins[i]);
				success = FALSE;
			}
			else

				g_object_unref (feature);
		}

		feature = gst_registry_lookup_feature (registry, "autovideosink");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			has_autovideo_sink = TRUE;
			g_object_unref (feature);
		}

		feature = gst_registry_lookup_feature (registry, "gtksink");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			has_gtksink = TRUE;
			g_object_unref (feature);
		}

		feature = gst_registry_lookup_feature (registry, "gtkglsink");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			has_gtkglsink = TRUE;
			g_object_unref (feature);
		}

		if (!has_autovideo_sink && !has_gtkglsink && !has_gtksink) {
			g_print ("Missing GStreamer video output plugin (autovideosink, gtksink or gtkglsink)\n");
			success = FALSE;
		}

		if (!success)
			g_print ("Check your gstreamer installation.\n");

		/* Kludge, prevent autoloading of coglsink, which doesn't seem to work for us */
		feature = gst_registry_lookup_feature (registry, "coglsink");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			gst_plugin_feature_set_rank (feature, GST_RANK_NONE);
			g_object_unref (feature);
		}

		check_success = success;

		g_once_init_leave (&check_done, 1);
	}

	return check_success;
}
#endif

typedef struct {
	GtkApplication parent_instance;

	ArvCamera *camera;
	char *camera_name;
	ArvStream *stream;
	ArvBuffer *last_buffer;

	GstElement *pipeline;
	GstElement *appsrc;
	GstElement *src;
	GstElement *tee, *rec_q, *encoder, *muxer, *filesink;
	GstPad *rec_pad;
	GstElement *transform;

	guint rotation;
	gboolean flip_vertical;
	gboolean flip_horizontal;

	double exposure_min, exposure_max;

	NotifyNotification *notification;

	GtkBuilder *builder;
	GtkWidget *main_window;
	GtkWidget *main_stack;
#if ORIG
	GtkWidget *main_headerbar;
	GtkWidget *camera_box;
	GtkWidget *refresh_button;
	GtkWidget *video_mode_button;
	GtkWidget *camera_tree;
	GtkWidget *back_button;
	GtkWidget *snapshot_button;
	GtkWidget *rotate_cw_button;
	GtkWidget *flip_vertical_toggle;
	GtkWidget *flip_horizontal_toggle;
	GtkWidget *camera_parameters;
	GtkWidget *pixel_format_combo;
	GtkWidget *camera_x;
	GtkWidget *camera_y;
	GtkWidget *camera_binning_x;
	GtkWidget *camera_binning_y;
	GtkWidget *camera_width;
	GtkWidget *camera_height;
	GtkWidget *video_box;
	GtkWidget *video_frame;
#endif
	GtkWidget *fps_label;
	GtkWidget *image_label;
	GtkWidget *trigger_combo_box;
	GtkWidget *frame_rate_entry;
	GtkWidget *exposure_spin_button;
	GtkWidget *gain_spin_button;
	GtkWidget *exposure_hscale;
	GtkWidget *gain_hscale;
	GtkWidget *auto_exposure_toggle;
	GtkWidget *auto_gain_toggle;
	GtkWidget *acquisition_button;

	gulong camera_selected;
	gulong exposure_spin_changed;
	gulong gain_spin_changed;
	gulong exposure_hscale_changed;
	gulong gain_hscale_changed;
	gulong auto_gain_clicked;
	gulong auto_exposure_clicked;
	gulong camera_x_changed;
	gulong camera_y_changed;
	gulong camera_binning_x_changed;
	gulong camera_binning_y_changed;
	gulong camera_width_changed;
	gulong camera_height_changed;
	gulong pixel_format_changed;

	guint gain_update_event;
	guint exposure_update_event;

	guint status_bar_update_event;
	gint64 last_status_bar_update_time_ms;
	unsigned last_n_images;
	unsigned last_n_bytes;
	unsigned n_images;
	unsigned n_bytes;
	unsigned n_errors;

	gboolean auto_socket_buffer;
	gboolean packet_resend;
	guint packet_timeout;
	guint frame_retention;
	ArvRegisterCachePolicy cache_policy;

	gulong video_window_xid;
} ArvViewer;

typedef GtkApplicationClass ArvViewerClass;

G_DEFINE_TYPE (ArvViewer, arv_viewer, GTK_TYPE_APPLICATION)

typedef enum {
	ARV_VIEWER_MODE_CAMERA_LIST,
	ARV_VIEWER_MODE_VIDEO
} ArvViewerMode;

static void 	select_mode 	(ArvViewer *viewer, ArvViewerMode mode);

void
arv_viewer_set_options (ArvViewer *viewer,
			gboolean auto_socket_buffer,
			gboolean packet_resend,
			guint packet_timeout,
			guint frame_retention,
			ArvRegisterCachePolicy cache_policy)
{	_entry:;
	g_return_if_fail (viewer != NULL);

	viewer->auto_socket_buffer = auto_socket_buffer;
	viewer->packet_resend = packet_resend;
	viewer->packet_timeout = packet_timeout;
	viewer->frame_retention = frame_retention;
	viewer->cache_policy = cache_policy;
}

static double
arv_viewer_value_to_log (double value, double min, double max)
{	_entry:;
	if (min >= max)
		return 1.0;

	if (value < min)
		return 0.0;

	return (log10 (value) - log10 (min)) / (log10 (max) - log10 (min));
}

static double
arv_viewer_value_from_log (double value, double min, double max)
{	_entry:;
	if (min <= 0.0 || max <= 0)
		return 0.0;

	if (value > 1.0)
		return max;
	if (value < 0.0)
		return min;

	return pow (10.0, (value * (log10 (max) - log10 (min)) + log10 (min)));
}

#if ORIG
typedef struct {
	GWeakRef stream;
	ArvBuffer* arv_buffer;
	void *data;
} ArvGstBufferReleaseData;

static void
gst_buffer_release_cb (void *user_data)
{	_entry:;
	ArvGstBufferReleaseData* release_data = user_data;

	ArvStream* stream = g_weak_ref_get (&release_data->stream);

	g_free (release_data->data);

	if (stream) {
		gint n_input_buffers, n_output_buffers;

		arv_stream_get_n_buffers (stream, &n_input_buffers, &n_output_buffers);
		arv_log_viewer ("push buffer (%d,%d)", n_input_buffers, n_output_buffers);

		arv_stream_push_buffer (stream, release_data->arv_buffer);
		g_object_unref (stream);
	} else {
		arv_debug_viewer ("invalid stream object");
		g_object_unref (release_data->arv_buffer);
	}

	g_weak_ref_clear (&release_data->stream);
	g_free (release_data);
}

static GstBuffer *
arv_to_gst_buffer (ArvBuffer *arv_buffer, ArvStream *stream)
{	_entry:;
	ArvGstBufferReleaseData* release_data;
	int arv_row_stride;
	int width, height;
	char *buffer_data;
	size_t buffer_size;
	size_t size;
	void *data;

	buffer_data = (char *) arv_buffer_get_data (arv_buffer, &buffer_size);
	arv_buffer_get_image_region (arv_buffer, NULL, NULL, &width, &height);
	arv_row_stride = width * ARV_PIXEL_FORMAT_BIT_PER_PIXEL (arv_buffer_get_image_pixel_format (arv_buffer)) / 8;

	release_data = g_new0 (ArvGstBufferReleaseData, 1);

	g_weak_ref_init (&release_data->stream, stream);
	release_data->arv_buffer = arv_buffer;

	/* Gstreamer requires row stride to be a multiple of 4 */
	if ((arv_row_stride & 0x3) != 0) {
		int gst_row_stride;
		int i;

		gst_row_stride = (arv_row_stride & ~(0x3)) + 4;

		size = height * gst_row_stride;
		data = g_malloc (size);

		for (i = 0; i < height; i++)
			memcpy (((char *) data) + i * gst_row_stride, buffer_data + i * arv_row_stride, arv_row_stride);

		release_data->data = data;

	} else {
		data = buffer_data;
		size = buffer_size;
	}

	return gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
					    data, size, 0, size,
					    release_data, gst_buffer_release_cb);
}

static void
new_buffer_cb (ArvStream *stream, ArvViewer *viewer)
{	_entry:;
	ArvBuffer *arv_buffer;
	gint n_input_buffers, n_output_buffers;

	arv_buffer = arv_stream_pop_buffer (stream);
	if (arv_buffer == NULL)
		return;

	arv_stream_get_n_buffers (stream, &n_input_buffers, &n_output_buffers);
	arv_log_viewer ("pop buffer (%d,%d)", n_input_buffers, n_output_buffers);

	if (arv_buffer_get_status (arv_buffer) == ARV_BUFFER_STATUS_SUCCESS) {
		size_t size;

		arv_buffer_get_data (arv_buffer, &size);

		g_clear_object( &viewer->last_buffer );
		viewer->last_buffer = g_object_ref( arv_buffer );

		gst_app_src_push_buffer (GST_APP_SRC (viewer->appsrc), arv_to_gst_buffer (arv_buffer, stream));

		viewer->n_images++;
		viewer->n_bytes += size;
	} else {
		arv_log_viewer ("push discarded buffer");
		arv_stream_push_buffer (stream, arv_buffer);
		viewer->n_errors++;
	}
}
#endif

static void
frame_rate_entry_cb (GtkEntry *entry, ArvViewer *viewer)
{	_entry:;
	char *text;
	double frame_rate;

	text = (char *) gtk_entry_get_text (entry);

	arv_camera_set_frame_rate (viewer->camera, g_strtod (text, NULL), NULL);

	frame_rate = arv_camera_get_frame_rate (viewer->camera, NULL);
	text = g_strdup_printf ("%g", frame_rate);
	gtk_entry_set_text (entry, text);
	g_free (text);
}

static gboolean
frame_rate_entry_focus_cb (GtkEntry *entry, GdkEventFocus *event,
		    ArvViewer *viewer)
{	_entry:;
	frame_rate_entry_cb (entry, viewer);

	return FALSE;
}

static void
exposure_spin_cb (GtkSpinButton *spin_button, ArvViewer *viewer)
{	_entry:;
	double exposure = gtk_spin_button_get_value (spin_button);
	double log_exposure = arv_viewer_value_to_log (exposure, viewer->exposure_min, viewer->exposure_max);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_exposure_toggle), FALSE);

	arv_camera_set_exposure_time (viewer->camera, exposure, NULL);

	g_signal_handler_block (viewer->exposure_hscale, viewer->exposure_hscale_changed);
	gtk_range_set_value (GTK_RANGE (viewer->exposure_hscale), log_exposure);
	g_signal_handler_unblock (viewer->exposure_hscale, viewer->exposure_hscale_changed);
}

static void
gain_spin_cb (GtkSpinButton *spin_button, ArvViewer *viewer)
{	_entry:;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_gain_toggle), FALSE);

	arv_camera_set_gain (viewer->camera, gtk_spin_button_get_value (spin_button), NULL);

	g_signal_handler_block (viewer->gain_hscale, viewer->gain_hscale_changed);
	gtk_range_set_value (GTK_RANGE (viewer->gain_hscale), gtk_spin_button_get_value (spin_button));
	g_signal_handler_unblock (viewer->gain_hscale, viewer->gain_hscale_changed);
}

static void
exposure_scale_cb (GtkRange *range, ArvViewer *viewer)
{	_entry:;
	double log_exposure = gtk_range_get_value (range);
	double exposure = arv_viewer_value_from_log (log_exposure, viewer->exposure_min, viewer->exposure_max);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_exposure_toggle), FALSE);

	arv_camera_set_exposure_time (viewer->camera, exposure, NULL);

	g_signal_handler_block (viewer->exposure_spin_button, viewer->exposure_spin_changed);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->exposure_spin_button), exposure);
	g_signal_handler_unblock (viewer->exposure_spin_button, viewer->exposure_spin_changed);
}

static void
gain_scale_cb (GtkRange *range, ArvViewer *viewer)
{	_entry:;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_gain_toggle), FALSE);

	arv_camera_set_gain (viewer->camera, gtk_range_get_value (range), NULL);

	g_signal_handler_block (viewer->gain_spin_button, viewer->gain_spin_changed);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->gain_spin_button), gtk_range_get_value (range));
	g_signal_handler_unblock (viewer->gain_spin_button, viewer->gain_spin_changed);
}

gboolean
update_exposure_cb (void *data)
{	_entry:;
	ArvViewer *viewer = data;
	double exposure;
	double log_exposure;

	exposure = arv_camera_get_exposure_time (viewer->camera, NULL);
	log_exposure = arv_viewer_value_to_log (exposure, viewer->exposure_min, viewer->exposure_max);

	g_signal_handler_block (viewer->exposure_hscale, viewer->exposure_hscale_changed);
	g_signal_handler_block (viewer->exposure_spin_button, viewer->exposure_spin_changed);
	gtk_range_set_value (GTK_RANGE (viewer->exposure_hscale), log_exposure);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->exposure_spin_button), exposure);
	g_signal_handler_unblock (viewer->exposure_spin_button, viewer->exposure_spin_changed);
	g_signal_handler_unblock (viewer->exposure_hscale, viewer->exposure_hscale_changed);

	return TRUE;
}

static void
update_exposure_ui (ArvViewer *viewer, gboolean is_auto)
{	_entry:;
	update_exposure_cb (viewer);

	if (viewer->exposure_update_event > 0) {
		g_source_remove (viewer->exposure_update_event);
		viewer->exposure_update_event = 0;
	}

	if (is_auto)
		viewer->exposure_update_event = g_timeout_add_seconds (1, update_exposure_cb, viewer);
}

static void
auto_exposure_cb (GtkToggleButton *toggle, ArvViewer *viewer)
{	_entry:;
	gboolean is_auto;

	is_auto = gtk_toggle_button_get_active (toggle);

	arv_camera_set_exposure_time_auto (viewer->camera, is_auto ? ARV_AUTO_CONTINUOUS : ARV_AUTO_OFF, NULL);
	update_exposure_ui (viewer, is_auto);
}

static gboolean
update_gain_cb (void *data)
{	trllog();
	ArvViewer *viewer = data;
	double gain;

	gain = arv_camera_get_gain (viewer->camera, NULL);

	g_signal_handler_block (viewer->gain_hscale, viewer->gain_hscale_changed);
	g_signal_handler_block (viewer->gain_spin_button, viewer->gain_spin_changed);
	gtk_range_set_value (GTK_RANGE (viewer->gain_hscale), gain);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->gain_spin_button), gain);
	g_signal_handler_unblock (viewer->gain_spin_button, viewer->gain_spin_changed);
	g_signal_handler_unblock (viewer->gain_hscale, viewer->gain_hscale_changed);

	return TRUE;
}

static void
update_gain_ui (ArvViewer *viewer, gboolean is_auto)
{	_entry:;
	update_gain_cb (viewer);

	if (viewer->gain_update_event > 0) {
		g_source_remove (viewer->gain_update_event);
		viewer->gain_update_event = 0;
	}

	if (is_auto)
		viewer->gain_update_event = g_timeout_add_seconds (1, update_gain_cb, viewer);

}


void
auto_gain_cb (GtkToggleButton *toggle, ArvViewer *viewer)
{	_entry:;
	gboolean is_auto;

	is_auto = gtk_toggle_button_get_active (toggle);

	arv_camera_set_gain_auto (viewer->camera, is_auto ? ARV_AUTO_CONTINUOUS : ARV_AUTO_OFF, NULL);
	update_gain_ui (viewer, is_auto);
}

static void
update_camera_region (ArvViewer *viewer);

static void
set_camera_widgets(ArvViewer *viewer)
{	//_entry:;
	trlvx(viewer->camera);
	if (!viewer->camera && viewer->src) {
		viewer->camera = ((GstAravis*)viewer->src)->camera;
		update_camera_region (viewer);
	}
	if (!viewer->camera)
		return;
	static int done;
	if (done)
		return;
#if small
	arv_camera_set_region (viewer->camera, -1, -1, 256, 256, NULL);
#endif
	done = 1;
	g_signal_handler_block (viewer->gain_hscale, viewer->gain_hscale_changed);
	g_signal_handler_block (viewer->gain_spin_button, viewer->gain_spin_changed);
	g_signal_handler_block (viewer->exposure_hscale, viewer->exposure_hscale_changed);
	g_signal_handler_block (viewer->exposure_spin_button, viewer->exposure_spin_changed);

	arv_camera_get_exposure_time_bounds (viewer->camera, &viewer->exposure_min, &viewer->exposure_max, NULL);
	viewer->exposure_max = MIN(100000, viewer->exposure_max);

	gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->exposure_spin_button),
				   viewer->exposure_min, viewer->exposure_max);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->exposure_spin_button), 200.0, 1000.0);
	double gain_min, gain_max;
	arv_camera_get_gain_bounds (viewer->camera, &gain_min, &gain_max, NULL);
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->gain_spin_button), gain_min, gain_max);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->gain_spin_button), 1, 10);

	gtk_range_set_range (GTK_RANGE (viewer->gain_hscale), gain_min, gain_max);

	gtk_widget_set_sensitive (viewer->frame_rate_entry,
				  arv_camera_is_frame_rate_available (viewer->camera, NULL));

	g_autofree char *string;
	string = g_strdup_printf ("%g", arv_camera_get_frame_rate (viewer->camera, NULL));
	gtk_entry_set_text (GTK_ENTRY (viewer->frame_rate_entry), string);

	gboolean ga = arv_camera_is_gain_available (viewer->camera, NULL);
	gtk_widget_set_sensitive (viewer->gain_hscale, ga);
	gtk_widget_set_sensitive (viewer->gain_spin_button, ga);

	gboolean sa = arv_camera_is_exposure_time_available (viewer->camera, NULL);
	gtk_range_set_range (GTK_RANGE (viewer->exposure_hscale), 0.0, 1.0);
	gtk_widget_set_sensitive (viewer->exposure_hscale, sa);
	gtk_widget_set_sensitive (viewer->exposure_spin_button, sa);

	g_signal_handler_unblock (viewer->gain_hscale, viewer->gain_hscale_changed);
	g_signal_handler_unblock (viewer->gain_spin_button, viewer->gain_spin_changed);
	g_signal_handler_unblock (viewer->exposure_hscale, viewer->exposure_hscale_changed);
	g_signal_handler_unblock (viewer->exposure_spin_button, viewer->exposure_spin_changed);

	gboolean auto_gain, auto_exposure;
	auto_gain = arv_camera_get_gain_auto (viewer->camera, NULL) != ARV_AUTO_OFF;
	auto_exposure = arv_camera_get_exposure_time_auto (viewer->camera, NULL) != ARV_AUTO_OFF;

	update_gain_ui (viewer, auto_gain);
	update_exposure_ui (viewer, auto_exposure);

	g_signal_handler_block (viewer->auto_gain_toggle, viewer->auto_gain_clicked);
	g_signal_handler_block (viewer->auto_exposure_toggle, viewer->auto_exposure_clicked);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_gain_toggle), auto_gain);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_exposure_toggle), auto_exposure);

	gtk_widget_set_sensitive (viewer->auto_gain_toggle,
		arv_camera_is_gain_auto_available (viewer->camera, NULL));
	gtk_widget_set_sensitive (viewer->auto_exposure_toggle,
		arv_camera_is_exposure_auto_available (viewer->camera, NULL));

	g_signal_handler_unblock (viewer->auto_gain_toggle, viewer->auto_gain_clicked);
	g_signal_handler_unblock (viewer->auto_exposure_toggle, viewer->auto_exposure_clicked);
}

#if ORIG
void
snapshot_cb (GtkButton *button, ArvViewer *viewer)
{	_entry:;
	GFile *file;
	char *path;
	char *filename;
	GDateTime *date;
	char *date_string;
	int width, height;
	const char *data;
	const char *pixel_format;
	size_t size;

	g_return_if_fail (ARV_IS_CAMERA (viewer->camera));
	g_return_if_fail (ARV_IS_BUFFER (viewer->last_buffer));

	pixel_format = arv_camera_get_pixel_format_as_string (viewer->camera, NULL);
	arv_buffer_get_image_region (viewer->last_buffer, NULL, NULL, &width, &height);
	data = arv_buffer_get_data (viewer->last_buffer, &size);

	path = g_build_filename (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES),
					 "Aravis", NULL);
	file = g_file_new_for_path (path);
	g_free (path);
	g_file_make_directory (file, NULL, NULL);
	g_object_unref (file);

	date = g_date_time_new_now_local ();
	date_string = g_date_time_format (date, "%Y-%m-%d-%H:%M:%S");
	filename = g_strdup_printf ("%s-%s-%d-%d-%s-%s.raw",
				    arv_camera_get_vendor_name (viewer->camera, NULL),
				    arv_camera_get_device_id (viewer->camera, NULL),
				    width,
				    height,
				    pixel_format != NULL ? pixel_format : "Unknown",
				    date_string);
	path = g_build_filename (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES),
				 "Aravis", filename, NULL);
	g_file_set_contents (path, data, size, NULL);

	if (viewer->notification) {
		notify_notification_update (viewer->notification,
					    "Snapshot saved to Image folder",
					    path,
					    "gtk-save");
		notify_notification_show (viewer->notification, NULL);
	}

	g_free (path);
	g_free (filename);
	g_free (date_string);
	g_date_time_unref (date);
}

static void
update_transform (ArvViewer *viewer)
{	_entry:;
	static const gint methods[4][4] = {
		{0, 1, 2, 3},
		{4, 6, 5, 7},
		{5, 7, 4, 6},
		{2, 3, 0, 1}
	};
	int index = (viewer->flip_horizontal ? 1 : 0) + (viewer->flip_vertical ? 2 : 0);

	g_object_set (viewer->transform, "method", methods[index][viewer->rotation % 4], NULL);
}

static void
rotate_cw_cb (GtkButton *button, ArvViewer *viewer)
{	_entry:;
	viewer->rotation = (viewer->rotation + 1) % 4;

	update_transform (viewer);
}

static void
flip_horizontal_cb (GtkToggleButton *toggle, ArvViewer *viewer)
{	_entry:;
	viewer->flip_horizontal = gtk_toggle_button_get_active (toggle);

	update_transform (viewer);
}

static void
flip_vertical_cb (GtkToggleButton *toggle, ArvViewer *viewer)
{	_entry:;
	viewer->flip_vertical = gtk_toggle_button_get_active (toggle);

	update_transform (viewer);
}

static void
stream_cb (void *user_data, ArvStreamCallbackType type, ArvBuffer *buffer)
{	_entry:;
	if (type == ARV_STREAM_CALLBACK_TYPE_INIT) {
		if (!arv_make_thread_realtime (10) &&
		    !arv_make_thread_high_priority (-10))
			g_warning ("Failed to make stream thread high priority");
	}
}
#endif

float timer_diff(struct timeval *t2, struct timeval *t1)
{	//_entry:;
	struct timeval d;
	timersub(t2, t1, &d);
	return d.tv_sec + d.tv_usec / 1e6;
}

int get_usage(int *user, int *kernel, int *idle)
{	//_entry:;
	/*
	   static struct timespec t1;
	   struct timespec t2;
	// CLOCK_MONOTONIC_COARSE
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t2);
	clock_gettime(CLOCK_MONOTONIC_RAW, &t2);
	trlvd(t2.tv_sec);
	trlvd(t2.tv_nsec);
	 */
	static struct timeval tv1;
	struct rusage u2;
	getrusage(RUSAGE_SELF, &u2);
	struct timeval tv2;
	gettimeofday(&tv2, NULL);
	static struct rusage u1;
	float d = get_nprocs() * timer_diff(&tv2, &tv1);
	*user = 100*timer_diff(&u2.ru_utime, &u1.ru_utime) / d;
	*kernel = 100*timer_diff(&u2.ru_stime, &u1.ru_stime) / d;
	char g_autofree *s;
	g_file_get_contents("/proc/uptime", &s, 0, NULL);
	static double idle1;
	double idle2;
	sscanf(s,"%*f %lf", &idle2);
	*idle = 100*(idle2 - idle1) / d;
	u1 = u2;
	tv1 = tv2;
	idle1 = idle2;
	return 0;
}

void rec_stop(ArvViewer *ctx);
void restart(GtkButton *b, ArvViewer *ctx);
static gboolean
update_status_cb (void *data)
{
	ArvViewer *viewer = data;
	ArvStream *stream = 0;
	gtk_window_set_keep_above(GTK_WINDOW (viewer->main_window), True);
	char g_autofree *p = 0;
	char g_autofree *t = 0;
	gtk_widget_set_sensitive (viewer->camera_parameters, TRUE);
#if __x86_64__ || videotestsrc
#else
	if (viewer->src)
		stream = ((GstAravis*)viewer->src)->stream;
#endif
	set_camera_widgets(viewer);
	guint n_errors = viewer->n_errors;

	GString *s = g_string_new (0);
	if (!stream) {
		g_string_append_printf(s, "no camera\n");
		goto exit;
	}
	ArvStreamStatistics * st = arv_stream_get_statistics2 (stream);
	if (!st) {
		g_string_append_printf(s, "no data\n");
		goto exit;
	}
	if (st->interval_ms > 0) {
		g_string_append_printf(s, "frame rate: %.1f fps\n", 1e3 / st->interval_ms);
		g_string_append_printf(s, "interval: %d ms\n", st->interval_ms);
		g_atomic_int_set(&st->interval_ms, -1);
		g_string_append_printf(s, "internal latency: %d ms\n", st->latency_ms);
		g_string_append_printf(s, "X: %.0f \n", x);
		g_string_append_printf(s, "Y: %.0f \n", y);
		g_string_append_printf(s, "pixel: %u \n", 1000*st->pixel/255);
		g_atomic_int_set(&st->y, (int)y);
		g_atomic_int_set(&st->x, (int)x);
	}
	g_string_append_printf(s, "Camera temp: %.1f C\n", arv_camera_get_float (viewer->camera, "DeviceTemperature", 0));
	g_file_get_contents ("/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq", &p, 0, NULL);
	g_string_append_printf(s, "CPU freq: %.2g GHz \n", atof(p) / 1e6);
	int user, kernel, idle;
	get_usage(&user, &kernel, &idle);
	g_string_append_printf(s, "CPU usage total (kernel): %d (%d) %%\n", user + kernel, kernel);
	g_string_append_printf(s, "Idle CPU: %d%%\n", idle);
	g_file_get_contents ("/sys/class/hwmon/hwmon0/temp1_input", &t, 0, NULL);
	g_string_append_printf(s, "CPU temp: %.1f C \n", atof(t) / 1e3);
	if (n_errors)
		g_string_append_printf (s, "errors: %u\n", n_errors);

	gint empties = -1, loads;
	arv_stream_get_n_buffers (stream, &empties, &loads);
	if (empties < 10)
		g_string_append_printf (s, "empties: %u\n", empties);
	if (st->n_failures)
		g_string_append_printf (s, "failures: %u\n", st->n_failures);
	if (loads > 2)
		g_string_append_printf (s, "loads: %u\n", loads);
	if (!empties || st->n_failures > 100) {
		trlm("overflow");
		if (ARV_IS_CAMERA(viewer->camera)) {
			rec_stop(viewer);
			arv_device_reset(arv_camera_get_device (viewer->camera));
			g_string_append_printf (s, "\nThe camera was reset\n");
			g_string_append_printf (s, "\nRestart\n");
			//restart(0, viewer);
		}
	}
exit:
	gtk_label_set_label(GTK_LABEL(gtk_builder_get_object(viewer->builder, "stats")), s->str);
	g_string_free (s, TRUE);
	return TRUE;
}

#if ORIG
static gboolean
update_status_bar_cb (void *data)
{	_entry:;
	ArvViewer *viewer = data;
	char *text;
	gint64 time_ms = g_get_real_time () / 1000;
	gint64 elapsed_time_ms = time_ms - viewer->last_status_bar_update_time_ms;
	guint n_images = viewer->n_images;
	guint n_bytes = viewer->n_bytes;
	guint n_errors = viewer->n_errors;

	if (elapsed_time_ms == 0)
		return TRUE;

	text = g_strdup_printf ("%.1f fps (%.1f MB/s)",
				1000.0 * (n_images - viewer->last_n_images) / elapsed_time_ms,
				((n_bytes - viewer->last_n_bytes) / 1000.0) / elapsed_time_ms);
	gtk_label_set_label (GTK_LABEL (viewer->fps_label), text);
	g_free (text);

	text = g_strdup_printf ("%u image%s / %u error%s",
				n_images, n_images > 0 ? "s" : "",
				n_errors, n_errors > 0 ? "s" : "");
	gtk_label_set_label (GTK_LABEL (viewer->image_label), text);
	g_free (text);

	viewer->last_status_bar_update_time_ms = time_ms;
	viewer->last_n_images = n_images;
	viewer->last_n_bytes = n_bytes;

	return TRUE;
}

static void
update_camera_region (ArvViewer *viewer)
{	_entry:;
	gint x, y, width, height;
	gint dx, dy;
	gint min, max;
	gint inc;
	if (!viewer->camera)
		return;

	g_signal_handler_block (viewer->camera_x, viewer->camera_x_changed);
	g_signal_handler_block (viewer->camera_y, viewer->camera_y_changed);
	g_signal_handler_block (viewer->camera_width, viewer->camera_width_changed);
	g_signal_handler_block (viewer->camera_height, viewer->camera_height_changed);
	g_signal_handler_block (viewer->camera_binning_x, viewer->camera_binning_x_changed);
	g_signal_handler_block (viewer->camera_binning_y, viewer->camera_binning_y_changed);

	arv_camera_get_region (viewer->camera, &x, &y, &width, &height, NULL);
	arv_camera_get_binning (viewer->camera, &dx, &dy, NULL);

	arv_camera_get_x_binning_bounds (viewer->camera, &min, &max, NULL);
	inc = arv_camera_get_x_binning_increment (viewer->camera, NULL);
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_binning_x), min, max);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_binning_x), inc, inc * 10);
	gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_binning_x), TRUE);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_binning_x), dx);
	arv_camera_get_y_binning_bounds (viewer->camera, &min, &max, NULL);
	inc = arv_camera_get_y_binning_increment (viewer->camera,  NULL);
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_binning_y), min, max);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_binning_y), inc, inc * 10);
	gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_binning_y), TRUE);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_binning_y), dy);
	arv_camera_get_x_offset_bounds (viewer->camera, &min, &max, NULL);
	inc = arv_camera_get_x_offset_increment (viewer->camera, NULL);
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_x), min, max);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_x), inc, inc * 10);
	gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_x), TRUE);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_x), x);
	arv_camera_get_y_offset_bounds (viewer->camera, &min, &max, NULL);
	inc = arv_camera_get_y_offset_increment (viewer->camera, NULL);
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_y), min, max);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_y), inc, inc * 10);
	gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_y), TRUE);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_y), y);
	arv_camera_get_width_bounds (viewer->camera, &min, &max, NULL);
	inc = arv_camera_get_width_increment (viewer->camera, NULL);
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_width), min, max);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_width), inc, inc * 10);
	gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_width), TRUE);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_width), width);
	arv_camera_get_height_bounds (viewer->camera, &min, &max, NULL);
	inc = arv_camera_get_height_increment (viewer->camera, NULL);
	gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_height), min, max);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_height), inc, inc * 10);
	gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_height), TRUE);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_height), height);

	g_signal_handler_unblock (viewer->camera_x, viewer->camera_x_changed);
	g_signal_handler_unblock (viewer->camera_y, viewer->camera_y_changed);
	g_signal_handler_unblock (viewer->camera_width, viewer->camera_width_changed);
	g_signal_handler_unblock (viewer->camera_height, viewer->camera_height_changed);
	g_signal_handler_unblock (viewer->camera_binning_x, viewer->camera_binning_x_changed);
	g_signal_handler_unblock (viewer->camera_binning_y, viewer->camera_binning_y_changed);
}

void
camera_region_cb (GtkSpinButton *spin_button, ArvViewer *viewer)
{	_entry:;
	int x = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_x));
	int y = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_y));
	int width = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_width));
	int height = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_height));

	arv_camera_set_region (viewer->camera, x, y, width, height, NULL);

	update_camera_region (viewer);
}

void
camera_binning_cb (GtkSpinButton *spin_button, ArvViewer *viewer)
{	_entry:;
	int dx = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_binning_x));
	int dy = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_binning_y));

	arv_camera_set_binning (viewer->camera, dx, dy, NULL);

	update_camera_region (viewer);
}

void
pixel_format_combo_cb (GtkComboBoxText *combo, ArvViewer *viewer)
{
	char *pixel_format;

	pixel_format = gtk_combo_box_text_get_active_text (combo);
	arv_camera_set_pixel_format_from_string (viewer->camera, pixel_format, NULL);
	g_free (pixel_format);
}

void
update_device_list_cb (GtkToolButton *button, ArvViewer *viewer)
{
	GtkListStore *list_store;
	GtkTreeIter iter;
	unsigned int n_devices;
	unsigned int i;

	gtk_widget_set_sensitive (viewer->video_mode_button, FALSE);
	gtk_widget_set_sensitive (viewer->camera_parameters, FALSE);

	g_signal_handler_block (gtk_tree_view_get_selection (GTK_TREE_VIEW (viewer->camera_tree)), viewer->camera_selected);
	list_store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (viewer->camera_tree)));
	gtk_list_store_clear (list_store);
	arv_update_device_list ();
	n_devices = arv_get_n_devices ();
	for (i = 0; i < n_devices; i++) {
		GString *protocol;

		protocol = g_string_new (NULL);
		g_string_append_printf (protocol, "aravis-%s-symbolic", arv_get_device_protocol (i));
		g_string_ascii_down (protocol);

		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    0, arv_get_device_id (i),
				    1, protocol->str,
				    2, arv_get_device_vendor (i),
				    3, arv_get_device_model (i),
				    4, arv_get_device_serial_nbr (i),
				    -1);

		g_string_free (protocol, TRUE);
	}
	g_signal_handler_unblock (gtk_tree_view_get_selection (GTK_TREE_VIEW (viewer->camera_tree)), viewer->camera_selected);
}

static void
remove_widget (GtkWidget *widget, gpointer data)
{	_entry:;
	gtk_container_remove (data, widget);
	g_object_unref (widget);
}
#endif

static void
stop_video (ArvViewer *viewer)
{	_entry:;
	//if (viewer->rec_pad)
	trlvd(GST_OBJECT_REFCOUNT(viewer->src));
	trvs(GST_OBJECT_NAME(viewer->src));
	gst_element_send_event (viewer->pipeline, gst_event_new_eos ()); // hangs after reset
	sleep(1);
	trl();
	if (GST_IS_PIPELINE (viewer->pipeline))
		gst_element_set_state (viewer->pipeline, GST_STATE_NULL);
	trlvd(GST_OBJECT_REFCOUNT(viewer->src));
#if ORIG
	if (ARV_IS_STREAM (viewer->stream))
		arv_stream_set_emit_signals (viewer->stream, FALSE);

	g_clear_object (&viewer->stream);
	g_clear_object (&viewer->pipeline);

	viewer->appsrc = NULL;

	g_clear_object (&viewer->last_buffer);
	if (ARV_IS_CAMERA (viewer->camera))
		arv_camera_stop_acquisition (viewer->camera, NULL);
	gtk_container_foreach (GTK_CONTAINER (viewer->video_frame), remove_widget, viewer->video_frame);
#endif
#if !ORIG
	viewer->rec_pad = 0;
	viewer->filesink = 0;
	viewer->camera = NULL;
#endif

	if (viewer->status_bar_update_event > 0) {
		g_source_remove (viewer->status_bar_update_event);
		viewer->status_bar_update_event = 0;
	}

	if (viewer->exposure_update_event > 0) {
		g_source_remove (viewer->exposure_update_event);
		viewer->exposure_update_event = 0;
	}

	if (viewer->gain_update_event > 0) {
		g_source_remove (viewer->gain_update_event);
		viewer->gain_update_event = 0;
	}
	trlvd(GST_OBJECT_REFCOUNT(viewer->pipeline));
	g_object_unref (viewer->pipeline);
}

#if ORIG
static GstBusSyncReply
bus_sync_handler (GstBus *bus, GstMessage *message, gpointer user_data)
{	_entry:;
	ArvViewer *viewer = user_data;

	if (!gst_is_video_overlay_prepare_window_handle_message(message))
		return GST_BUS_PASS;

	if (viewer->video_window_xid != 0) {
		GstVideoOverlay *videooverlay;

		videooverlay = GST_VIDEO_OVERLAY (GST_MESSAGE_SRC (message));
		gst_video_overlay_set_window_handle (videooverlay, viewer->video_window_xid);
	} else {
		g_warning ("Should have obtained video_window_xid by now!");
	}

	gst_message_unref (message);

	return GST_BUS_DROP;
}
#endif

gboolean
gst_bin_add_link_many (GstElement * bin, GstElement * a, GstElement * b, ...)
{	_entry:;
	va_list args;
	gboolean res = TRUE;

	g_return_val_if_fail (GST_IS_BIN (bin), 0);
	g_return_val_if_fail (GST_IS_ELEMENT (a), FALSE);

	gst_bin_add (GST_BIN(bin), a);
	gst_element_sync_state_with_parent(a);
	trvs(GST_ELEMENT_NAME(a));
	va_start (args, b);

	while (b) {
		g_return_val_if_fail (GST_IS_ELEMENT (b), FALSE);
		trvs(GST_ELEMENT_NAME(b));
		trlvd(GST_OBJECT_REFCOUNT(b));
		gst_bin_add (GST_BIN(bin), b);
		gst_element_sync_state_with_parent(b);

		trlvd(GST_OBJECT_REFCOUNT(b));
		if (!gst_element_link (a, b)) {
			res = FALSE;
			break;
		}
		trlvd(GST_OBJECT_REFCOUNT(b));
		a = b;
		b = va_arg (args, GstElement *);
	}

	va_end (args);

	return res;
}

gboolean
gst_bin_unlink_remove_many (GstElement * bin, GstElement * a, GstElement * b, ...)
{	_entry:;
	va_list args;
	gboolean res = TRUE;

	g_return_val_if_fail (GST_IS_BIN (bin), 0);
	g_return_val_if_fail (GST_IS_ELEMENT (a), FALSE);

	gst_element_set_state(a, GST_STATE_NULL);
	va_start (args, b);

	while (b) {
		g_return_val_if_fail (GST_IS_ELEMENT (b), FALSE);
		trlvd(GST_OBJECT_REFCOUNT(b));
		gst_element_unlink (a, b);
		trlvd(GST_OBJECT_REFCOUNT(b));
		gst_element_set_state(b, GST_STATE_NULL);
		assert(gst_bin_remove (GST_BIN(bin), a));

		trlvd(GST_OBJECT_REFCOUNT(b));
		a = b;
		b = va_arg (args, GstElement *);
	}
	assert(gst_bin_remove (GST_BIN(bin), a));
	trlvd(GST_OBJECT_REFCOUNT(a));
	va_end (args);

	return res;
}

void set_filename(ArvViewer *ctx)
{	_entry:;
#if __x86_64__ || test_mode
	static gint counter;
	char g_autofree *rec_path = g_strdup_printf("rec-%02d.mk", counter++);
	char *fn = rec_path;
#else
	g_autoptr(GDateTime) time = g_date_time_new_now_local();
	char g_autofree *fn = g_date_time_format(time, "A-%y%m%d+%H%M%S.mkv");
	char g_autofree *rec_path = g_build_filename(g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS), fn, NULL);
#endif
	gtk_entry_set_text (GTK_ENTRY(GTK_WIDGET(gtk_builder_get_object (ctx->builder, "rec_file"))), fn);
	g_object_set(ctx->filesink, "location", rec_path, NULL);
	trvs(rec_path);
}

void rec_make(ArvViewer *ctx)
{	_entry:;
	ctx->rec_q = gst_element_factory_make("queue", "rec_q");
	trlvd(GST_OBJECT_REFCOUNT(ctx->rec_q));
#if __x86_64__ || soft_enc
	ctx->encoder = gst_element_factory_make("x264enc", NULL);
	g_object_set(ctx->encoder, "tune", 4, NULL); // zerolatency
#else
	ctx->encoder = gst_element_factory_make("omxh264enc", NULL);
#endif
	ctx->muxer = gst_element_factory_make("matroskamux", NULL);
	ctx->filesink = gst_element_factory_make("filesink", NULL);
	set_filename(ctx);
}


void rec_start(ArvViewer *ctx)
{	_entry:;
	/*
	   aravissrc ! videoconvert ! tee name=t \
	   t. ! queue ! omxh264enc ! matroskamux ! filesink location=264.mkv \
	   t. ! xvimagesink
	 */
	trl();
	if (ctx->rec_pad)
		return;
	ctx->rec_pad = gst_element_request_pad(ctx->tee, gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(ctx->tee), "src_%u"), NULL, NULL);
	trvx(ctx->rec_pad);
	rec_make(ctx);

	trlvd(GST_OBJECT_REFCOUNT(ctx->rec_q));
	trlvd(GST_OBJECT_REFCOUNT(ctx->filesink));
	gst_bin_add_link_many(ctx->pipeline,
			      ctx->rec_q,
			      ctx->encoder,
			      ctx->muxer,
			      ctx->filesink,
			      0);
	g_autoptr(GstPad) rec_sinkpad = gst_element_get_static_pad(ctx->rec_q, "sink");
	trvx(rec_sinkpad);
	int ret;
	ret = gst_pad_link(ctx->rec_pad, rec_sinkpad);
	trlvd(ret);
	trlvd(GST_OBJECT_REFCOUNT(ctx->rec_q));
	trlvd(GST_OBJECT_REFCOUNT(ctx->filesink));
}

static GstPadProbeReturn unlink_cb(GstPad *pad, GstPadProbeInfo *info, ArvViewer *ctx)
{	_entry:;
	if (!ctx->rec_pad)
		return GST_PAD_PROBE_REMOVE;
	trlvd(GST_OBJECT_REFCOUNT(ctx->rec_q));
	g_object_ref(ctx->filesink);
	trlvd(GST_OBJECT_REFCOUNT(ctx->rec_q));
	{
		g_autoptr(GstPad) rec_sinkpad = gst_element_get_static_pad (ctx->rec_q, "sink");
		gst_pad_unlink (ctx->rec_pad, rec_sinkpad);
	}

	gst_element_release_request_pad ((GstElement *)ctx->tee, ctx->rec_pad);
	trlvd(GST_OBJECT_REFCOUNT(ctx->rec_pad));
	gst_object_unref (ctx->rec_pad);
	ctx->rec_pad = 0;
	gst_bin_unlink_remove_many(ctx->pipeline, ctx->rec_q, ctx->encoder, ctx->muxer, ctx->filesink, NULL);
	trlvd(GST_OBJECT_REFCOUNT(ctx->filesink));
	g_object_unref(ctx->filesink);

	return GST_PAD_PROBE_REMOVE;
}

void rec_stop(ArvViewer *ctx)
{	_entry:;
	trl();
	gst_pad_add_probe(ctx->rec_pad, GST_PAD_PROBE_TYPE_IDLE, (GstPadProbeCallback) unlink_cb, ctx, NULL);
}

int make_video (ArvViewer *viewer)
{	_entry:;
	trl();
#if __x86_64__ || videotestsrc
	viewer->src = gst_element_factory_make ("videotestsrc", 0);
	/*
	   caps = gst_caps_new_empty ();
	   GValue fps = G_VALUE_INIT;
	   g_value_init (&fps, GST_TYPE_FRACTION);
	   gst_value_set_fraction (&fps, 10, 1);
	   gst_caps_set_value (caps, "framerate", &fps);
	   gst_base_src_set_caps (GST_BASE_SRC (viewer->src), caps);*/
	//g_value_unset (&fps);

	//g_object_set(viewer->src, "pattern", 18, NULL); // GST_VIDEO_TEST_SRC_BALL
	//g_object_set(viewer->src, "background-color", 0x80808080, NULL);
	//g_object_set(viewer->src, "do-timestamp", TRUE, NULL);
#else
	viewer->src = gst_element_factory_make ("aravissrc", 0);
#endif
	GST_BASE_SRC_GET_CLASS(viewer->src)->event = GST_DEBUG_FUNCPTR (_event);
	trvp(GST_BASE_SRC_CLASS(&((GstAravisClass*)GST_BASE_SRC_GET_CLASS(viewer->src))->parent_class));
	trvp(&(((GstAravisClass*)GST_BASE_SRC_GET_CLASS(viewer->src))->parent_class.parent_class));
	trvp(GST_BASE_SRC_GET_CLASS(GST_BASE_SRC_GET_CLASS(viewer->src)));
	trvp(g_type_class_peek_parent(GST_BASE_SRC_GET_CLASS(viewer->src)));

	assert(viewer->src);
	viewer->pipeline = gst_pipeline_new ("pipeline");
	GstElement *last;

	gst_bin_add_link_many(viewer->pipeline,
			      viewer->src,
			      gst_element_factory_make ("videoconvert", NULL),
			      viewer->tee = gst_element_factory_make("tee", NULL),
#if __x86_64__
			      //GstElement *videosink = gst_element_factory_make ("autovideosink", NULL); // works with videotestsrc
			      //GstElement *videosink = gst_element_factory_make ("fpsdisplaysink", NULL);
			      //GstElement *videosink = gst_element_factory_make ("gtksink", NULL);
			      gst_element_factory_make ("xvimagesink", NULL),
#else
			      gst_element_factory_make ("xvimagesink", NULL),
			      //GstBus_autoptr bus = gst_pipeline_get_bus (GST_PIPELINE (viewer->pipeline));
			      //gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, viewer, NULL);
			      // Next: video_frame_realize_cb, bus_sync_handler
#endif
			      0);
	trlvd(GST_OBJECT_REFCOUNT(viewer->src));
	return 1;
}

int start_video2(ArvViewer *ctx)
{	_entry:;
	trl();
	gst_element_set_state (ctx->pipeline, GST_STATE_PLAYING);
	if (ctx->camera)
		arv_camera_start_acquisition (ctx->camera, NULL);
	ctx->status_bar_update_event = g_timeout_add_seconds (1, update_status_cb, ctx);
	trl();
	return 0;
}

#if ORIG
static gboolean
start_video (ArvViewer *viewer)
{	_entry:;
	GstElement *videoconvert;
	GstElement *videosink;
	GstCaps *caps;
	ArvPixelFormat pixel_format;
	unsigned payload;
	unsigned i;
	gint width, height;
	const char *caps_string;

	//if (!ARV_IS_CAMERA (viewer->camera))
	//	return FALSE;

	stop_video (viewer);

	viewer->rotation = 0;

	viewer->stream = arv_camera_create_stream (viewer->camera, stream_cb, NULL, NULL);
	if (!ARV_IS_STREAM (viewer->stream)) {

		g_object_unref (viewer->camera);
		viewer->camera = NULL;
		return FALSE;
	}

	if (ARV_IS_GV_STREAM (viewer->stream)) {
		if (viewer->auto_socket_buffer)
			g_object_set (viewer->stream,
				      "socket-buffer", ARV_GV_STREAM_SOCKET_BUFFER_AUTO,
				      "socket-buffer-size", 0,
				      NULL);
		if (!viewer->packet_resend)
			g_object_set (viewer->stream,
				      "packet-resend", ARV_GV_STREAM_PACKET_RESEND_NEVER,
				      NULL);
		g_object_set (viewer->stream,
			      "packet-timeout", (unsigned) viewer->packet_timeout * 1000,
			      "frame-retention", (unsigned) viewer->frame_retention * 1000,
			      NULL);
	}

	arv_stream_set_emit_signals (viewer->stream, TRUE);
	payload = arv_camera_get_payload (viewer->camera, NULL);
	for (i = 0; i < 5; i++)
		arv_stream_push_buffer (viewer->stream, arv_buffer_new (payload, NULL));
	set_camera_widgets(viewer);
	pixel_format = arv_camera_get_pixel_format (viewer->camera, NULL);

	caps_string = arv_pixel_format_to_gst_caps_string (pixel_format);
	if (caps_string == NULL) {
		g_message ("GStreamer cannot understand the camera pixel format: 0x%x!\n", (int) pixel_format);
		//stop_video (viewer);
		//return FALSE;
	}

	viewer->pipeline = gst_pipeline_new ("pipeline");

	videoconvert = gst_element_factory_make ("videoconvert", NULL);
#if __x86_64__ || test_mode > 3
		viewer->src = gst_element_factory_make ("videotestsrc", 0);
		g_object_set(viewer->src, "pattern", 18, NULL); // GST_VIDEO_TEST_SRC_BALL
		g_object_set(viewer->src, "background-color", 0x80808080, NULL);
		g_object_set(viewer->src, "do-timestamp", TRUE, NULL);
#else
#endif
	viewer->appsrc = gst_element_factory_make ("appsrc", NULL);
	viewer->transform = gst_element_factory_make ("videoflip", NULL);

	gst_bin_add_many (GST_BIN (viewer->pipeline), viewer->appsrc, videoconvert, viewer->transform, NULL);

	if (g_str_has_prefix (caps_string, "video/x-bayer")) {
		GstElement *bayer2rgb;

		bayer2rgb = gst_element_factory_make ("bayer2rgb", NULL);
		gst_bin_add (GST_BIN (viewer->pipeline), bayer2rgb);
		gst_element_link_many (viewer->appsrc, bayer2rgb, videoconvert, viewer->transform, NULL);
	} else {
		gst_element_link_many (viewer->appsrc, videoconvert, viewer->transform, NULL);
	}

	if (has_gtksink || has_gtkglsink) {
		GtkWidget *video_widget;

#if 0 /* Disable glsink for now, it crashes when we come back to camera list with:
	(lt-arv-viewer:29151): Gdk-WARNING **: eglMakeCurrent failed
	(lt-arv-viewer:29151): Gdk-WARNING **: eglMakeCurrent failed
	(lt-arv-viewer:29151): Gdk-WARNING **: eglMakeCurrent failed
	Erreur de segmentation (core dumped)
	*/

		videosink = gst_element_factory_make ("gtkglsink", NULL);

		if (GST_IS_ELEMENT (videosink)) {
			GstElement *glupload;

			glupload = gst_element_factory_make ("glupload", NULL);
			gst_bin_add_many (GST_BIN (viewer->pipeline), glupload, videosink, NULL);
			gst_element_link_many (viewer->transform, glupload, videosink, NULL);
		} else {
#else
		{
#endif
			videosink = gst_element_factory_make ("gtksink", NULL);
			gst_bin_add_many (GST_BIN (viewer->pipeline), videosink, NULL);
			gst_element_link_many (viewer->transform, videosink, NULL);
		}

		g_object_get (videosink, "widget", &video_widget, NULL);
		gtk_container_add (GTK_CONTAINER (viewer->video_frame), video_widget);
		gtk_widget_show (video_widget);
		g_object_set(G_OBJECT (video_widget), "force-aspect-ratio", TRUE, NULL);
		gtk_widget_set_size_request (video_widget, 640, 480);
	} else {
		videosink = gst_element_factory_make ("autovideosink", NULL);
		gst_bin_add (GST_BIN (viewer->pipeline), videosink);
		gst_element_link_many (viewer->transform, videosink, NULL);
	}

	g_object_set(G_OBJECT (videosink), "sync", FALSE, NULL);

	caps = gst_caps_from_string (caps_string);
	arv_camera_get_region (viewer->camera, NULL, NULL, &width, &height, NULL);
	gst_caps_set_simple (caps,
			     "width", G_TYPE_INT, width,
			     "height", G_TYPE_INT, height,
			     "framerate", GST_TYPE_FRACTION, 0, 1,
			     NULL);
	gst_app_src_set_caps (GST_APP_SRC (viewer->appsrc), caps);
	gst_caps_unref (caps);

	g_object_set(G_OBJECT (viewer->appsrc), "format", GST_FORMAT_TIME, "is-live", TRUE, "do-timestamp", TRUE, NULL);

	if (!has_gtkglsink && !has_gtksink) {
		// supposed autovideosink
		GstBus *bus;

		bus = gst_pipeline_get_bus (GST_PIPELINE (viewer->pipeline));
		gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, viewer, NULL);
		gst_object_unref (bus);
	}

	gst_element_set_state (viewer->pipeline, GST_STATE_PLAYING);

	viewer->last_status_bar_update_time_ms = g_get_real_time () / 1000;
	viewer->last_n_images = 0;
	viewer->last_n_bytes = 0;
	viewer->n_images = 0;
	viewer->n_bytes = 0;
	viewer->n_errors = 0;
	viewer->status_bar_update_event = g_timeout_add_seconds (1, update_status_bar_cb, viewer);

	g_signal_connect (viewer->stream, "new-buffer", G_CALLBACK (new_buffer_cb), viewer);
	arv_camera_start_acquisition (viewer->camera, NULL);

	return TRUE;
}

static gboolean
select_camera_list_mode (gpointer user_data)
{	_entry:;
	ArvViewer *viewer = user_data;

	select_mode (viewer, ARV_VIEWER_MODE_CAMERA_LIST);
	update_device_list_cb (GTK_TOOL_BUTTON (viewer->refresh_button), viewer);

	return FALSE;
}

static void
control_lost_cb (ArvCamera *camera, ArvViewer *viewer)
{	_entry:;
	g_main_context_invoke (NULL, select_camera_list_mode, viewer);
}

static void
stop_camera (ArvViewer *viewer)
{	_entry:;
	gtk_widget_set_sensitive (viewer->camera_parameters, FALSE);
	gtk_widget_set_sensitive (viewer->video_mode_button, FALSE);
	stop_video (viewer);
	g_clear_object (&viewer->camera);
	g_clear_pointer (&viewer->camera_name, g_free);
}

static gboolean
start_camera (ArvViewer *viewer, const char *camera_id)
{	_entry:;
	GtkTreeIter iter;
	GtkListStore *list_store;
	gint64 *pixel_formats;
	const char *pixel_format_string;
	const char **pixel_format_strings;
	guint i, n_pixel_formats, n_pixel_format_strings, n_valid_formats;
	gboolean binning_available;

	stop_camera (viewer);

	viewer->camera = arv_camera_new (camera_id, NULL);

	if (!ARV_IS_CAMERA (viewer->camera))
		return FALSE;

	arv_device_set_register_cache_policy (arv_camera_get_device (viewer->camera), viewer->cache_policy);

	viewer->camera_name = g_strdup (camera_id);

	gtk_widget_set_sensitive (viewer->camera_parameters, TRUE);

	//arv_camera_set_chunk_mode (viewer->camera, FALSE, NULL);

	update_camera_region (viewer);

	g_signal_handler_block (viewer->pixel_format_combo, viewer->pixel_format_changed);

	list_store = GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (viewer->pixel_format_combo)));
	gtk_list_store_clear (list_store);
	n_valid_formats = 0;
	pixel_format_strings = arv_camera_dup_available_pixel_formats_as_strings (viewer->camera, &n_pixel_format_strings, NULL);
	pixel_formats = arv_camera_dup_available_pixel_formats (viewer->camera, &n_pixel_formats, NULL);
	//g_assert (n_pixel_formats == n_pixel_format_strings);
	pixel_format_string = arv_camera_get_pixel_format_as_string (viewer->camera, NULL);
	for (i = 0; i < n_pixel_formats; i++) {
		if (arv_pixel_format_to_gst_caps_string (pixel_formats[i]) != NULL) {
			gtk_list_store_append (list_store, &iter);
			gtk_list_store_set (list_store, &iter, 0, pixel_format_strings[i], -1);
			if (g_strcmp0 (pixel_format_strings[i], pixel_format_string) == 0)
				gtk_combo_box_set_active (GTK_COMBO_BOX (viewer->pixel_format_combo), n_valid_formats);
			n_valid_formats++;
		}
	}
	g_free (pixel_formats);
	g_free (pixel_format_strings);
	gtk_widget_set_sensitive (viewer->pixel_format_combo, n_valid_formats > 1);
	gtk_widget_set_sensitive (viewer->video_mode_button, TRUE);

	binning_available = arv_camera_is_binning_available (viewer->camera, NULL);
	gtk_widget_set_sensitive (viewer->camera_binning_x, binning_available);
	gtk_widget_set_sensitive (viewer->camera_binning_y, binning_available);

	g_signal_handler_unblock (viewer->pixel_format_combo, viewer->pixel_format_changed);

	g_signal_connect (arv_camera_get_device (viewer->camera), "control-lost", G_CALLBACK (control_lost_cb), viewer);

	return TRUE;
}

static void
camera_selection_changed_cb (GtkTreeSelection *selection, ArvViewer *viewer)
{	_entry:;
	GtkTreeIter iter;
	GtkTreeModel *tree_model;
	char *camera_id = NULL;

	if (gtk_tree_selection_get_selected (selection, &tree_model, &iter)) {
		gtk_tree_model_get (tree_model, &iter, 0, &camera_id, -1);
		start_camera (viewer, camera_id);
		g_free (camera_id);
	} else
		stop_camera (viewer);
}

static void
select_mode (ArvViewer *viewer, ArvViewerMode mode)
{	_entry:;
	gboolean video_visibility;
	char *subtitle;
	gint width, height, x, y;

	//if (!ARV_IS_CAMERA (viewer->camera))
	//	mode = ARV_VIEWER_MODE_CAMERA_LIST;

	switch (mode) {
		case ARV_VIEWER_MODE_CAMERA_LIST:
			video_visibility = FALSE;
			gtk_stack_set_visible_child (GTK_STACK (viewer->main_stack), viewer->camera_box);
			gtk_header_bar_set_title (GTK_HEADER_BAR (viewer->main_headerbar), "Aravis Viewer");
			gtk_header_bar_set_subtitle (GTK_HEADER_BAR (viewer->main_headerbar), NULL);
			stop_video (viewer);
			break;
		case ARV_VIEWER_MODE_VIDEO:
			start_video (viewer);
			if (!viewer->camera)
				break;
			video_visibility = TRUE;
			arv_camera_get_region (viewer->camera, &x, &y, &width, &height, NULL);
			subtitle = g_strdup_printf ("%s %dx%d@%d,%d %s",
						    arv_camera_get_model_name (viewer->camera, NULL),
						    width, height,
						    x, y,
						    arv_camera_get_pixel_format_as_string (viewer->camera, NULL));
			gtk_stack_set_visible_child (GTK_STACK (viewer->main_stack), viewer->video_box);
			gtk_header_bar_set_title (GTK_HEADER_BAR (viewer->main_headerbar), viewer->camera_name);
			gtk_header_bar_set_subtitle (GTK_HEADER_BAR (viewer->main_headerbar), subtitle);
			g_free (subtitle);
			break;
		default:
			g_assert_not_reached ();
			break;
	}

	/*gtk_widget_set_visible (viewer->back_button, video_visibility);
	gtk_widget_set_visible (viewer->rotate_cw_button, video_visibility);
	gtk_widget_set_visible (viewer->flip_vertical_toggle, video_visibility);
	gtk_widget_set_visible (viewer->flip_horizontal_toggle, video_visibility);
	gtk_widget_set_visible (viewer->snapshot_button, video_visibility);
	gtk_widget_set_visible (viewer->acquisition_button, video_visibility);*/
	gtk_widget_set_visible (viewer->main_headerbar, video_visibility);
}

void
switch_to_camera_list_cb (GtkToolButton *button, ArvViewer *viewer)
{	_entry:;
	select_mode (viewer, ARV_VIEWER_MODE_CAMERA_LIST);
}

void
switch_to_video_mode_cb (GtkToolButton *button, ArvViewer *viewer)
{	_entry:;
	select_mode (viewer, ARV_VIEWER_MODE_VIDEO);
}
#endif

void
arv_viewer_quit_cb (GtkApplicationWindow *window, ArvViewer *viewer)
{	_entry:;
	alarm(5);
#if ORIG
	stop_camera (viewer);
#else
	stop_video(viewer);
#endif
	g_application_quit (G_APPLICATION (viewer)); // stops g_application_run
}

static void
video_frame_realize_cb (GtkWidget * widget, ArvViewer *viewer)
{	_entry:;
#ifdef GDK_WINDOWING_X11
	viewer->video_window_xid = GDK_WINDOW_XID (gtk_widget_get_window (widget));
#endif
#ifdef GDK_WINDOWING_WIN32
	viewer->video_window_xid = (guintptr) GDK_WINDOW_HWND (gtk_widget_get_window (video_window));
#endif
}

void rec_switch(GtkToggleButton *toggle, ArvViewer *ctx)
{
	if (gtk_toggle_button_get_active(toggle)) 
		rec_start(ctx);
	else
		rec_stop(ctx);
}

static void
activate (GApplication *application)
{	_entry:;
	trl();
	ArvViewer *viewer = (ArvViewer *) application;
	g_autoptr (GtkBuilder) builder;
#if ORIG
	builder = gtk_builder_new_from_resource ("/org/aravis/viewer/arv-viewer.ui");
#else
	builder = gtk_builder_new_from_resource ("/org/argos/viewer/argos.ui");
	g_signal_connect(GTK_WIDGET (gtk_builder_get_object (builder, "rec")),
			 "clicked", G_CALLBACK(rec_switch), viewer);
#endif

	viewer->main_window = GTK_WIDGET (gtk_builder_get_object (builder, "main_window"));
	viewer->main_stack = GTK_WIDGET (gtk_builder_get_object (builder, "main_stack"));
#ifdef ORIG
	viewer->main_headerbar = GTK_WIDGET (gtk_builder_get_object (builder, "main_headerbar"));
	viewer->camera_box = GTK_WIDGET (gtk_builder_get_object (builder, "camera_box"));
	viewer->refresh_button = GTK_WIDGET (gtk_builder_get_object (builder, "refresh_button"));
	viewer->video_mode_button = GTK_WIDGET (gtk_builder_get_object (builder, "video_mode_button"));
	viewer->back_button = GTK_WIDGET (gtk_builder_get_object (builder, "back_button"));
	viewer->snapshot_button = GTK_WIDGET (gtk_builder_get_object (builder, "snapshot_button"));
	viewer->camera_tree = GTK_WIDGET (gtk_builder_get_object (builder, "camera_tree"));
	viewer->camera_parameters = GTK_WIDGET (gtk_builder_get_object (builder, "camera_parameters"));
	viewer->pixel_format_combo = GTK_WIDGET (gtk_builder_get_object (builder, "pixel_format_combo"));
	viewer->camera_x = GTK_WIDGET (gtk_builder_get_object (builder, "camera_x"));
	viewer->camera_y = GTK_WIDGET (gtk_builder_get_object (builder, "camera_y"));
	viewer->camera_binning_x = GTK_WIDGET (gtk_builder_get_object (builder, "camera_binning_x"));
	viewer->camera_binning_y = GTK_WIDGET (gtk_builder_get_object (builder, "camera_binning_y"));
	viewer->camera_width = GTK_WIDGET (gtk_builder_get_object (builder, "camera_width"));
	viewer->camera_height = GTK_WIDGET (gtk_builder_get_object (builder, "camera_height"));
	viewer->video_box = GTK_WIDGET (gtk_builder_get_object (builder, "video_box"));
	viewer->video_frame = GTK_WIDGET (gtk_builder_get_object (builder, "video_frame"));
	viewer->fps_label = GTK_WIDGET (gtk_builder_get_object (builder, "fps_label"));
	viewer->image_label = GTK_WIDGET (gtk_builder_get_object (builder, "image_label"));
	viewer->trigger_combo_box = GTK_WIDGET (gtk_builder_get_object (builder, "trigger_combobox"));
#endif
	viewer->frame_rate_entry = GTK_WIDGET (gtk_builder_get_object (builder, "frame_rate_entry"));
	viewer->exposure_spin_button = GTK_WIDGET (gtk_builder_get_object (builder, "exposure_spinbutton"));
	viewer->gain_spin_button = GTK_WIDGET (gtk_builder_get_object (builder, "gain_spinbutton"));
	viewer->exposure_hscale = GTK_WIDGET (gtk_builder_get_object (builder, "exposure_hscale"));
	viewer->gain_hscale = GTK_WIDGET (gtk_builder_get_object (builder, "gain_hscale"));
	viewer->auto_exposure_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "auto_exposure_togglebutton"));
	viewer->auto_gain_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "auto_gain_togglebutton"));
#ifdef ORIG
	viewer->rotate_cw_button = GTK_WIDGET (gtk_builder_get_object (builder, "rotate_cw_button"));
	viewer->flip_vertical_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "flip_vertical_togglebutton"));
	viewer->flip_horizontal_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "flip_horizontal_togglebutton"));
	viewer->acquisition_button = GTK_WIDGET (gtk_builder_get_object (builder, "acquisition_button"));

	gtk_widget_set_no_show_all (viewer->trigger_combo_box, TRUE);
#endif

	gtk_widget_show_all (viewer->main_window);

	gtk_application_add_window (GTK_APPLICATION (application), GTK_WINDOW (viewer->main_window));
	g_signal_connect (viewer->main_window, "destroy", G_CALLBACK (arv_viewer_quit_cb), viewer);
#if ORIG
	g_signal_connect (viewer->refresh_button, "clicked", G_CALLBACK (update_device_list_cb), viewer);
	g_signal_connect (viewer->video_mode_button, "clicked", G_CALLBACK (switch_to_video_mode_cb), viewer);
	g_signal_connect (viewer->back_button, "clicked", G_CALLBACK (switch_to_camera_list_cb), viewer);
	g_signal_connect (viewer->snapshot_button, "clicked", G_CALLBACK (snapshot_cb), viewer);
	g_signal_connect (viewer->rotate_cw_button, "clicked", G_CALLBACK (rotate_cw_cb), viewer);
	g_signal_connect (viewer->flip_horizontal_toggle, "clicked", G_CALLBACK (flip_horizontal_cb), viewer);
	g_signal_connect (viewer->flip_vertical_toggle, "clicked", G_CALLBACK (flip_vertical_cb), viewer);
#endif
	g_signal_connect (viewer->frame_rate_entry, "activate", G_CALLBACK (frame_rate_entry_cb), viewer);
	g_signal_connect (viewer->frame_rate_entry, "focus-out-event", G_CALLBACK (frame_rate_entry_focus_cb), viewer);

#if ORIG
	if (!has_gtksink && !has_gtkglsink) {
		g_signal_connect (viewer->video_frame, "realize", G_CALLBACK (video_frame_realize_cb), viewer);
	}
	viewer->camera_selected = g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (viewer->camera_tree)), "changed",
						    G_CALLBACK (camera_selection_changed_cb), viewer);
#endif
	viewer->exposure_spin_changed = g_signal_connect (viewer->exposure_spin_button, "value-changed",
							  G_CALLBACK (exposure_spin_cb), viewer);
	viewer->gain_spin_changed = g_signal_connect (viewer->gain_spin_button, "value-changed",
						      G_CALLBACK (gain_spin_cb), viewer);
	viewer->exposure_hscale_changed = g_signal_connect (viewer->exposure_hscale, "value-changed",
							    G_CALLBACK (exposure_scale_cb), viewer);
	viewer->gain_hscale_changed = g_signal_connect (viewer->gain_hscale, "value-changed",
							G_CALLBACK (gain_scale_cb), viewer);
	viewer->auto_exposure_clicked = g_signal_connect (viewer->auto_exposure_toggle, "clicked",
							  G_CALLBACK (auto_exposure_cb), viewer);
	viewer->auto_gain_clicked = g_signal_connect (viewer->auto_gain_toggle, "clicked",
						      G_CALLBACK (auto_gain_cb), viewer);
#if ORIG
	viewer->pixel_format_changed = g_signal_connect (viewer->pixel_format_combo, "changed",
							 G_CALLBACK (pixel_format_combo_cb), viewer);
	viewer->camera_x_changed = g_signal_connect (viewer->camera_x, "value-changed",
						     G_CALLBACK (camera_region_cb), viewer);
	viewer->camera_y_changed = g_signal_connect (viewer->camera_y, "value-changed",
						     G_CALLBACK (camera_region_cb), viewer);
	viewer->camera_width_changed = g_signal_connect (viewer->camera_width, "value-changed",
							 G_CALLBACK (camera_region_cb), viewer);
	viewer->camera_height_changed = g_signal_connect (viewer->camera_height, "value-changed",
							  G_CALLBACK (camera_region_cb), viewer);
	viewer->camera_binning_x_changed = g_signal_connect (viewer->camera_binning_x, "value-changed",
							     G_CALLBACK (camera_binning_cb), viewer);
	viewer->camera_binning_y_changed = g_signal_connect (viewer->camera_binning_y, "value-changed",
							     G_CALLBACK (camera_binning_cb), viewer);

	gtk_widget_set_sensitive (viewer->camera_parameters, FALSE);
	select_mode (viewer, ARV_VIEWER_MODE_CAMERA_LIST);
	update_device_list_cb (GTK_TOOL_BUTTON (viewer->refresh_button), viewer);
#endif

#if !ORIG
	//start_camera(viewer, NULL);
	//select_mode(viewer, ARV_VIEWER_MODE_VIDEO); //calls start_video
	make_video(viewer);
	start_video2(viewer);
#endif
	viewer->builder = builder;
	builder = NULL;
}

static void
startup (GApplication *application)
{	_entry:;
	arv_enable_interface ("Fake");

	G_APPLICATION_CLASS (arv_viewer_parent_class)->startup (application);
}

static void
shutdown (GApplication *application)
{	_entry:;
	trl();
	G_APPLICATION_CLASS (arv_viewer_parent_class)->shutdown (application);

	arv_shutdown ();
}

static void
finalize (GObject *object)
{	_entry:;
	trl();
	ArvViewer *viewer = (ArvViewer *) object;
//#if !ORIG
//	stop_video(viewer);
//#endif
	G_OBJECT_CLASS (arv_viewer_parent_class)->finalize (object);

	g_clear_object (&viewer->notification);
}

ArvViewer *arv_viewer;

static void set_cancel (int s)
{	_entry:;
	signal(SIGINT, 0);
  	signal(SIGALRM, 0);
	//arv_viewer_quit_cb(0, arv_viewer);
	g_signal_emit_by_name(arv_viewer->main_window, "destroy"); // calls arv_viewer_quit_cb
}

ArvViewer *
arv_viewer_new (void)
{	_entry:;

#if ORIG
  if (!gstreamer_plugin_check ())
	  return NULL;
  g_set_application_name ("ArvViewer");
#else
  g_set_application_name ("Argos");
#endif
  arv_viewer = g_object_new (arv_viewer_get_type (),
			     "application-id", "org.aravis.ArvViewer",
			     "flags", G_APPLICATION_NON_UNIQUE,
			     "inactivity-timeout", 30000,
			     NULL);
  signal (SIGINT, set_cancel);
#if test_limit
  alarm(test_limit);
  signal (SIGALRM, set_cancel);
#endif


  return arv_viewer;
}


static void
arv_viewer_init (ArvViewer *viewer)
{	_entry:;
	viewer->notification = notify_notification_new (NULL, NULL, NULL);
	viewer->auto_socket_buffer = FALSE;
	viewer->packet_resend = TRUE;
	viewer->packet_timeout = 20;
	viewer->frame_retention = 100;
	viewer->cache_policy = ARV_REGISTER_CACHE_POLICY_DEFAULT;
}

static void
arv_viewer_class_init (ArvViewerClass *class)
{	_entry:;
	trl();
  GApplicationClass *application_class = G_APPLICATION_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = finalize;

  application_class->startup = startup;
  application_class->shutdown = shutdown;
  application_class->activate = activate;
}
