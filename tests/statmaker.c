#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <pcap.h>

#define current_unix_time_in_us g_get_real_time ()
#define current_unix_time_in_ms (current_unix_time_in_us / 1000L)
#define current_unix_time_in_s  (current_unix_time_in_ms / 1000L)
#define epoch_now_in_ns ((guint64)((current_unix_time_in_us + 2208988800000000LL) * 1000))
#define get_ntp_from_epoch_ns(epoch_in_ns) gst_util_uint64_scale (epoch_in_ns, (1LL << 32), GST_SECOND)
#define get_epoch_time_from_ntp_in_ns(ntp_time) gst_util_uint64_scale (ntp_time, GST_SECOND, (1LL << 32))
#define NTP_NOW get_ntp_from_epoch_ns(epoch_now_in_ns)
#define _now(this) gst_clock_get_time (this->sysclock)

#define FEC_PAYLOAD_TYPE 126

typedef struct _Packet
{
  guint64              tracked_ntp;
  guint16              seq_num;
  guint32              ssrc;
  guint8               subflow_id;
  guint16              subflow_seq;

  gboolean             marker;
  guint8               payload_type;
  guint32              timestamp;

  guint                header_size;
  guint                payload_size;

  guint16              protect_begin;
  guint16              protect_end;
}Packet;


static _setup_packet(Packet* packet, gchar* line){
  sscanf(line, "%lu,%hu,%u,%u,%d,%u,%d,%d,%d,%hu,%hu,%d",
        &packet->tracked_ntp,
        &packet->seq_num,
        &packet->timestamp,
        &packet->ssrc,
        &packet->payload_type,
        &packet->payload_size,
        &packet->subflow_id,
        &packet->subflow_seq,
        &packet->header_size,
        &packet->protect_begin,
        &packet->protect_end,
        &packet->marker
  );
}

static Packet* _make_packet(gchar* line){
  Packet* packet = g_malloc0(sizeof(Packet));
  _setup_packet(packet, line);
  return packet;
}
static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}


typedef struct{
  GQueue* items;
  void (*addcb)(gpointer udata, gpointer* item);
  void (*remcb)(gpointer udata, gpointer* item);
  void (*free)(Packet*);
  void (*sampler)(gpointer);
  GstClockTime (*extractor)(gpointer);
  gpointer udata;
  GstClockTime first;
  GstClockTime last;
  GstClockTime sampled;
}SlidingWindow;

SlidingWindow* _make_sliding_window(
    void (*addcb)(gpointer udata, gpointer* item),
    void (*remcb)(gpointer udata, gpointer* item),
    void (*sampler)(gpointer udata),
    GstClockTime (*extractor)(gpointer),
    gpointer udata,
    void (*free)(Packet*)
)
{
  SlidingWindow* this = g_malloc0(sizeof(SlidingWindow));
  this->addcb     = addcb;
  this->remcb     = remcb;
  this->free      = free;
  this->udata     = udata;
  this->sampler   = sampler;
  this->extractor = extractor;
  this->items = g_queue_new();
  return this;
}

void _free_sliding_window(SlidingWindow* this){
  g_queue_free_full(this->items, this->free);
  g_free(this);
}


void _sliding_window_push(SlidingWindow* this, gpointer* item){
  g_queue_push_tail(this->items, item);
  this->last  = this->extractor(item);
  if(!this->first){
    this->sampled = this->first = this->last;
  }
  if(this->addcb){
    this->addcb(this->udata, item);
  }
  while(this->first < this->last - GST_SECOND){
    Packet* obsolated = g_queue_pop_head(this->items);
    if(this->remcb){
        this->remcb(this->udata, obsolated);
    }
    this->first = this->extractor(g_queue_peek_head(this->items));
    if(this->free){
      this->free(obsolated);
    }
  }

  while(this->sampled < this->last - 100 * GST_MSECOND){
    if(this->sampler){
      this->sampler(this->udata);
      this->sampled += 100 * GST_MSECOND;
    }
  }
}

typedef struct _QueueTuple{
  GQueue* q1;
  GQueue* q2;
}QueueTuple;

typedef struct{
  gint32      sending_rate;
  gint32      fec_rate;
  QueueTuple* results;
}RateTuple;

static gint32 _get_sent_bytes(Packet* packet){
  return packet->payload_size + packet->header_size + 8 /*UDP header*/;
}
static void _refresh_rate(RateTuple* this, Packet* packet, gint32 multiplier){
  if(packet->payload_type == FEC_PAYLOAD_TYPE){
     this->fec_rate += _get_sent_bytes(packet) * multiplier;
   }else{
     this->sending_rate += _get_sent_bytes(packet) * multiplier;
   }
}

static void _add_rate(RateTuple* this, Packet* packet){
  _refresh_rate(this, packet, 1);
}

static void _rem_rate(RateTuple* this, Packet* packet){
  _refresh_rate(this, packet, -1);
}

static void _rate_sampler(RateTuple* this){
  guint32* value = g_malloc0(sizeof(guint32));
  *value = this->sending_rate;
  g_queue_push_tail(this->results->q1, value);
  value = g_malloc0(sizeof(guint32));
  *value = this->fec_rate;
  g_queue_push_tail(this->results->q2, value);
}

static GstClockTime _packet_tracked_ntp_in_ns(Packet* packet){
  return get_epoch_time_from_ntp_in_ns(packet->tracked_ntp);
}

static QueueTuple* _get_sending_rates(FILE* fp){
  QueueTuple* tuple = g_malloc0(sizeof(QueueTuple));
  RateTuple   rates = {0,0,tuple};
  SlidingWindow* sw = _make_sliding_window(_add_rate, _rem_rate, _rate_sampler, _packet_tracked_ntp_in_ns, &rates, g_free);
  gchar line[1024];

  tuple->q1 = g_queue_new();
  tuple->q2 = g_queue_new();

  while (fgets(line, 1024, fp)){
     _sliding_window_push(sw, _make_packet(line));
  }
  _free_sliding_window(sw);
  return tuple;
}

static void _gp_sampler(RateTuple* this){
  guint32* value = g_malloc0(sizeof(guint32));
  *value = this->sending_rate;
  g_queue_push_tail(this->results->q1, value);
  value = g_malloc0(sizeof(guint32));
  *value = this->fec_rate;
  g_queue_push_tail(this->results->q2, value);
}

static QueueTuple* _get_goodput_rate(FILE* fp){
  QueueTuple* tuple = g_malloc0(sizeof(QueueTuple));
  RateTuple rates = {0,0,tuple};
  SlidingWindow* sw = _make_sliding_window(_add_rate, _rem_rate, _gp_sampler, _packet_tracked_ntp_in_ns, &rates, g_free);
  gchar line[1024];
  Packet* packet;
  guint16 act_seq = 0;
  gboolean init = FALSE;

  tuple->q1 = g_queue_new();
  tuple->q2 = g_queue_new();

  while (fgets(line, 1024, fp)){
    packet = _make_packet(line);
    if(init == FALSE){
      init = TRUE;
    }else if(_cmp_seq(packet->seq_num, act_seq) < 0){
      continue;
    }
    _sliding_window_push(sw, packet);
    act_seq = packet->seq_num;
  }
  _free_sliding_window(sw);
  return tuple;
}
typedef struct{
  GstClockTime timestamp;
  guint16      dst_port;
  guint16      src_port;
  gint32       size;
}TCPPacket;

/* ethernet headers are always exactly 14 bytes [1] */
#define SIZE_ETHERNET 14

static TCPPacket* _make_tcppacket(struct pcap_pkthdr* header, gchar* bytes){
  TCPPacket* packet = g_malloc0(sizeof(TCPPacket));
  guint16 ihl;//IP header length
  struct sniff_ip* ip;
  packet->timestamp = (GstClockTime)header->ts.tv_sec * GST_SECOND + (GstClockTime)header->ts.tv_usec * GST_USECOND;
  ip = (struct sniff_ip*)(bytes + SIZE_ETHERNET);
  ihl = (*(guint8*)(bytes + SIZE_ETHERNET) & 0x0F)*4;
  packet->size = g_ntohs(*((guint16*) (bytes + SIZE_ETHERNET + 2)));
  packet->src_port = g_ntohs((guint16*)(bytes + SIZE_ETHERNET + ihl + 0));
  packet->dst_port = g_ntohs((guint16*)(bytes + SIZE_ETHERNET + ihl + 2));
  return packet;
}

typedef struct{
  guint16      src_port;
  guint16      dst_port;
  GstClockTime last;
}TCPFlow;

typedef struct {
  guint32 packets_num;
  guint32 sending_rate;
  guint32 flownum;
  GList*  flowslist;
  QueueTuple* tuple;
}TCPRate;

static gint _find_tcp_flow(TCPFlow* tcp_flow, TCPPacket* packet){
  return tcp_flow->dst_port == packet->dst_port && tcp_flow->src_port == packet->src_port ? 0 : -1;
}

static TCPFlow* _make_tcp_flow(TCPPacket* packet){
  TCPFlow* this = g_malloc0(sizeof(TCPFlow));
  this->src_port = packet->src_port;
  this->dst_port = packet->dst_port;

  return this;
}

static void _sw_add_tcp_packet(TCPRate* this, TCPPacket* packet){
  TCPFlow* tcp_flow;
  this->sending_rate += packet->size;
  ++this->packets_num;

  tcp_flow = g_list_find_custom(this->flowslist, packet, _find_tcp_flow);
  if(!tcp_flow){
    tcp_flow = _make_tcp_flow(packet);
    this->flowslist = g_list_prepend(this->flowslist, tcp_flow);
    ++this->flownum;
  }
  tcp_flow->last = packet->timestamp;
}

static void _sw_rem_tcp_packet(TCPRate* this, TCPPacket* packet){
  TCPFlow* tcp_flow;
  this->sending_rate -= packet->size;
  --this->packets_num;

  tcp_flow = g_list_find_custom(this->flowslist, packet, _find_tcp_flow);
  if(tcp_flow && tcp_flow->last == packet->timestamp){
    this->flowslist = g_list_remove(this->flowslist, tcp_flow);
    --this->flownum;
    g_free(tcp_flow);
  }
}

static GstClockTime _tcp_packet_ts_in_ns(TCPPacket* packet){
  return packet->timestamp;
}

static void _sw_sampling_tcp_packets(TCPRate* this){
  guint32* value = g_malloc0(sizeof(guint32));
  *value = this->sending_rate;
  g_queue_push_tail(this->tuple->q1, value);
  value = g_malloc0(sizeof(guint32));
  *value = this->flownum;
  g_queue_push_tail(this->tuple->q2, value);
}

static QueueTuple* tcpstat(gchar* path)
{
  pcap_t *pcap;
  const unsigned char *packet;
  char errbuf[1400];
  struct pcap_pkthdr header;
  TCPPacket* tcp_packet;
  QueueTuple* results = g_malloc0(sizeof(QueueTuple));
  TCPRate rates = {0,0,0,NULL, results};
  results->q1 = g_queue_new();
  results->q2 = g_queue_new();

  SlidingWindow* sw = _make_sliding_window(
      _sw_add_tcp_packet,
      _sw_rem_tcp_packet,
      _sw_sampling_tcp_packets,
      _tcp_packet_ts_in_ns,
      &rates,
      g_free);

  pcap = pcap_open_offline(path, errbuf);
  if (pcap == NULL)
  {
    g_print("error reading pcap file: %s\n", errbuf);
    exit(1);
  }

  /* Now just loop through extracting packets as long as we have
   * some to read.
   */
  while ((packet = pcap_next(pcap, &header)) != NULL){
    tcp_packet = _make_tcppacket(&header, packet);
//    g_print("ts: %lu size: %lu %hu-%hu\n", tcp_packet->timestamp, tcp_packet->size, tcp_packet->src_port, tcp_packet->dst_port);
    _sliding_window_push(sw, tcp_packet);
  }

  // terminate
  _free_sliding_window(sw);
  return results;
}


typedef struct{
  guint32 sr_1,sr_2;
  GQueue* result;
}RatioTuple;

static void _refresh_ratio(RatioTuple* this, Packet* packet, gint32 multiplier){
  if(packet->subflow_id == 1){
    this->sr_1 += _get_sent_bytes(packet) * multiplier;
  }else if(packet->subflow_id == 2){
    this->sr_2 += _get_sent_bytes(packet) * multiplier;
  }
}

static void _add_ratio(RatioTuple* this, Packet* packet){
  _refresh_ratio(this, packet, 1);
}

static void _rem_ratio(RatioTuple* this, Packet* packet){
  _refresh_ratio(this, packet, -1);
}

static void _ratio_sampler(RatioTuple* this){
  gdouble* value = g_malloc0(sizeof(gdouble));
  *value = (gdouble)this->sr_1 / (gdouble)(this->sr_1 + this->sr_2);
  g_queue_push_tail(this->result, value);
//  g_print("%1.3f\n", *value);
}


static GQueue* _get_subflow_ratio(FILE* fp){
  GQueue* result = g_queue_new();
  RatioTuple ratios = {0,0,result};
  SlidingWindow* sw = _make_sliding_window(_add_ratio, _rem_ratio, _ratio_sampler, _packet_tracked_ntp_in_ns, &ratios, g_free);
  gchar line[1024];
  Packet* packet;

  while (fgets(line, 1024, fp)){
    packet = _make_packet(line);
    _sliding_window_push(sw, packet);
  }
  _free_sliding_window(sw);
  return result;
}



static gint _cmp_packets_seq(Packet* a, Packet* b, gpointer udata){
  return _cmp_seq(a->seq_num, b->seq_num);
}

static gint _cmp_packets_seq_simple(Packet* a, Packet* b){
  return a->seq_num == b->seq_num ? 0 : -1;
}

static gint _cmp_timestamp(Packet* a, Packet* b){
  return a->timestamp == b->timestamp ? 0 : -1;
}

typedef struct _PairedTimestamp{
  guint64 snd_ntp;
  guint64 rcv_ntp;
}PairedTimestamp;


static PairedTimestamp* _get_queue_paired_timestamps(Packet* snd_packet, GQueue* rcv_packets){
  PairedTimestamp* result = NULL;
  Packet* rcv_packet;
  GList* list;
  list = g_queue_find_custom(rcv_packets, snd_packet, _cmp_packets_seq_simple);
  if(!list){
    return result;
  }
  rcv_packet = list->data;

  result = g_malloc0(sizeof(PairedTimestamp));
//  g_print("%hu - %hu\n", snd_packet->seq_num, rcv_packet->seq_num);
  result->rcv_ntp = rcv_packet->tracked_ntp;
  result->snd_ntp = snd_packet->tracked_ntp;
  return result;
}

typedef struct{
  guint32 discarded_packets_num;
  guint32 discarded_bytes;
}DiscardTuple;

static DiscardTuple* _get_discard_tuple(FILE* fp){
  DiscardTuple* result = g_malloc0(sizeof(DiscardTuple));
  gchar line[1024];
  Packet packet;
  guint16 act_seq = 0;
  gboolean init = FALSE;

  while (fgets(line, 1024, fp)){
    _setup_packet(&packet, line);
    if(init == FALSE){
      init = TRUE;
      act_seq = packet.seq_num;
    }
    if(_cmp_seq(act_seq, packet.seq_num) <= 0){
      act_seq = packet.seq_num;
      continue;
    }
    ++result->discarded_packets_num;
    result->discarded_bytes += _get_sent_bytes(&packet);
  }
  return result;
}

static GQueue* _get_queue_timestamps(FILE* snd, FILE* rcv){
  GQueue* result = g_queue_new();
  GQueue* snd_packets = g_queue_new();
  GQueue* rcv_packets = g_queue_new();
  gchar line[1024];
  Packet *packet,*snd_head,*rcv_head, *rcv_tail;
  GstClockTime* pair_timestamp;
  GstClockTime packet_ts = 0;
  guint32 sending_rate = 0;

  while (fgets(line, 1024, snd)){
    packet = _make_packet(line);
    g_queue_push_tail(snd_packets, packet);
  }
  snd_head = g_queue_peek_head(snd_packets);

  while (fgets(line, 1024, rcv)){
    packet = _make_packet(line);
    g_queue_push_tail(rcv_packets, packet);

    rcv_tail = g_queue_peek_tail(rcv_packets);
    while(get_epoch_time_from_ntp_in_ns(snd_head->tracked_ntp) < get_epoch_time_from_ntp_in_ns(rcv_tail->tracked_ntp) - 30 * GST_SECOND){
      Packet* p = g_queue_pop_head(snd_packets);
      pair_timestamp = _get_queue_paired_timestamps(p, rcv_packets);
      if(pair_timestamp){
        g_queue_push_tail(result, pair_timestamp);
      }
      g_free(p);
      snd_head = g_queue_peek_head(snd_packets);
    }
  }

  while(!g_queue_is_empty(snd_packets)){
    Packet* p = g_queue_pop_head(snd_packets);
    pair_timestamp = _get_queue_paired_timestamps(p, rcv_packets);
    if(pair_timestamp){
      g_queue_push_tail(result, pair_timestamp);
    }
    g_free(p);
  }

  g_queue_free_full(rcv_packets, g_free);
  return result;
}

typedef struct{
  guint32 sent_bytes;
  guint32 lost_bytes;
  guint32 sent_packets_num;
  guint32 lost_packets_num;
  GQueue* lost_packets;
}LostTuple;

static LostTuple* _get_lost_tuple(FILE* snd, FILE* rcv){
  LostTuple* result = g_malloc0(sizeof(LostTuple));
  GQueue* snd_packets  = g_queue_new();
  GQueue* rcv_packets  = g_queue_new();
  GQueue* lost_packets = g_queue_new();
  gchar line[1024];
  Packet *packet,*snd_head, *rcv_tail;
  GstClockTime* pair_timestamp;
  GstClockTime packet_ts = 0;
  guint32 sending_rate = 0;

  result->lost_packets = lost_packets;

  while (fgets(line, 1024, snd)){
    packet = _make_packet(line);
    g_queue_push_tail(snd_packets, packet);
  }
  snd_head = g_queue_peek_head(snd_packets);

  while (fgets(line, 1024, rcv)){
    packet = _make_packet(line);
    g_queue_push_tail(rcv_packets, packet);

    rcv_tail = g_queue_peek_tail(rcv_packets);
    while(get_epoch_time_from_ntp_in_ns(snd_head->tracked_ntp) < get_epoch_time_from_ntp_in_ns(rcv_tail->tracked_ntp) - 30 * GST_SECOND){
      Packet *sent = g_queue_pop_head(snd_packets);
      GList* list;
//      g_queue_foreach(rcv_packets, _print, packet);g_print("\n\n||||||||%hu||||||||||||\n\n", p->seq_num);
      list = g_queue_find_custom(rcv_packets, sent, _cmp_packets_seq_simple);
      result->sent_bytes += _get_sent_bytes(sent);
      ++result->sent_packets_num;
      if(!list){
        g_queue_push_tail(lost_packets, sent);
        result->lost_bytes += _get_sent_bytes(sent);
        ++result->lost_packets_num;
      }else{
        Packet* rcved = list->data;
        g_queue_remove(rcv_packets, rcved);
        g_free(rcved);
        g_free(sent);
      }
      snd_head = g_queue_peek_head(snd_packets);
    }
  }

  while(!g_queue_is_empty(snd_packets)){
    Packet* sent = g_queue_pop_head(snd_packets);
    GList* list;
    list = g_queue_find_custom(rcv_packets, sent, _cmp_packets_seq_simple);
    result->sent_bytes += _get_sent_bytes(sent);
    ++result->sent_packets_num;
    if(!list){
      g_queue_push_tail(lost_packets, sent);
      result->lost_bytes += _get_sent_bytes(sent);
      ++result->lost_packets_num;
    }else{
      Packet* rcved = list->data;
      g_queue_remove(rcv_packets, rcved);
      g_free(rcved);
      g_free(sent);
    }

  }

  g_queue_free_full(rcv_packets, g_free);
  return result;
}

static guint32 _get_lost_frame_nums(FILE* sndp, FILE* plyp){
  guint32 result = 0;
  gchar line[1024];
  Packet played;
  guint16 act_seq = 0;
  gboolean init = FALSE;
  guint32 lost_timestamp = 0;

  while (fgets(line, 1024, plyp)){
    _setup_packet(&played, line);
//    packet = _make_packet(line);
    if(init == FALSE){
      init = TRUE;
      act_seq = played.seq_num;
    }else if(_cmp_seq(played.seq_num, act_seq) < 0){
      continue;
    }

    if((guint16)(act_seq + 1) != played.seq_num){
      Packet sent;
      while (fgets(line, 1024, sndp)){
        _setup_packet(&sent, line);
        if(_cmp_seq(sent.seq_num, act_seq) < 0){
          continue;
        }
        if(_cmp_seq(played.seq_num, sent.seq_num) <= 0){
          break;
        }
        if(lost_timestamp == sent.timestamp){
          continue;
        }
//        g_print("%hu - %u\n", sent.seq_num, sent.timestamp);
        lost_timestamp = sent.timestamp;
        ++result;
      }
    }
    act_seq = played.seq_num;
  }
  return result;
}

typedef struct{
  guint32 protected_but_lost;
  guint32 lost_but_recovered;
}FFRETuple;


static gdouble _get_ffre(FILE* sndp, FILE* fecp, FILE* rcvp, FILE* plyp){
  FFRETuple* ffre_tuple = g_malloc0(sizeof(FFRETuple));
  gchar line[1024];
  Packet* packet = g_malloc0(sizeof(Packet));
  GQueue* protected_but_lost;
  Packet fecpacket,played;
  gdouble result;
  LostTuple* lost_tuple = _get_lost_tuple(sndp, rcvp);
  GQueue* lost_packets = lost_tuple->lost_packets;
  Packet* lost_packet = g_queue_pop_head(lost_packets);

  g_free(lost_tuple);
  if(!lost_packet){
    g_queue_free(lost_packets);
    return 0.;
  }
  fgets(line, 1024, fecp);
  _setup_packet(&fecpacket, line);
  protected_but_lost = g_queue_new();

  while (lost_packet){
//    g_print("Lost packet: %hu  (%hu-%hu)\n", lost_packet->seq_num, fecpacket.protect_begin, fecpacket.protect_end);
    if(_cmp_seq(lost_packet->seq_num, fecpacket.protect_begin) < 0){
//      g_print("Not protected: %hu\n", lost_packet->seq_num);
      g_free(lost_packet);
      lost_packet = g_queue_pop_head(lost_packets);
      continue;
    }
    while(_cmp_seq(fecpacket.protect_end, lost_packet->seq_num) < 0 && fgets(line, 1024, fecp)){
      _setup_packet(&fecpacket, line);
    }
    g_queue_push_tail(protected_but_lost, lost_packet);
    lost_packet = g_queue_pop_head(lost_packets);
  }
  fgets(line, 1024, plyp);
  _setup_packet(&played, line);

  while(!g_queue_is_empty(protected_but_lost)){
    gint cmp;
    lost_packet = g_queue_pop_head(protected_but_lost);
    while((cmp = _cmp_seq(played.seq_num, lost_packet->seq_num)) < 0 && fgets(line, 1024, plyp)){
      _setup_packet(&played, line);
    }

    if(!cmp){
      ++ffre_tuple->lost_but_recovered;
    }else{
      ++ffre_tuple->protected_but_lost;
    }
    g_free(lost_packet);
  }


  if(!ffre_tuple->lost_but_recovered && !ffre_tuple->protected_but_lost){
    return 0.;
  }

  result = (gdouble)(ffre_tuple->lost_but_recovered) /
           (gdouble)(ffre_tuple->protected_but_lost + ffre_tuple->lost_but_recovered);
  g_free(ffre_tuple);
  return result;
}

typedef struct{
  GList*  items;
  GQueue* results;
}MedianTuple;

static GstClockTime _get_delay(PairedTimestamp* timestamps){
  return get_epoch_time_from_ntp_in_ns(timestamps->rcv_ntp) - get_epoch_time_from_ntp_in_ns(timestamps->snd_ntp);
}

static gint _paired_timestamp_cmp(PairedTimestamp* a, PairedTimestamp* b){
  GstClockTime delaya, delayb;
  delaya = _get_delay(a);
  delayb = _get_delay(b);
  if(delaya == delayb) return 0;
  return delaya < delayb ? -1 : 1;
}

static void _sw_add_paired_timestamp(MedianTuple* this, PairedTimestamp* paired_timestamp){
  this->items = g_list_insert_sorted(this->items, paired_timestamp, _paired_timestamp_cmp);
}

static void _sw_rem_paired_timestamp(MedianTuple* this, PairedTimestamp* paired_timestamp){
  this->items = g_list_remove(this->items, paired_timestamp);
}

static void _sw_paired_timestamp_sampler(MedianTuple* this){
  GstClockTime *value = g_malloc0(sizeof(GstClockTime));
  guint length = g_list_length(this->items);

  if(length % 2 == 1){
    *value = _get_delay(g_list_nth(this->items, length / 2)->data);
  }else{
    *value = _get_delay(g_list_nth(this->items, length / 2)->data) / 2;
    *value += _get_delay(g_list_nth(this->items, length / 2 + 1)->data) / 2;
  }
  g_queue_push_tail(this->results, value);
}

static GstClockTime _pair_timestamp_snd_in_ns(PairedTimestamp* paired_timestamp){
  return get_epoch_time_from_ntp_in_ns(paired_timestamp->snd_ntp);
}

static GQueue* _get_queue_delay_medians(FILE* snd, FILE* rcv){
  MedianTuple median_tuple = {NULL, g_queue_new()};
  SlidingWindow* sw = _make_sliding_window(
      _sw_add_paired_timestamp,
      _sw_rem_paired_timestamp,
      _sw_paired_timestamp_sampler,
      _pair_timestamp_snd_in_ns, &median_tuple, g_free);

  GQueue* queue_timestamps = _get_queue_timestamps(snd, rcv);

  while (!g_queue_is_empty(queue_timestamps)){
     _sliding_window_push(sw, g_queue_pop_head(queue_timestamps));
  }
  _free_sliding_window(sw);
  return median_tuple.results;
}

static void _paired_timestamps_to_delay_writer(FILE* fp, gpointer data){
  PairedTimestamp* paired_timestamp = data;
  GstClockTime delay = get_epoch_time_from_ntp_in_ns(paired_timestamp->rcv_ntp) - get_epoch_time_from_ntp_in_ns(paired_timestamp->snd_ntp);
  fprintf(fp, "%lu\n", delay / 1000 //in us
      );
}

static void _gst_clock_time_writer(FILE* fp, gpointer data){
  GstClockTime delay = *(GstClockTime*)data;
  fprintf(fp, "%lu\n", delay / 1000);
}

static void _guint32_writer(FILE* fp, gpointer data){
  fprintf(fp, "%u\n", *(guint32*)data);
}

static void _gdouble_writer(FILE* fp, gpointer data){
  fprintf(fp, "%1.3f\n", *(gdouble*)data);
}

static void _fwrite(FILE* fp, GQueue* queue, void (*writer)(FILE*,gpointer)){
  while(!g_queue_is_empty(queue)){
    writer(fp, g_queue_pop_head(queue));
  }
}

static void _tuple_fwrite(FILE* fp, QueueTuple* tuple){
  GQueue* q1,*q2;
  q1 = tuple->q1;
  q2 = tuple->q2;
  while(!g_queue_is_empty(q1) || !g_queue_is_empty(q2)){
    guint32 v1 = 0;
    guint32 v2 = 0;
    if(!g_queue_is_empty(q1)){
      v1 = *((guint32*)(g_queue_pop_head(q1)));
    }
    if(!g_queue_is_empty(q2)){
      v2 = *((guint32*)(g_queue_pop_head(q2)));
    }
    fprintf(fp, "%u,%u\n",v1,v2);
  }
}

static void _lost_tuple_fwrite(FILE* fp, LostTuple* tuple){
  gdouble fractional_lost = (gdouble)tuple->lost_packets_num / (gdouble)tuple->sent_packets_num;
  fprintf(fp, "%1.3f\n",fractional_lost);
}


int main (int argc, char **argv)
{
  FILE *fp;
  GQueue* sending_rates;
  GQueue* queue_delays;
  char * line = NULL;
  size_t group_num = 0,group_i,arg_i;
  guint32 bw_in_kbps;
  guint32 repeat_num,repeat_i;

  if(argc < 3){
  usage:
    g_print("Usage: ./program result_path [sr|qd]\n");
    g_print("sr snd_packets - accumulates the sending rate\n");
    g_print("qd snd_packets rcv_packets - calculates the queueing delays for packets\n");
    g_print("qmd snd_packets rcv_packets - calculates the median queueing delays for packets\n");
    g_print("gp ply_packets - calculates the goodput for packets\n");
    g_print("gp_avg ply_packets - calculates the average goodput for packets\n");
    g_print("fec_avg fec_packets - calculates the average fec rate for packets\n");
    g_print("tcprate tcpdump - calculates the tcp sending rates for tcpdump\n");
    g_print("lr snd_packets rcv_packets - calculates the loss rate for packets\n");
    g_print("ffre fec_packets snd_packets rcv_packets ply_packets - calculates the ffre\n");
    g_print("tfs snd_packets tcpdump - calculates traffic fair share\n");
    g_print("nlf snd_packets ply_packets - calculates the number of lost frames\n");
    g_print("ratio snd_packets_1 snd_packets_2 - calculates the ratio between the flows\n");
    g_print("disc ply_packets - calculates the discarded packets ratio\n");
    g_print("tcpstat tcpdump - calculates the tcp rate based on pcap\n");
    return 0;
  }

  fp = fopen (argv[1],"w");
  if(strcmp(argv[2], "sr") == 0){
       FILE* sndp;
       QueueTuple* tuple;
       if(argc < 3){
         goto usage;
       }
       sndp        = fopen (argv[3],"r");
       tuple = _get_sending_rates(sndp);
       _tuple_fwrite(fp, tuple);
       g_free(tuple);
       fclose(sndp);
   }else if(strcmp(argv[2], "qd") == 0){
     FILE* sndp;
     FILE* rcvp;
      if(argc < 4){
        goto usage;
      }
      sndp        = fopen (argv[3],"r");
      rcvp        = fopen (argv[4],"r");
      _fwrite(fp, _get_queue_timestamps(sndp, rcvp), _paired_timestamps_to_delay_writer);
      fclose(sndp);
      fclose(rcvp);
   }else if(strcmp(argv[2], "qmd") == 0){
     FILE* sndp;
     FILE* rcvp;
     if(argc < 4){
       goto usage;
     }
     sndp        = fopen (argv[3],"r");
     rcvp        = fopen (argv[4],"r");
     _fwrite(fp, _get_queue_delay_medians(sndp, rcvp), _gst_clock_time_writer);
     fclose(sndp);
     fclose(rcvp);
   }else if(strcmp(argv[2], "lr") == 0){
     FILE* sndp;
     FILE* rcvp;
     if(argc < 4){
       goto usage;
     }
     sndp        = fopen (argv[3],"r");
     rcvp        = fopen (argv[4],"r");
     _lost_tuple_fwrite(fp, _get_lost_tuple(sndp, rcvp));
     fclose(sndp);
     fclose(rcvp);
   }else if(strcmp(argv[2], "ffre") == 0){
     FILE* fecp;
     FILE* sndp;
     FILE* rcvp;
     FILE* plyp;
     if(argc < 6){
       goto usage;
     }
     fecp        = fopen (argv[3],"r");
     sndp        = fopen (argv[4],"r");
     rcvp        = fopen (argv[5],"r");
     plyp        = fopen (argv[6],"r");
     fprintf(fp, "%1.3f", _get_ffre(sndp, fecp, rcvp, plyp));
     fclose(fecp);
     fclose(sndp);
     fclose(rcvp);
     fclose(plyp);
   }else if(strcmp(argv[2], "gp") == 0){
     FILE* plyp;
     QueueTuple* tuple;
     if(argc < 3){
       goto usage;
     }
     plyp  = fopen (argv[3],"r");
     tuple = _get_goodput_rate(plyp);
     _fwrite(fp, tuple->q1, _guint32_writer);
//     g_queue_free_full(rates, g_free);
     fclose(plyp);
   }else if(strcmp(argv[2], "gp_avg") == 0 || strcmp(argv[2], "fec_avg") == 0){
     FILE* plyp;
     QueueTuple* tuple;
     GQueue* rates;
     gdouble* result = g_malloc0(sizeof(gdouble));
     gdouble len;
     if(argc < 3){
       goto usage;
     }
     plyp  = fopen (argv[3],"r");
     tuple = _get_goodput_rate(plyp);
     if(!strcmp(argv[2], "gp_avg")){
       rates = tuple->q1;
     }else{
       rates = tuple->q2;
     }
     len = g_queue_get_length(rates);
     while(!g_queue_is_empty(rates)){
       guint32* value = g_queue_pop_head(rates);
       *result += *value;
       g_free(value);
     }
     *result /= len;
     g_queue_push_tail(rates, result);
     _fwrite(fp, rates, _gdouble_writer);
//     g_queue_free_full(rates, g_free);
     fclose(plyp);
   }else if(strcmp(argv[2], "ratio") == 0){
     FILE* sndp;
     GQueue* ratios;
     if(argc < 3){
       goto usage;
     }
     sndp  = fopen (argv[3],"r");
     ratios = _get_subflow_ratio(sndp);
     _fwrite(fp, ratios, _gdouble_writer);
     fclose(sndp);
   }else if(strcmp(argv[2], "nlf") == 0){
     FILE* sndp;
     FILE* plyp;
     if(argc < 4){
       goto usage;
     }
     sndp  = fopen (argv[3],"r");
     plyp  = fopen (argv[4],"r");
     fprintf(fp, "%d\n", _get_lost_frame_nums(sndp, plyp));
     fclose(plyp);
     fclose(sndp);
   }else if(strcmp(argv[2], "disc") == 0){
     g_print("IMPLEMENT THIS!\n");
   }else if(strcmp(argv[2], "tcpstat") == 0){
     QueueTuple* tuple;
     if(argc < 3){
       goto usage;
     }
     tuple = tcpstat(argv[3]);
     _tuple_fwrite(fp, tuple);
     g_free(tuple);
   }else{
    goto usage;
  }

  fclose(fp);
  g_print("Results are made in %s\n", argv[1]);
  return 0;
}
