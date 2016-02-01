/* GStreamer
 * Copyright (C) 2013 Collabora Ltd.
 *   @author Torrie Fischer <torrie.fischer@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include <stdlib.h>
#include "test.h"

/*
 *
 *             .-------.                                            .----------.
 *  RTCP       |udpsrc |                                            |          |                                   .-------.
 *  port=5005  |     src------------------------------------------>recv_rtcp_0 |                                   |udpsink|   RTCP port 5010
 *             '-------'                                            |   send_rtcp_1------------------------------>sink     |
 *                                                                  |          |                                   '-------'
 *             .-------.    .------------.    .------------.        |          |               .-----------.   .---------.   .-------------.
 *  RTP        |udpsrc |    | mprtp_rcv  |    | mprtp_ply  |        | rtpbin   |               |theoradepay|   |theoradec|   |autovideosink|
 *  port=5000  |      src->sink_0 mprtp_src->mprtp_sink mprtp_src->rtp_recv_rtp_0 recv_rtp_0->sink       src->sink     src->sink           |
 *             '-------'    |            |    |            |        '----------'               '-----------'   '---------'   '-------------'
 *                          | mprtcp_sr_src->mprtcp_sr_sink|
 *             .-------.    |            |    '------------'
 *  RTP        |udpsrc |    |            |
 *  port=5001  |      src->sink_1        |
 *             '-------'    |            |
 *                          |            |
 *                          | mprtcp_rr_src-> MPRTCP RR,XR reports blocks
 *                          '------------'
 *
 *
 */


GMainLoop *loop = NULL;

typedef struct _SessionData
{
  int ref;
  GstElement *rtpbin;
  guint sessionNum;
  GstCaps *caps;
  GstElement *output;
} SessionData;

static SessionData *
session_ref (SessionData * data)
{
  g_atomic_int_inc (&data->ref);
  return data;
}

static void
session_unref (gpointer data)
{
  SessionData *session = (SessionData *) data;
  if (g_atomic_int_dec_and_test (&session->ref)) {
    g_object_unref (session->rtpbin);
    gst_caps_unref (session->caps);
    g_free (session);
  }
}

static SessionData *
session_new (guint sessionNum)
{
  SessionData *ret = g_new0 (SessionData, 1);
  ret->sessionNum = sessionNum;
  return session_ref (ret);
}

static void
setup_ghost_sink (GstElement * sink, GstBin * bin)
{
  GstPad *sinkPad = gst_element_get_static_pad (sink, "sink");
  GstPad *binPad = gst_ghost_pad_new ("sink", sinkPad);
  gst_element_add_pad (GST_ELEMENT (bin), binPad);
}

static SessionData *
make_video_session (guint sessionNum)
{
  SessionData *ret = session_new (sessionNum);
  GstBin *bin = GST_BIN (gst_bin_new ("video"));
  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GstElement *depayloader = gst_element_factory_make ("rtpvp8depay", NULL);
  GstElement *decoder = gst_element_factory_make ("vp8dec", NULL);

  GstElement *converter = gst_element_factory_make ("videoconvert", NULL);
  GstElement *sink = gst_element_factory_make ("autovideosink", NULL);

  gst_bin_add_many (bin, depayloader, decoder, converter, queue, sink, NULL);
  gst_element_link_many (queue, depayloader, decoder, converter, sink, NULL);

  setup_ghost_sink (queue, bin);

  ret->output = GST_ELEMENT (bin);

  ret->caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "video",
      "clock-rate", G_TYPE_INT, 90000,
      "width", G_TYPE_INT, 352,
      "height", G_TYPE_INT, 288,
      "framerate", GST_TYPE_FRACTION, 25, 1,
      //"encoding-name", G_TYPE_STRING, "THEORA", NULL
      "encoding-name", G_TYPE_STRING, "VP8", NULL
      );

  g_object_set (sink, "sync", FALSE, NULL);
  return ret;
}

static GstCaps *
request_pt_map (GstElement * rtpbin, guint session, guint pt,
    gpointer user_data)
{
  SessionData *data = (SessionData *) user_data;
  g_print ("Looking for caps for pt %u in session %u, have %u\n", pt, session,
      data->sessionNum);
  if (session == data->sessionNum) {
    g_print ("Returning %s\n", gst_caps_to_string (data->caps));
    return gst_caps_ref (data->caps);
  }
  return NULL;
}

static void
cb_eos (GstBus * bus, GstMessage * message, gpointer data)
{
  g_print ("Got EOS\n");
  g_main_loop_quit (loop);
}

static void
cb_state (GstBus * bus, GstMessage * message, gpointer data)
{
  GstObject *pipe = GST_OBJECT (data);
  GstState old, new, pending;
  gst_message_parse_state_changed (message, &old, &new, &pending);
  if (message->src == pipe) {
    g_print ("Pipeline %s changed state from %s to %s\n",
        GST_OBJECT_NAME (message->src),
        gst_element_state_get_name (old), gst_element_state_get_name (new));
  }
}

static void
cb_warning (GstBus * bus, GstMessage * message, gpointer data)
{
  GError *error = NULL;
  gst_message_parse_warning (message, &error, NULL);
  g_printerr ("Got warning from %s: %s\n", GST_OBJECT_NAME (message->src),
      error->message);
  g_error_free (error);
}

static void
cb_error (GstBus * bus, GstMessage * message, gpointer data)
{
  GError *error = NULL;
  gst_message_parse_error (message, &error, NULL);
  g_printerr ("Got error from %s: %s\n", GST_OBJECT_NAME (message->src),
      error->message);
  g_error_free (error);
  g_main_loop_quit (loop);
}

static void
handle_new_stream (GstElement * element, GstPad * newPad, gpointer data)
{
  SessionData *session = (SessionData *) data;
  gchar *padName;
  gchar *myPrefix;

  padName = gst_pad_get_name (newPad);
  myPrefix = g_strdup_printf ("recv_rtp_src_%u", session->sessionNum);

  g_print ("New pad: %s, looking for %s_*\n", padName, myPrefix);

  if (g_str_has_prefix (padName, myPrefix)) {
    GstPad *outputSinkPad;
    GstElement *parent;

    parent = GST_ELEMENT (gst_element_get_parent (session->rtpbin));
    gst_bin_add (GST_BIN (parent), session->output);
    gst_element_sync_state_with_parent (session->output);
    gst_object_unref (parent);

    outputSinkPad = gst_element_get_static_pad (session->output, "sink");
    g_assert_cmpint (gst_pad_link (newPad, outputSinkPad), ==, GST_PAD_LINK_OK);
    gst_object_unref (outputSinkPad);

    g_print ("Linked!\n");
  }
  g_free (myPrefix);
  g_free (padName);
}

static GstElement *
request_aux_receiver (GstElement * rtpbin, guint sessid, SessionData * session)
{
  GstElement *rtx, *bin;
  GstPad *pad;
  gchar *name;
  GstStructure *pt_map;

  GST_INFO ("creating AUX receiver");
  bin = gst_bin_new (NULL);
  rtx = gst_element_factory_make ("rtprtxreceive", NULL);
  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, 99, NULL);
  g_object_set (rtx, "payload-type-map", pt_map, NULL);
  gst_structure_free (pt_map);
  gst_bin_add (GST_BIN (bin), rtx);

  pad = gst_element_get_static_pad (rtx, "src");
  name = g_strdup_printf ("src_%u", sessid);
  gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (rtx, "sink");
  name = g_strdup_printf ("sink_%u", sessid);
  gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);

  return bin;
}


static void
join_session (GstElement * pipeline, GstElement * rtpBin, SessionData * session,
    guint32 clockrate)
{
  GstElement *rtpSrc_1, *rtpSrc_2, *rtpSrc_3;
  GstElement *async_tx_rtcpSrc_1, *async_tx_rtcpSrc_2, *async_tx_rtcpSrc_3;
  GstElement *rtcpSrc;
  GstElement *rtcpSink, *async_rx_rtcpSink_1, *async_rx_rtcpSink_2, *async_rx_rtcpSink_3;
  GstElement *mprtprcv, *mprtpsnd, *mprtpply;
  gchar *padName, *padname2;
  guint basePort;

  g_print ("Joining session %p\n", session);

  session->rtpbin = g_object_ref (rtpBin);

  basePort = 5000 + (session->sessionNum * 20);

  rtpSrc_1 = gst_element_factory_make ("udpsrc", NULL);
  rtpSrc_2 = gst_element_factory_make ("udpsrc", NULL);
  rtpSrc_3 = gst_element_factory_make ("udpsrc", NULL);
  async_tx_rtcpSrc_1 = gst_element_factory_make ("udpsrc", NULL);
  async_tx_rtcpSrc_2 = gst_element_factory_make ("udpsrc", NULL);
  async_tx_rtcpSrc_3 = gst_element_factory_make ("udpsrc", NULL);
  rtcpSrc = gst_element_factory_make ("udpsrc", NULL);
  rtcpSink = gst_element_factory_make ("udpsink", NULL);
  async_rx_rtcpSink_1 = gst_element_factory_make ("udpsink", NULL);
  async_rx_rtcpSink_2 = gst_element_factory_make ("udpsink", NULL);
  async_rx_rtcpSink_3 = gst_element_factory_make ("udpsink", NULL);
  mprtprcv = gst_element_factory_make ("mprtpreceiver", NULL);
  mprtpply = gst_element_factory_make ("mprtpplayouter", NULL);
  mprtpsnd = gst_element_factory_make ("mprtpsender", NULL);

  g_object_set (rtpSrc_1, "port", path1_tx_rtp_port, "caps", session->caps, NULL);
  g_object_set (rtpSrc_2, "port", path2_tx_rtp_port, "caps", session->caps, NULL);
  g_object_set (rtpSrc_3, "port", path3_tx_rtp_port, "caps", session->caps, NULL);

  if(test_parameters_.test_directive == AUTO_RATE_AND_CC_CONTROLLING){
      g_object_set (async_rx_rtcpSink_1, "port", path1_rx_rtcp_port, "host", "10.0.0.1", "sync", FALSE, "async", FALSE, NULL);
      g_object_set (async_rx_rtcpSink_2, "port", path2_rx_rtcp_port, "host", "10.0.1.1", "sync", FALSE, "async", FALSE, NULL);
      g_object_set (async_rx_rtcpSink_3, "port", path3_rx_rtcp_port, "host", "10.0.2.1", "sync", FALSE, "async", FALSE, NULL);
      g_object_set (async_tx_rtcpSrc_1, "port",  path1_tx_rtcp_port, NULL);
      g_object_set (async_tx_rtcpSrc_2, "port",  path2_tx_rtcp_port, NULL);
      g_object_set (async_tx_rtcpSrc_3, "port",  path3_tx_rtcp_port, NULL);
  }

  g_object_set (rtcpSrc, "port", rtpbin_tx_rtcp_port, NULL);
  g_object_set (rtcpSink, "port", rtpbin_rx_rtcp_port, "host", "10.0.0.1",
//                NULL);
        "async", FALSE, NULL);

  g_object_set (mprtpply, "pivot-clock-rate", clockrate, NULL);

  if(test_parameters_.test_directive == AUTO_RATE_AND_CC_CONTROLLING)
    g_object_set (mprtpply, "auto-rate-and-cc", TRUE, NULL);

  if(test_parameters_.subflow1_active)
    g_object_set (mprtpply, "join-subflow", 1, NULL);
  if(test_parameters_.subflow2_active)
    g_object_set (mprtpply, "join-subflow", 2, NULL);
  if(test_parameters_.subflow3_active)
    g_object_set (mprtpply, "join-subflow", 3, NULL);

  if(0 && test_parameters_.video_session == FOREMAN_SOURCE)
    g_object_set(mprtpply, "delay-offset", 2 * GST_SECOND, NULL);

  g_print ("Connecting to %i/%i/%i/%i/%i/%i\n",
      basePort, basePort + 1, basePort + 5,
      basePort + 11, basePort + 12, basePort + 10);

  /* enable RFC4588 retransmission handling by setting rtprtxreceive
   * as the "aux" element of rtpbin */
  g_signal_connect (rtpBin, "request-aux-receiver",
      (GCallback) request_aux_receiver, session);

  gst_bin_add_many (GST_BIN (pipeline), rtpSrc_1, rtpSrc_2,
                    rtpSrc_3, rtcpSrc, rtcpSink,
      mprtpsnd, mprtprcv, mprtpply, NULL);

  if(test_parameters_.test_directive == AUTO_RATE_AND_CC_CONTROLLING){
      gst_bin_add_many (GST_BIN (pipeline),
                        async_rx_rtcpSink_1, async_rx_rtcpSink_2, async_rx_rtcpSink_3,
                        async_tx_rtcpSrc_1, async_tx_rtcpSrc_2, async_tx_rtcpSrc_3, NULL);
  }

  g_signal_connect_data (rtpBin, "pad-added", G_CALLBACK (handle_new_stream),
      session_ref (session), (GClosureNotify) session_unref, 0);

  g_signal_connect_data (rtpBin, "request-pt-map", G_CALLBACK (request_pt_map),
      session_ref (session), (GClosureNotify) session_unref, 0);

  gst_element_link_pads (rtpSrc_1, "src", mprtprcv, "sink_1");
  gst_element_link_pads (rtpSrc_2, "src", mprtprcv, "sink_2");
  gst_element_link_pads (rtpSrc_3, "src", mprtprcv, "sink_3");

  padName = g_strdup_printf ("recv_rtp_sink_%u", session->sessionNum);
  gst_element_link_pads (mprtprcv, "mprtp_src", mprtpply, "mprtp_sink");
  gst_element_link_pads (mprtpply, "mprtp_src", rtpBin, padName);
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", session->sessionNum);
  gst_element_link_pads (mprtprcv, "mprtcp_sr_src", mprtpply, "mprtcp_sr_sink");
  gst_element_link_pads (rtcpSrc, "src", rtpBin, padName);
  g_free (padName);

  gst_element_link_pads (mprtpply, "mprtcp_rr_src", mprtpsnd, "mprtcp_rr_sink");

  padName = g_strdup_printf ("send_rtcp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, rtcpSink, "sink");
  if(test_parameters_.test_directive == AUTO_RATE_AND_CC_CONTROLLING){
      gst_element_link_pads (async_tx_rtcpSrc_1, "src", mprtprcv, "async_sink_1");
      gst_element_link_pads (async_tx_rtcpSrc_2, "src", mprtprcv, "async_sink_2");
      gst_element_link_pads (async_tx_rtcpSrc_3, "src", mprtprcv, "async_sink_3");

      gst_element_link_pads (mprtpsnd, "async_src_1", async_rx_rtcpSink_1, "sink");
      gst_element_link_pads (mprtpsnd, "async_src_2", async_rx_rtcpSink_2, "sink");
      gst_element_link_pads (mprtpsnd, "async_src_3", async_rx_rtcpSink_3, "sink");
  }

  g_free (padName);

  session_unref (session);
}

int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  SessionData *videoSession;
  SessionData *audioSession;
  GstElement *rtpBin;
  GstBus *bus;

  GError *error = NULL;
  GOptionContext *context;

  context = g_option_context_new ("- test tree model performance");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, &error);
  if(info){
    _print_info();
    return 0;
  }
  _setup_test_params(profile);
  gst_init (NULL, NULL);

  loop = g_main_loop_new (NULL, FALSE);
  pipe = GST_PIPELINE (gst_pipeline_new (NULL));

  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::error", G_CALLBACK (cb_error), pipe);
  g_signal_connect (bus, "message::warning", G_CALLBACK (cb_warning), pipe);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  g_signal_connect (bus, "message::eos", G_CALLBACK (cb_eos), NULL);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  rtpBin = gst_element_factory_make ("rtpbin", NULL);
  gst_bin_add (GST_BIN (pipe), rtpBin);
  g_object_set (rtpBin, "latency", 200, "do-retransmission", TRUE,
      "rtp-profile", GST_RTP_PROFILE_AVPF, NULL);

  switch(test_parameters_.video_session){
    case TEST_SOURCE:
    default:
      videoSession = make_video_session (0);
    break;
  }


  join_session (GST_ELEMENT (pipe), rtpBin, videoSession, 90000);

  g_print ("starting client pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stoping client pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  return 0;
}
