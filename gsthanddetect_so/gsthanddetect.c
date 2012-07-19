/*
 * GStreamer hand gesture detect plugins
 * Copyright (C) 2012 Andol Li <<andol@andol.info>>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-handdetect
 *
 * FIXME:operates hand gesture detection in video streams and images, and enable media operation e.g. play/stop/fast forward/back rewind.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch autovideosrc ! ffmpegcolorspace ! "video/x-raw-rgb, width=320, height=240" ! \
   videoscale ! handdetect ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsthanddetect.h"
/* for debug */
#include "debug.h"

GST_DEBUG_CATEGORY_STATIC (gst_handdetect_debug);
#define GST_CAT_DEFAULT gst_handdetect_debug

/* define the dir of haar file for hand detect */
#define HAAR_FILE "/usr/local/share/opencv/haarcascades/fist.xml"

/* Filter signals and args */
enum
{
	/* FILL ME */
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_DISPLAY,
	PROP_PROFILE
};

/* the capabilities of the inputs and outputs.
 *
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
	GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

GST_BOILERPLATE (Gsthanddetect, gst_handdetect, GstElement, GST_TYPE_ELEMENT);

static void gst_handdetect_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_handdetect_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_handdetect_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_handdetect_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_handdetect_sink_handler(GstPad *pad, GstEvent *event);
static gboolean gst_handdetect_src_handler(GstPad *pad, GstEvent *event);

static void gst_handdetect_load_profile (Gsthanddetect * filter);

/* clean opencv images and parameters */
static void
gst_handdetect_finalise(GObject *obj)
{
	Gsthanddetect *filter = GST_HANDDETECT (obj);

	if (filter->cvImage) {
	    cvReleaseImage (&filter->cvImage);
	  }
	if(filter->cvGray) {
	    cvReleaseImage (&filter->cvGray);
	}

	g_free (filter->profile);
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* GObject vmethod implementations */
static void
gst_handdetect_base_init (gpointer gclass)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

	gst_element_class_set_details_simple(element_class,
			"handdetect",
			"Filter/Effect/Video",
			"Performs hand detection on videos and images, providing detected positions via bus messages, and use the messages for media operation",
			"andol li <<andol@andol.info>>");

	gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&src_factory));
	gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_factory));
}

/* initialise the HANDDETECT class */
static void
gst_handdetect_class_init (GsthanddetectClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	parent_class = g_type_class_peek_parent (klass);

	gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_handdetect_finalise);
	gobject_class->set_property = gst_handdetect_set_property;
	gobject_class->get_property = gst_handdetect_get_property;

	g_object_class_install_property (
			gobject_class,
			PROP_DISPLAY,
			g_param_spec_boolean (
				  "display",
				  "Display",
				  "Sets whether the detected hands should be highlighted in the output",
				  TRUE,
				  G_PARAM_READWRITE)
			);

	g_object_class_install_property (
			gobject_class,
			PROP_PROFILE,
			g_param_spec_string (
					"profile",
					"Profile",
					"Location of Haar cascade file to use for hand detection",
					HAAR_FILE,
					G_PARAM_READWRITE)
			);
}

/* initialise the new element
 * instantiate pads and add them to element
 * set pad call-back functions
 * initialise instance structure
 */
static void
gst_handdetect_init (Gsthanddetect * filter, GsthanddetectClass * gclass)
{
	  g_print("---plugin init OK\n");
	  g_print("---%s\n", HAAR_FILE);

	  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
	  gst_pad_set_setcaps_function (
			  filter->sinkpad,
			  GST_DEBUG_FUNCPTR(gst_handdetect_set_caps));
	  gst_pad_set_getcaps_function (
			  filter->sinkpad,
			  GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
	  gst_pad_set_chain_function (
			  filter->sinkpad,
			  GST_DEBUG_FUNCPTR(gst_handdetect_chain));
	  gst_pad_set_event_function(
			  filter->sinkpad,
			  GST_DEBUG_FUNCPTR(gst_handdetect_sink_handler));

	  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
	  gst_pad_set_getcaps_function (
			  filter->srcpad,
			  GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
	  gst_pad_set_event_function(
			  filter->srcpad,
			  GST_DEBUG_FUNCPTR(gst_handdetect_src_handler));

	  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
	  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

	  filter->profile = g_strdup(HAAR_FILE);
	  filter->display = TRUE;
	  gst_handdetect_load_profile (filter);
}

static void
gst_handdetect_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
	  Gsthanddetect *filter = GST_HANDDETECT (object);

	  switch (prop_id) {
		  case PROP_PROFILE:
			g_free (filter->profile);
			filter->profile = g_value_dup_string (value);
			gst_handdetect_load_profile (filter);
			break;
		  case PROP_DISPLAY:
			filter->display = g_value_get_boolean (value);
			break;
		  default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	  }
}

static void
gst_handdetect_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	  Gsthanddetect *filter = GST_HANDDETECT (object);

	  switch (prop_id) {
		  case PROP_PROFILE:
			g_value_set_string (value, filter->profile);
			break;
		  case PROP_DISPLAY:
			g_value_set_boolean (value, filter->display);
			break;
		  default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	  }
}

/* GstElement vmethod implementations */
/* this function handles the link with other elements */
static gboolean
gst_handdetect_set_caps (GstPad * pad, GstCaps * caps)
{
	  Gsthanddetect *filter;
	  GstPad *otherpad;
	  gint width, height;
	  GstStructure *structure;

	  filter = GST_HANDDETECT (gst_pad_get_parent (pad));
	  structure = gst_caps_get_structure(caps, 0);
	  gst_structure_get_int(structure, "width", &width);
	  gst_structure_get_int(structure, "height", &height);

	  filter->cvImage = cvCreateImage (cvSize(320, 240), IPL_DEPTH_8U, 3);
	  filter->cvGray = cvCreateImage (cvSize(320, 240), IPL_DEPTH_8U, 1);
	  filter->cvStorage = cvCreateMemStorage (0);

	  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
	  gst_object_unref (filter);

	  return gst_pad_set_caps (otherpad, caps);
}

/* chain function
 * this function does the actual processing 'of hand detect and display'
 */
static GstFlowReturn
gst_handdetect_chain (GstPad * pad, GstBuffer * buf)
{
	  Gsthanddetect *filter;
	  CvSeq *hands;
	  CvRect *r;
	  GstStructure *s;
	  GstMessage *m;
	  int i;

	  filter = GST_HANDDETECT (GST_OBJECT_PARENT (pad));
	  /* get image data from gst buffer
	   * have not tested if this data is captured correctly,
	   * as the handdetect becomes much worse than that in opencv_handdetect project.
	   * */
	  filter->cvImage->imageData = (char *) GST_BUFFER_DATA (buf);
	  /* filter->cvImage = cvLoadImage("/home/javauser/workspace/pic.jpg", 0); */
	  /* debug print*/
	  /* g_print("---filter->cvImage->imageData OK\n"); */
	  cvCvtColor(filter->cvImage, filter->cvGray, CV_RGB2GRAY);
	  cvClearMemStorage (filter->cvStorage);

	  if(filter->cvCascade){
		  /* detect hands */
		  hands = cvHaarDetectObjects (
				  filter->cvImage,
				  filter->cvCascade,
				  filter->cvStorage,
				  1.1,
				  2,
				  CV_HAAR_DO_CANNY_PRUNING,
				  cvSize(24,24) /* haar training picture size 24x24 */
				  #if (CV_MAJOR_VERSION >= 2) && (CV_MINOR_VERSION >= 2)
				  , cvSize(0, 0)
				  #endif
				  );

		  /* if hands detected, set the buffer writable */
		  if(filter->display && hands && hands->total > 0){
			  buf = gst_buffer_make_writable(buf);
			  /* debug print */
			  g_print("---%d hands detected\n", (int)hands->total);
		  }

		  /* go through all hands detected to get the best hand
		   * prev_r => previous hand
		   * best_r => best hand in this frame
		   */
		  if(hands && hands->total > 0){
			  /* init distance for comparisons, 400 is the maximum distance in 320x240 frame*/
			  int min_distance = 400;
			  /* init filter->prev_r */
			  CvRect temp_r = cvRect(0,0,0,0);
			  if(filter->prev_r == NULL) filter->prev_r = &temp_r;

			  /* get the best hand */
			  for(i = 0; i< (hands ? hands->total : 0); i++){
				  r = (CvRect *) cvGetSeqElem(hands, i);
				  int distance = (int) sqrt(pow((r->x - filter->prev_r->x), 2) + pow((r->y - filter->prev_r->y), 2));
				  if(distance <= min_distance){
					  min_distance = distance;
					  filter->best_r = r;
				  }
			  }

			  /* save best_r as prev_r for next frame */
			  filter->prev_r = (CvRect *)filter->best_r;

			  /* processing the best hand in the frame
			   * best_r => the best hand
			   */
			  /* debug function - defined in debug.h*/
			  Debug_Printf_FrameInfos();
			  /* define the structure for message post */
			  s = gst_structure_new(
					  "detected_hand_info",
					  "gesture", G_TYPE_CHAR, "fist",
					  "x", G_TYPE_UINT, (uint)(filter->best_r->x + filter->best_r->width * 0.5),
					  "y", G_TYPE_UINT, (uint)(filter->best_r->y + filter->best_r->height * 0.5),
					  "width", G_TYPE_UINT, (uint)filter->best_r->width,
					  "height", G_TYPE_UINT, (uint)filter->best_r->height,
					  NULL);
			  /* init message element */
			  m = gst_message_new_element(GST_OBJECT(filter), s);
			  /* send the message to GstBus */
			  gst_element_post_message(GST_ELEMENT(filter), m);

			  /* check the filter->display,
			   * if TRUE then display the circle marker in the frame
			   */
			  if(filter->display){
				  CvPoint center;
				  int radius;
				  center.x = cvRound((filter->best_r->x + filter->best_r->width * 0.5));
				  center.y = cvRound((filter->best_r->y + filter->best_r->height * 0.5));
				  radius = cvRound((filter->best_r->width + filter->best_r->height) * 0.25);
				  cvCircle(filter->cvImage, center, radius, CV_RGB(0, 0, 200), 1, 8, 0);
			  }
		  }
	  }

	  /*
	   * just push out the incoming buffer without touching it
	   */
	  return gst_pad_push (filter->srcpad, buf);
}

static void
gst_handdetect_load_profile(Gsthanddetect *filter){
	filter->cvCascade = (CvHaarClassifierCascade *) cvLoad(filter->profile, 0, 0, 0);
	if(! filter->cvCascade) GST_WARNING("could not load haar classifier cascade: %s.", filter->profile);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
handdetect_init (GstPlugin * handdetect)
{
	  /* debug category for filtering log messages
	   *
	   * exchange the string 'Template handdetect' with your description
	   */
	  GST_DEBUG_CATEGORY_INIT (
			  gst_handdetect_debug,
			  "handdetect",
			  0,
			  "performs hand detect on videos and images, providing detected positions via bus messages for media operation.");

	  return gst_element_register (
			  handdetect,
			  "handdetect",
			  GST_RANK_NONE,
			  GST_TYPE_HANDDETECT);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gst_handdetect"
#endif

/* gstreamer looks for this structure to register handdetects
 *
 * exchange the string 'Template handdetect' with your handdetect description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "handdetect",
    "Template handdetect",
    handdetect_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)

static gboolean
gst_handdetect_sink_handler(GstPad *pad, GstEvent *event)
{
	Gsthanddetect *filter;
	gboolean ret;

	filter = GST_HANDDETECT(gst_pad_get_parent (pad));

	switch (GST_EVENT_TYPE (event)) {
	case GST_EVENT_NEWSEGMENT:
	  ret = gst_pad_push_event (filter->srcpad, event);
	  break;
	case GST_EVENT_EOS:
//	  gst_handdetect_stop_processing (filter);
	  ret = gst_pad_push_event (filter->srcpad, event);
	  break;
	case GST_EVENT_FLUSH_STOP:
//	  gst_handdetect_clear_temporary_buffers (filter);
	  ret = gst_pad_push_event (filter->srcpad, event);
	  break;
	default:
	  ret = gst_pad_event_default (pad, event);
	  break;
	  }

	  gst_object_unref (filter);
	  return ret;
}

static gboolean
gst_handdetect_src_handler(GstPad *pad, GstEvent *event)
{
	return TRUE;
}
