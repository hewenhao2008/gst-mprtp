/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "sndsubflows.h"


GST_DEBUG_CATEGORY_STATIC (sndsubflows_debug_category);
#define GST_CAT_DEFAULT sndsubflows_debug_category

G_DEFINE_TYPE (SndSubflows, sndsubflows, G_TYPE_OBJECT);

#define CHANGE_SUBFLOW_PROPERTY_VALUE(subflows_list, subflow_id, property, property_value, changed_subflows) \
{                                                     \
  GSList* it;                                         \
  if(changed_subflows){                               \
    g_queue_clear(changed_subflows);                  \
  }                                                   \
  for(it = subflows_list; it; it = it->next)          \
  {                                                   \
    SndSubflow* subflow = it->data;                   \
    if(subflow_id == 0 || subflow_id == 255){         \
      subflow->property = property_value;             \
      if(changed_subflows){                           \
        g_queue_push_head(changed_subflows, subflow); \
      }                                               \
      continue;                                       \
    }                                                 \
    if(subflow->id == subflow_id){                    \
      subflow->property = property_value;             \
      if(changed_subflows){                           \
        g_queue_push_head(changed_subflows, subflow); \
      }                                               \
      break;                                          \
    }                                                 \
  }                                                   \
}                                                     \

#define NOTIFY_CHANGED_SUBFLOWS(changed_subflows, notifier)        \
{                                                                  \
  while(!g_queue_is_empty(changed_subflows)){                      \
    notifier_do(notifier, g_queue_pop_head(changed_subflows)); \
  }                                                                \
}                                                                  \

#define _now(this) gst_clock_get_time (this->sysclock)
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to sndsubflows
static void
sndsubflows_finalize (
    GObject * object);


static SndSubflow*
_make_subflow(SndSubflows* base_db, guint8 subflow_id);

static void
_dispose_subflow(SndSubflow *subflow);


//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------

void
sndsubflows_class_init (SndSubflowsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndsubflows_finalize;

  GST_DEBUG_CATEGORY_INIT (sndsubflows_debug_category, "sndsubflows", 0,
      "Stream Splitter");

}

SndSubflows* make_sndsubflows(Mediator* monitoring_handler)
{
  SndSubflows *this;
  this = g_object_new (SNDSUBFLOWS_TYPE, NULL);

  this->monitoring_handler = g_object_ref(monitoring_handler);

  return this;
}

void
sndsubflows_finalize (GObject * object)
{
  SndSubflows *this = SNDSUBFLOWS (object);

  g_object_unref(this->monitoring_handler);

  g_queue_clear(this->changed_subflows);
  g_object_unref (this->sysclock);

  g_object_unref(this->on_subflow_detached);
  g_object_unref(this->on_subflow_joined);
  g_object_unref(this->on_congestion_controlling_type_changed);
  g_object_unref(this->on_path_active_changed);
  g_object_unref(this->on_desired_bitrate_changed);
  g_object_unref(this->on_subflow_state_changed);
  g_object_unref(this->on_subflow_state_stat_changed);
  g_object_unref(this->on_stable_target_bitrate_changed);

  g_object_unref(this->changed_subflows);

  g_free(this->subflows);
}


void
sndsubflows_init (SndSubflows * this)
{
  this->sysclock            = gst_system_clock_obtain ();
  this->made                = _now(this);

  this->on_subflow_detached                     = make_notifier("SndSubflow: on-subflow-detached");
  this->on_subflow_joined                       = make_notifier("SndSubflow: on-subflow-joined");
  this->on_congestion_controlling_type_changed  = make_notifier("SndSubflow: on-congestion-controlling-type-changed");
  this->on_path_active_changed                  = make_notifier("SndSubflow: on-path-active-changed");
  this->on_desired_bitrate_changed               = make_notifier("SndSubflow: on-target-bitrate-changed");
  this->on_stable_target_bitrate_changed        = make_notifier("SndSubflow: on-stable-target-bitrate-changed");
  this->on_subflow_state_changed                = make_notifier("SndSubflow: on-subflow-state-changed");
  this->on_subflow_state_stat_changed           = make_notifier("SndSubflow: on-subflow-state-stat");

  this->changed_subflows                        = g_queue_new();

}


void sndsubflows_join(SndSubflows* this, guint8 id)
{
  SndSubflow *subflow;

  if(this->subflows[id]){
    GST_WARNING_OBJECT(this, "Subflow with a given id %d is already joined", id);
    return;
  }

  subflow = _make_subflow(this, id);
  subflow->active = TRUE;

  this->subflows[id] = subflow;
  ++this->subflows_num;
  ++this->active_subflows_num;

  this->joined = g_slist_prepend(this->joined, subflow);
  sndsubflows_set_target_bitrate(this, id, SUBFLOW_DEFAULT_SENDING_RATE);
  notifier_do(this->on_subflow_joined, subflow);

}

void sndsubflows_detach(SndSubflows* this, guint8 id)
{
  SndSubflow* subflow;
  subflow = this->subflows[id];

  if(!subflow){
    GST_WARNING_OBJECT(this, "Subflow with a given id %d is not exist or it is already detached", id);
    return;
  }

  notifier_do(this->on_subflow_detached, subflow);

  this->joined = g_slist_remove(this->joined, subflow);

  --this->subflows_num;
  this->subflows[id] = NULL;

  _dispose_subflow(subflow);

}

GSList* sndsubflows_get_subflows(SndSubflows* this)
{
  return this->joined;
}

void sndsubflows_iterate(SndSubflows* this, GFunc process, gpointer udata)
{
  if(!this->joined){
    return;
  }
  g_slist_foreach(this->joined, process, udata);
}

void sndsubflows_add_on_subflow_joined_cb(SndSubflows* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_subflow_joined, callback, udata);
}

void sndsubflows_add_on_subflow_detached_cb(SndSubflows* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_subflow_detached, callback, udata);
}

void sndsubflows_add_on_subflow_state_changed_cb(SndSubflows* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_subflow_state_changed, callback, udata);
}

void sndsubflows_add_on_subflow_state_stat_changed_cb(SndSubflows* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_subflow_state_stat_changed, callback, udata);
}

void sndsubflows_add_on_congestion_controlling_type_changed_cb(SndSubflows* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_congestion_controlling_type_changed, callback, udata);
}

void sndsubflows_add_on_path_active_changed_cb(SndSubflows* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_path_active_changed, callback, udata);
}

void sndsubflows_add_on_desired_bitrate_changed_cb(SndSubflows* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_desired_bitrate_changed, callback, udata);
}

void sndsubflows_add_on_stable_target_bitrate_changed_cb(SndSubflows* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_stable_target_bitrate_changed, callback, udata);
}

void sndsubflow_monitoring_request(SndSubflow* subflow)
{
  SndSubflows *subflows = subflow->base_db;
  mediator_set_request(subflows->monitoring_handler, subflow);
}

void sndsubflow_set_desired_bitrate(SndSubflow* subflow, gint32 desired_rate)
{
  SndSubflows *subflows = subflow->base_db;
//  subflows->target_rate -= subflow->target_bitrate;
//  subflows->target_rate += subflow->target_bitrate = target_rate;
  subflow->allocated_target = desired_rate;

  notifier_do(subflows->on_desired_bitrate_changed, subflow);
}

void sndsubflow_set_stable_target_rate(SndSubflow* subflow, gint32 target_rate)
{
  SndSubflows *subflows = subflow->base_db;
  notifier_do(subflows->on_stable_target_bitrate_changed, subflow);
}

void sndsubflow_set_rtt(SndSubflow* subflow, GstClockTime rtt) {
  subflow->rtt = rtt;
}

void sndsubflow_set_eqd(SndSubflow* subflow, gint32 eqd) {
  subflow->eqd = eqd;
}

//gint32 sndsubflows_get_total_target(SndSubflows* this)
//{
//  return this->target_rate;
//}

guint sndsubflows_get_subflows_num(SndSubflows* this)
{
  return this->subflows_num;
}

gint sndsubflows_get_active_subflows_num(SndSubflows* this)
{
  return this->active_subflows_num;
}



SndSubflow* sndsubflows_get_subflow(SndSubflows* this, guint8 subflow_id)
{
  return this->subflows[subflow_id];
}


void sndsubflows_set_congestion_controlling_type(SndSubflows* this, guint8 subflow_id, CongestionControllingType new_type)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->joined, subflow_id, congestion_controlling_type, new_type, this->changed_subflows);
  NOTIFY_CHANGED_SUBFLOWS(this->changed_subflows, this->on_congestion_controlling_type_changed);
}

void sndsubflows_set_path_active(SndSubflows* this, guint8 subflow_id, gboolean value)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->joined, subflow_id, active, value, this->changed_subflows);
  NOTIFY_CHANGED_SUBFLOWS(this->changed_subflows, this->on_path_active_changed);
  this->active_subflows_num += value ? 1 : -1;
}

void sndsubflows_set_rtcp_interval_type(SndSubflows* this, guint8 subflow_id, RTCPIntervalType new_type)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->joined, subflow_id, rtcp_interval_type, new_type, NULL);
}

void sndsubflows_set_path_lossy(SndSubflows* this, guint8 subflow_id, gboolean value)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->joined, subflow_id, lossy, value, NULL);
}

void sndsubflows_set_path_congested(SndSubflows* this, guint8 subflow_id, gboolean value)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->joined, subflow_id, congested, value, NULL);
}

static void _on_subflow_desired_bitrate_changed(SndSubflow* subflow, SndSubflows* subflows)
{
//  subflows->target_rate += subflow->target_bitrate;
  notifier_do(subflows->on_desired_bitrate_changed, subflow);
}

void sndsubflows_set_target_bitrate(SndSubflows* this, guint8 subflow_id, gint32 target_bitrate)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->joined, subflow_id, allocated_target, target_bitrate, NULL);
//  this->target_rate = 0;
  g_slist_foreach(this->joined, (GFunc) _on_subflow_desired_bitrate_changed, this);
}

void sndsubflows_set_report_timeout(SndSubflows* this, guint8 subflow_id, GstClockTime report_timeout)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->joined, subflow_id, report_timeout, report_timeout, NULL);
}


//-----------------------------------------------------------------------------------------------------------

static void _collect_on_subflow_state_stat(SndSubflow* subflow, SndSubflowsStateStat* state_stat)
{
  if (state_stat->max < subflow->state) {
    state_stat->max = subflow->state;
  }

  if (subflow->state < state_stat->min) {
    state_stat->min = subflow->state;
  }
}

void sndsubflow_set_state(SndSubflow* subflow, SndSubflowState state)
{
  SndSubflows* this = subflow->base_db;
  SndSubflowsStateStat state_stat;
  subflow->prev_state = subflow->state;
  subflow->state = state;
  notifier_do(this->on_subflow_state_changed, subflow);

  state_stat.min = SNDSUBFLOW_STATE_INCREASING;
  state_stat.max = SNDSUBFLOW_STATE_CONGESTED;
  g_slist_foreach(this->joined, (GFunc) _collect_on_subflow_state_stat, &state_stat);
  notifier_do(this->on_subflow_state_stat_changed, &state_stat);
}

void sndsubflow_refresh_report_interval(SndSubflow* subflow)
{
  GstClockTime now = _now(subflow->base_db);
  if(subflow->last_report == 0){
    subflow->last_report = now;
    subflow->report_interval = 50 * GST_MSECOND;
    return;
  }

  subflow->report_interval = MIN(subflow->report_interval * 1.3, now - subflow->last_report);
  subflow->last_report = now;
}

SndSubflowState sndsubflow_get_state(SndSubflow* subflow)
{
  return subflow->state;
}

guint16 sndsubflow_get_next_subflow_seq(SndSubflow* subflow)
{
  return subflowseqtracker_increase(&subflow->seqtracker);
}

guint8 sndsubflow_get_flags_abs_value(SndSubflow* subflow)
{
  return (subflow->active ? 4 : 0) + (subflow->lossy ? 0 : 2) + (subflow->congested ? 0 : 1);
}


SndSubflow* _make_subflow(SndSubflows* base_db, guint8 subflow_id)
{
  SndSubflow* result = g_malloc0(sizeof(SndSubflow));
  result->base_db         = base_db;
  result->id              = subflow_id;
  result->control_channel = make_mediator();
  return result;
}

void _dispose_subflow(SndSubflow *subflow)
{
  g_object_unref(subflow->control_channel);
  g_free(subflow);
}



#undef CHANGE_SUBFLOW_PROPERTY_VALUE
