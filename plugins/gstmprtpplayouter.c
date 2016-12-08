#ifdef HAVE_CONFIG_H
#include "config.h"
#endif



#include <gst/gst.h>
#include <gst/gst.h>
#include <string.h>
#include "gstmprtpplayouter.h"
#include "gstmprtcpbuffer.h"
#include "streamjoiner.h"
#include "mprtplogger.h"


#include "rcvctrler.h"

typedef struct _SubflowSpecProp{
  #if G_BYTE_ORDER == G_LITTLE_ENDIAN
    guint32  value : 24;
    guint32  id     : 8;
  #elif G_BYTE_ORDER == G_BIG_ENDIAN
    guint32  id     : 8;
    guint32  value : 24;
  #else
  #error "G_BYTE_ORDER should be big or little endian."
  #endif
}SubflowSpecProp;

GST_DEBUG_CATEGORY_STATIC (gst_mprtpplayouter_debug_category);
#define GST_CAT_DEFAULT gst_mprtpplayouter_debug_category

#define THIS_LOCK(this) g_mutex_lock(&this->mutex)
#define THIS_UNLOCK(this) g_mutex_unlock(&this->mutex)



static void gst_mprtpplayouter_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpplayouter_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpplayouter_dispose (GObject * object);
static void gst_mprtpplayouter_finalize (GObject * object);

static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_mprtpplayouter_query (GstElement * element,
    GstQuery * query);
static GstFlowReturn gst_mprtpplayouter_mprtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_mprtpplayouter_sink_query (GstPad * sinkpad,
    GstObject * parent, GstQuery * query);
static gboolean gst_mprtpplayouter_src_query (GstPad * sinkpad,
    GstObject * parent, GstQuery * query);
static gboolean gst_mprtpplayouter_mprtcp_rr_src_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query);
static gboolean gst_mprtpplayouter_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_mprtpplayouter_mprtp_src_event(GstPad *pad, GstObject *parent,
    GstEvent *event);

static GstFlowReturn _processing_mprtcp_packet (GstMprtpplayouter * this,
    GstBuffer * buf);

static void
_playouter_on_rtcp_ready(
    GstMprtpplayouter *this,
    GstBuffer* buffer);

static void
_playouter_on_repair_response(
    GstMprtpplayouter *this,
    GstBuffer *rtpbuf);

static void
_playout_process (
    GstMprtpplayouter *this);
#define _trash_mprtp_buffer(this, mprtp) mprtp_free(mprtp)

#define _now(this) gst_clock_get_time (this->sysclock)

static void _forward_process(GstMprtpplayouter* this);


enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_ABS_TIME_EXT_HEADER_ID,
  PROP_FEC_PAYLOAD_TYPE,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_SETUP_CONTROLLING_MODE,
  PROP_MAX_REAPIR_DELAY,
  PROP_ENFORCED_DELAY,
  PROP_SETUP_RTCP_INTERVAL_TYPE,

};

/* pad templates */

static GstStaticPadTemplate gst_mprtpplayouter_mprtp_sink_template =
    GST_STATIC_PAD_TEMPLATE ("mprtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp;application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtpplayouter_mprtcp_sr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_mprtpplayouter_mprtp_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static GstStaticPadTemplate gst_mprtpplayouter_mprtcp_rr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtpplayouter, gst_mprtpplayouter,
    GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_mprtpplayouter_debug_category,
        "mprtpplayouter", 0, "debug category for mprtpplayouter element"));

static void
gst_mprtpplayouter_class_init (GstMprtpplayouterClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);


  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get(&gst_mprtpplayouter_mprtcp_sr_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtcp_rr_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "MPRTP Playouter", "Generic",
      "MPRTP Playouter assembles and playing out rtp streams",
      "Balázs Kreith <balazs.kreith@gmail.com>");

  gobject_class->set_property = gst_mprtpplayouter_set_property;
  gobject_class->get_property = gst_mprtpplayouter_get_property;
  gobject_class->dispose = gst_mprtpplayouter_dispose;
  gobject_class->finalize = gst_mprtpplayouter_finalize;

  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
        g_param_spec_uint ("mprtp-ext-header-id",
            "Multipath RTP Header Extension ID",
            "Sets or gets the RTP header extension ID for MPRTP",
            0, 15, MPRTP_DEFAULT_EXTENSION_HEADER_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ABS_TIME_EXT_HEADER_ID,
      g_param_spec_uint ("abs-time-ext-header-id",
          "Absolute time RTP extension ID",
          "Sets or gets the RTP header extension for abs NTP time.",
          0, 15, ABS_TIME_DEFAULT_EXTENSION_HEADER_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FEC_PAYLOAD_TYPE,
      g_param_spec_uint ("fec-payload-type",
          "Set or get the payload type of FEC packets.",
          "Set or get the payload type of FEC packets.",
          0, 127, FEC_PAYLOAD_DEFAULT_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_JOIN_SUBFLOW,
        g_param_spec_uint ("join-subflow",
            "Join a subflow with a given id",
            "Join a subflow with a given id.",
            0, MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_DETACH_SUBFLOW,
        g_param_spec_uint ("detach-subflow",
            "Detach a subflow with a given id.",
            "Detach a subflow with a given id.",
            0, MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_RTCP_INTERVAL_TYPE,
       g_param_spec_uint ("rtcp-interval-type",
            "RTCP interval types: 0 - regular, 1 - early, 2 - immediate feedback",
            "RTCP interval types: 0 - regular, 1 - early, 2 - immediate feedback",
            0,
            4294967295, 2, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_CONTROLLING_MODE,
      g_param_spec_uint ("controlling-mode",
          "Set the controlling mode. 0 - None, 1 - Regular, 2 - FRACTaL",
          "Set the controlling mode. 0 - None, 1 - Regular, 2 - FRACTaL",
          0, 4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_REAPIR_DELAY,
      g_param_spec_uint ("max-repair-delay",
          "Max time in ms the playouter waits for FEC response",
          "Max time in ms the playouter waits for FEC response", 0,
          100, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENFORCED_DELAY,
      g_param_spec_uint ("enforced-delay",
          "An enforced delay unifying the delys from different paths.",
          "An enforced delay unifying the delys from different paths.",
          0,
          1000, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));


  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpplayouter_query);
}


static void
gst_mprtpplayouter_init (GstMprtpplayouter * this)
{
//  init_mprtp_logger();
//  //TODO: Only for development use
//  mprtp_logger_set_state(TRUE);
//  mprtp_logger_set_system_command("bash -c '[ ! -d temp_logs ]' && mkdir temp_logs");
//  mprtp_logger_set_system_command("rm temp_logs/*");
//  mprtp_logger_set_target_directory("temp_logs/");

  this->mprtp_sinkpad =
      gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtp_sink_template,
      "mprtp_sink");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_sinkpad);

  this->mprtp_srcpad =
      gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtp_src_template,
      "mprtp_src");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_srcpad);

  this->mprtcp_sr_sinkpad =
      gst_pad_new_from_static_template(&gst_mprtpplayouter_mprtcp_sr_sink_template,
      "mprtcp_sr_sink");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_sr_sinkpad);

  this->mprtcp_rr_srcpad =
      gst_pad_new_from_static_template(&gst_mprtpplayouter_mprtcp_rr_src_template,
      "mprtcp_rr_src");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_rr_srcpad);

  gst_pad_set_query_function (this->mprtcp_rr_srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtcp_rr_src_query));

  gst_pad_set_chain_function (this->mprtcp_sr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtcp_sr_sink_chain));

  gst_pad_set_chain_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtp_sink_chain));
  gst_pad_set_query_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_sink_query));
  gst_pad_set_event_function (this->mprtp_sinkpad,
        GST_DEBUG_FUNCPTR (gst_mprtpplayouter_sink_event));
  GST_PAD_SET_PROXY_CAPS (this->mprtp_sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (this->mprtp_sinkpad);

  gst_pad_set_query_function (this->mprtp_srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_src_query));
  gst_pad_set_event_function (this->mprtp_srcpad,
        GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtp_src_event));
  gst_pad_use_fixed_caps (this->mprtp_srcpad);
  GST_PAD_SET_PROXY_CAPS (this->mprtp_srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (this->mprtp_srcpad);



  g_mutex_init (&this->mutex);
  g_cond_init(&this->receive_signal);
  g_cond_init(&this->waiting_signal);
  g_cond_init(&this->repair_signal);

  this->sysclock                 = gst_system_clock_obtain();
  this->fec_payload_type         = FEC_PAYLOAD_DEFAULT_ID;

  this->on_rtcp_ready            = make_notifier("MPRTPPly: on-rtcp-ready");
  this->on_recovered_buffer      = make_notifier("MPRTPPly: on-recovered-buffer");
  this->subflows                 = make_rcvsubflows();

  this->fec_decoder              = make_fecdecoder();
  this->jitterbuffer             = make_jitterbuffer();
  this->joiner                   = make_stream_joiner();

  this->rcvpackets               = make_rcvpackets();
  this->rcvtracker               = make_rcvtracker();

  this->controller               = make_rcvctrler(this->rcvtracker, this->subflows, this->on_rtcp_ready);

  this->max_repair_delay_in_ms   = 10;

  fecdecoder_add_response_listener(this->fec_decoder,
      (ListenerFunc) _playouter_on_repair_response, this);

  rcvpackets_set_abs_time_ext_header_id(this->rcvpackets, ABS_TIME_DEFAULT_EXTENSION_HEADER_ID);
  rcvpackets_set_mprtp_ext_header_id(this->rcvpackets, MPRTP_DEFAULT_EXTENSION_HEADER_ID);

  notifier_add_listener(this->on_rtcp_ready,       (ListenerFunc) _playouter_on_rtcp_ready,       this);
  notifier_add_listener(this->on_recovered_buffer, (ListenerFunc) rcvtracker_on_recovered_buffer, this);
}


void
gst_mprtpplayouter_finalize (GObject * object)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "finalize");
  g_object_unref (this->joiner);
  g_object_unref (this->controller);
  g_object_unref (this->sysclock);
  g_object_unref (this->jitterbuffer);
  g_object_unref (this->fec_decoder);

  /* clean up object here */
  G_OBJECT_CLASS (gst_mprtpplayouter_parent_class)->finalize (object);
//  while(!g_queue_is_empty(this->mprtp_buffer_pool)){
//    mprtp_free(g_queue_pop_head(this->mprtp_buffer_pool));
//  }
//  g_object_unref(this->mprtp_buffer_pool);
}


void
gst_mprtpplayouter_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);
  guint guint_value;
  SubflowSpecProp *subflow_prop;
  GST_DEBUG_OBJECT (this, "set_property");

  subflow_prop = (SubflowSpecProp*) &guint_value;
  THIS_LOCK (this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      rcvpackets_set_mprtp_ext_header_id(this->rcvpackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      rcvpackets_set_abs_time_ext_header_id(this->rcvpackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      this->fec_payload_type = g_value_get_uint (value);
      break;
    case PROP_JOIN_SUBFLOW:
      rcvsubflows_join(this->subflows, g_value_get_uint (value));
      break;
    case PROP_DETACH_SUBFLOW:
      rcvsubflows_detach(this->subflows, g_value_get_uint (value));
      break;
    case PROP_SETUP_RTCP_INTERVAL_TYPE:
      guint_value = g_value_get_uint (value);
      rcvsubflows_set_rtcp_interval_type(this->subflows, subflow_prop->id, subflow_prop->value);
      break;
    case PROP_SETUP_CONTROLLING_MODE:
      guint_value = g_value_get_uint (value);
      rcvsubflows_set_congestion_controlling_type(this->subflows, subflow_prop->id, subflow_prop->value);
      break;
    case PROP_MAX_REAPIR_DELAY:
      guint_value = g_value_get_uint (value);
      this->max_repair_delay_in_ms = guint_value;
      break;
    case PROP_ENFORCED_DELAY:
      guint_value = g_value_get_uint (value);
      stream_joiner_set_enforced_delay(this->joiner, guint_value * GST_MSECOND);
      //gst_pad_push_event(this->mprtp_srcpad, gst_event_new_latency(stream_joiner_get_latency(this->joiner)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK (this);
}


void
gst_mprtpplayouter_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "get_property");
  THIS_LOCK (this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) rcvpackets_get_mprtp_ext_header_id(this->rcvpackets));
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) rcvpackets_get_abs_time_ext_header_id(this->rcvpackets));
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      g_value_set_uint (value, (guint) this->fec_payload_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK (this);
}

gboolean
gst_mprtpplayouter_src_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result = FALSE;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min, max;
      GstPad *peer;
      peer = gst_pad_get_peer (this->mprtp_sinkpad);
      if ((result = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);
//          min= GST_MSECOND;
//          min = 0;
          min = stream_joiner_get_latency(this->joiner);
          max = -1;
          gst_query_set_latency (query, live, min, max);
      }
      gst_object_unref (peer);
    }
    break;
    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }
  return result;
}


gboolean
gst_mprtpplayouter_mprtcp_rr_src_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result = FALSE;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }
  return result;
}


gboolean
gst_mprtpplayouter_sink_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {

    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }
  return result;
}

gboolean gst_mprtpplayouter_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS:
        case GST_EVENT_FLUSH_STOP:
        case GST_EVENT_STREAM_START:
        case GST_EVENT_SEGMENT:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}

gboolean gst_mprtpplayouter_mprtp_src_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_FLUSH_START:
        case GST_EVENT_RECONFIGURE:
        case GST_EVENT_FLUSH_STOP:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}

void
gst_mprtpplayouter_dispose (GObject * object)
{
  GstMprtpplayouter *mprtpplayouter = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (mprtpplayouter, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_mprtpplayouter_parent_class)->dispose (object);
}

static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMprtpplayouter *this;
  g_return_val_if_fail (GST_IS_MPRTPPLAYOUTER (element),
      GST_STATE_CHANGE_FAILURE);

  this = GST_MPRTPPLAYOUTER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_pad_start_task(this->mprtp_srcpad, (GstTaskFunction)_playout_process, this, NULL);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      this->buffers = g_async_queue_new();
      this->thread = gst_task_new ((GstTaskFunction)_forward_process, this, NULL);
      gst_task_set_lock (this->thread, &this->thread_mutex);
      gst_task_start (this->thread);
        break;
      default:
        break;
    }

    ret =
        GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->change_state
        (element, transition);

    switch (transition) {
      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        gst_task_stop (this->thread);
        g_async_queue_unref(this->buffers);
        this->buffers = NULL;
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_mprtpplayouter_query (GstElement * element, GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (element);
  gboolean ret = TRUE;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret =
          GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}

//static guint received = 0;

static GstFlowReturn
gst_mprtpplayouter_mprtp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  guint8  *buf_2nd_byte;
  RcvPacket* packet;
  GstFlowReturn result = GST_FLOW_OK;

  this = GST_MPRTPPLAYOUTER (parent);

  GST_DEBUG_OBJECT (this, "RTP/RTCP/MPRTP/MPRTCP sink");

  if(!GST_IS_BUFFER(buf)){
    GST_WARNING("The arrived buffer is not a buffer.");
    goto done;
  }

  //Artificial lost
//  if(++received % 11 == 0){
//    gst_buffer_unref(buf);
//    goto done;
//  }

  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }
  buf_2nd_byte = info.data + 1;
  gst_buffer_unmap (buf, &info);

  //demultiplexing based on RFC5761
  if (*buf_2nd_byte == MPRTCP_PACKET_TYPE_IDENTIFIER) {
    result = _processing_mprtcp_packet (this, buf);
    goto done;
  }

  //check weather the packet is rtcp or mprtp
  if (*buf_2nd_byte > 192 && *buf_2nd_byte < 223) {
    if(GST_IS_BUFFER(buf)){
      gst_pad_push(this->mprtp_srcpad, buf);
    }
    goto done;
  }

  //if(!gst_rtp_buffer_is_mprtp(this->rcvpackets, buf)){
  if(!gst_buffer_is_mprtp(buf, rcvpackets_get_mprtp_ext_header_id(this->rcvpackets))){
    if(GST_IS_BUFFER(buf)){
      gst_pad_push(this->mprtp_srcpad, buf);
    }
    goto done;
  }

  if (*buf_2nd_byte == this->fec_payload_type) {
    fecdecoder_add_fec_buffer(this->fec_decoder, gst_buffer_ref(buf));
    goto done;
  }else{
    fecdecoder_add_rtp_buffer(this->fec_decoder, gst_buffer_ref(buf));
  }

//  packet = rcvpackets_get_packet(this->rcvpackets, gst_buffer_ref(buf));
//  return gst_pad_push(this->mprtp_srcpad, rcvpacket_retrieve_buffer_and_unref(packet));

PROFILING("MPRTPPLAYOUTER LOCK",
  THIS_LOCK(this);
);

//PROFILING("Processing arrived packet",
  packet = rcvpackets_get_packet(this->rcvpackets, gst_buffer_ref(buf));
  rcvtracker_add_packet(this->rcvtracker, packet);

  if(jitterbuffer_is_packet_discarded(this->jitterbuffer, packet)){
//    g_print("Discarded packet: %hu - %hu - %lu\n", packet->abs_seq, this->jitterbuffer->last_seq, GST_TIME_AS_MSECONDS(this->jitterbuffer->playout_delay));
    rcvtracker_add_discarded_packet(this->rcvtracker, packet);
    goto unlock_and_done;
  }
  stream_joiner_push_packet(this->joiner, packet);

//  g_print("Packet from subflow %d arrived with seq: %hu\n", packet->subflow_id, packet->abs_seq);
  g_cond_signal(&this->receive_signal);

  rcvctrler_time_update(this->controller);
//);

unlock_and_done:
  THIS_UNLOCK(this);
done:
  return result;

}


static GstFlowReturn
gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  GstFlowReturn result = GST_FLOW_OK;
  guint8 *buf_2nd_byte;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTCP/MPRTCP sink");
  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }

  buf_2nd_byte = info.data + 1;
  gst_buffer_unmap (buf, &info);

  if (*buf_2nd_byte == this->fec_payload_type) {
//    g_print("FEC BUFFER ARRIVED\n");
    fecdecoder_add_fec_buffer(this->fec_decoder, gst_buffer_ref(buf));
    goto done;
  }

  result = _processing_mprtcp_packet (this, buf);

done:
  return result;

}


GstFlowReturn
_processing_mprtcp_packet (GstMprtpplayouter * this, GstBuffer * buf)
{
  GstFlowReturn result = GST_FLOW_OK;;
//  PROFILING("_processing_mprtcp_packet LOCK",
    THIS_LOCK (this);
//  );

//  PROFILING("_processing_mprtcp_packet",
    rcvctrler_receive_mprtcp(this->controller, buf);
//  );

//  PROFILING("_processing_mprtcp_packet UNLOCK",
    THIS_UNLOCK (this);
//  );

//  {
//      GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
//      gst_rtcp_buffer_map(buf, GST_MAP_READ, &rtcp);
//      gst_print_rtcp_buffer(&rtcp);
//      gst_rtcp_buffer_unmap(&rtcp);
//  }
  return result;
}

void _playouter_on_rtcp_ready(GstMprtpplayouter *this, GstBuffer* buffer)
{
//  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
//  gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp);
//  gst_print_rtcp_buffer(&rtcp);
//  gst_rtcp_buffer_unmap(&rtcp);
  if(!gst_pad_is_linked(this->mprtcp_rr_srcpad)){
    GST_WARNING_OBJECT(this, "Pads are not linked for MPRTCP");
    return;
  }

  gst_pad_push(this->mprtcp_rr_srcpad, buffer);
}

void _playouter_on_repair_response(GstMprtpplayouter *this, GstBuffer *rtpbuf)
{
  PROFILING("_playouter_on_repair_response LOCK",
    THIS_LOCK(this);
  );

  this->repairedbuf = rtpbuf;
  g_cond_signal(&this->repair_signal);
  THIS_UNLOCK(this);
}

static void _wait(GstMprtpplayouter *this, GstClockTime end, gint64 step_in_microseconds)
{
  gint64 end_time;
  while(_now(this) < end){
    end_time = g_get_monotonic_time() + step_in_microseconds;
    g_cond_wait_until(&this->waiting_signal, &this->mutex, end_time);
  }
}

static void _wait2(GstMprtpplayouter *this, GstClockTime end)
{
  GstClockTime now = _now(this);
  gint64  end_time  = g_get_monotonic_time();
  if(now < end){
    guint64 wait_time = MAX((end - now) / 2000, 1000);
    end_time += wait_time;
  }else{
    end_time += 1000;
  }
  g_cond_wait_until(&this->waiting_signal, &this->mutex, end_time);
}



static gboolean _repair_responsed(GstMprtpplayouter *this)
{
  gint64 end_time;
  end_time = g_get_monotonic_time() + this->max_repair_delay_in_ms * G_TIME_SPAN_MILLISECOND;
  return g_cond_wait_until(&this->repair_signal, &this->mutex, end_time);
}

static void
_playout_process (GstMprtpplayouter *this)
{
  RcvPacket *packet;
  GstClockTime playout_time, now;
  guint16 gap_seq;
  THIS_LOCK(this);

  now = playout_time = _now(this);
  while((packet = stream_joiner_pop_packet(this->joiner)) != NULL){
    jitterbuffer_push_packet(this->jitterbuffer, packet);
  }

  packet = jitterbuffer_pop_packet(this->jitterbuffer, &playout_time);
  if(!packet){
    if(!playout_time){
      //in this case we have no packet in the jitterbuffer
//      g_cond_wait(&this->receive_signal, &this->mutex);
      g_cond_wait_until(&this->receive_signal, &this->mutex, g_get_monotonic_time() + 5 * G_TIME_SPAN_MILLISECOND);
    }else if(now < playout_time){//we have packet, but it must wait
//      g_print("before playout_time, the diff is  %lu\n", playout_time - _now(this));
      DISABLE_LINE _wait(this, playout_time, 1000);
      _wait2(this, playout_time);
      DISABLE_LINE g_cond_wait_until(&this->waiting_signal, &this->mutex, g_get_monotonic_time() + G_TIME_SPAN_MILLISECOND);
//      g_print("after waiting until playout_time, the diff is  %lu\n", _now(this) - playout_time);
    }
    goto done;
  }

  if(jitterbuffer_has_repair_request(this->jitterbuffer, &gap_seq)){
    if(this->repairedbuf){
      GST_WARNING_OBJECT(this, "A repair packet arrived perhaps later than it was necessary");
      gst_buffer_unref(this->repairedbuf);
      this->repairedbuf = NULL;
    }

    fecdecoder_request_repair(this->fec_decoder, gap_seq);

    if(!_repair_responsed(this)){
      GST_WARNING_OBJECT(this, "max_repair_delay reached without response. Maybe need to increase it");
    }

    if(this->repairedbuf){
//      gst_pad_push(this->mprtp_srcpad, this->discarded_packet.repairedbuf);
//      g_print("Repaired buffer appeared: %hu\n", gap_seq);
      notifier_do(this->on_recovered_buffer, this->repairedbuf);
      g_async_queue_push(this->buffers, this->repairedbuf);
      this->repairedbuf = NULL;
    }
  }

//PROFILING("_playout_process",
//  g_print("Packet arrived at subflow %d with abs seq %hu forwarded\n", packet->subflow_id, packet->abs_seq);
//  gst_pad_push(this->mprtp_srcpad, packet->buffer);
  g_async_queue_push(this->buffers, packet->buffer);

//);

done:
  THIS_UNLOCK(this);
  return;
}

//void _playout_process(GstMprtpplayouter *this)
//{
//   PROFILING("_playout_process",
//       _playout_process_(this);
//   );
//}

void _forward_process(GstMprtpplayouter* this){
  GstBuffer* buffer = g_async_queue_pop(this->buffers);
  gst_pad_push(this->mprtp_srcpad, buffer);
}

#undef THIS_LOCK
#undef THIS_UNLOCK
