/* GStreamer
 * Copyright (C) 2015 FIXME <fixme@example.com>
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

#ifndef _GST_RTPSTATMAKER_H_
#define _GST_RTPSTATMAKER_H_

#include <gst/gst.h>

#include "gstmprtcpbuffer.h"
#include "sndctrler.h"
#include "streamsplitter.h"
#include "mprtplogger.h"
#include "fecenc.h"
#include "mediator.h"

G_BEGIN_DECLS
#define GST_TYPE_RTPSTATMAKER   (gst_rtpstatmaker_get_type())
#define GST_RTPSTATMAKER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTPSTATMAKER,GstRTPStatMaker))
#define GST_RTPSTATMAKER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTPSTATMAKER,GstRTPStatMakerClass))
#define GST_IS_RTPSTATMAKER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTPSTATMAKER))
#define GST_IS_RTPSTATMAKER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTPSTATMAKER))

typedef struct _GstRTPStatMaker GstRTPStatMaker;
typedef struct _GstRTPStatMakerClass GstRTPStatMakerClass;
typedef struct _GstRTPStatMakerPrivate GstRTPStatMakerPrivate;


struct _GstRTPStatMaker
{
  GstElement                    base_object;
  GMutex                        mutex;
  GCond                         waiting_signal;
  GCond                         receiving_signal;
  GstPad*                       rtp_sinkpad;
  GstPad*                       mprtp_srcpad;
  GstPad*                       mprtcp_rr_sinkpad;
  GstPad*                       mprtcp_sr_srcpad;

  gboolean                      preroll;

  SndSubflows*                  subflows;
  SndPackets*                   sndpackets;
  StreamSplitter*               splitter;
  SndController*                controller;
  SndTracker*                   sndtracker;
  gboolean                      riport_flow_signal_sent;
  GstSegment                    segment;
  GstClockTime                  position_out;

  guint8                        fec_payload_type;
  GstClockTime                  obsolation_treshold;
  GstClock*                     sysclock;

  GstClockTime                  last_pts;

  guint32                       rtcp_sent_octet_sum;

  GstTask*                      thread;
  GRecMutex                     thread_mutex;
  FECEncoder*                   fec_encoder;
  guint32                       fec_interval;
  guint32                       sent_packets;

  Mediator*                     monitoring;
  GQueue*                       packetsq;
  Messenger*                    emit_msger;
//  GAsyncQueue*                  emitterq;
  Notifier*                     on_rtcp_ready;

  GstRTPStatMakerPrivate*     priv;


};

struct _GstRTPStatMakerClass
{
  GstElementClass base_class;

  void  (*mprtp_media_rate_utilization) (GstElement *,gpointer);
};

GType gst_rtpstatmaker_get_type (void);



G_END_DECLS
#endif //_GST_RTPSTATMAKER_H_