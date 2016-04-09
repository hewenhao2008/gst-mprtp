/*
 * test.h
 *
 *  Created on: Jan 26, 2016
 *      Author: balazs
 */


/* ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * +     +              10.0.0.0/24                   +     +
 * +     +--------------------------------------------+     +
 * +     +          .1     sync         .2            +     +
 * +     +     .----o------------------>¤ :5000       +     +
 * +  P  +     | TX |     async                       +  P  +
 * +  E  +     '----o------------------>¤ :5001       +  E  +
 * +  E  +                                            +  E  +
 * +  R  +                 sync                       +  R  +
 * +     +    :5002 ¤<------------------o----.        +     +
 * +  1  +                async         | RX |        +  2  +
 * +     +    :5003 ¤<------------------o----'        +     +
 * +     +                                            +     +
 * +     +--------------------------------------------+     +
 * +     +              10.0.0.1/24                   +     +
 * +     +--------------------------------------------+     +
 * +     +          .1     sync         .2            +     +
 * +     +     .----o------------------>¤ :5004       +     +
 * +  P  +     | TX |     async                       +  P  +
 * +  E  +     '----o------------------>¤ :5005       +  E  +
 * +  E  +                                            +  E  +
 * +  R  +                 sync                       +  R  +
 * +     +    :5006 ¤<------------------o----.        +     +
 * +  1  +                async         | RX |        +  2  +
 * +     +    :5007 ¤<------------------o----'        +     +
 * +     +                                            +     +
 * +     +--------------------------------------------+     +
 * +     +              10.0.0.2/24                   +     +
 * +     +--------------------------------------------+     +
 * +     +          .1     sync         .2            +     +
 * +     +     .----o------------------>¤ :5008       +     +
 * +  P  +     | TX |     async                       +  P  +
 * +  E  +     '----o------------------>¤ :5009       +  E  +
 * +  E  +                                            +  E  +
 * +  R  +                 sync                       +  R  +
 * +     +    :5010 ¤<------------------o----.        +     +
 * +  1  +                async         | RX |        +  2  +
 * +     +    :5011 ¤<------------------o----'        +     +
 * +     +                                            +     +
 * +     +--------------------------------------------+     +
 * +                                                        +
 * +                       RTCP                             +
 * +        TX o----------------------------->¤ :5013       +
 * +                       RTCP                             +
 * +     :5015 ¤<-----------------------------o RX          +
 * +                                                        +
 * ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 */
#ifndef TESTS_TEST_H_
#define TESTS_TEST_H_

typedef struct _MPRTPSubflowFBRACngCtrlerParams{
  //parameters can be changed
  gint32     max_rate;
  gint32     min_rate;

}MPRTPSubflowMARCCngCtrlerParams;

typedef struct _MPRTPSubflowMARCRateController{
  guint32 target_bitrate;
  gint8   state;
  guint32 discarded_bitrate;
  guint32 receiver_bitrate;
  guint32 sending_bitrate;

  MPRTPSubflowMARCCngCtrlerParams cngctrler;
}MPRTPSubflowFECBasedRateAdaption;

typedef union _MPRTPSubflowRateController{
  MPRTPSubflowFECBasedRateAdaption fbra_marc;
}MPRTPSubflowRateController;

typedef struct _MPRTPSubflowExtendedReport{
  guint32      total_discarded_bytes;
  GstClockTime owd_median;
  GstClockTime owd_min;
  GstClockTime owd_max;
}MPRTPSubflowExtendedReport;

typedef struct _MPRTPSubflowReceiverReport{
  GstClockTime RTT;
  guint32      jitter;
  gdouble      lost_rate;
  guint16      HSSN;
  guint16      cycle_num;
  guint32      cum_packet_lost;
}MPRTPSubflowReceiverReport;

typedef struct _MPRTPSubflowUtilizationSignalData{
  MPRTPSubflowReceiverReport receiver_report;
  MPRTPSubflowExtendedReport extended_report;
  MPRTPSubflowRateController ratectrler;
}MPRTPSubflowUtilizationSignalData;

typedef struct _MPRTPPluginSignalData{
  MPRTPSubflowUtilizationSignalData subflow[32];
}MPRTPPluginSignalData;




#include <string.h>

static gint32 info;
static int path1_tx_rtp_port    = 5000;
static int path1_tx_rtcp_port   = 5001;
static int path1_rx_rtp_port    = 5002;
static int path1_rx_rtcp_port   = 5003;

static int path2_tx_rtp_port    = 5004;
static int path2_tx_rtcp_port   = 5005;
static int path2_rx_rtp_port    = 5006;
static int path2_rx_rtcp_port   = 5007;

static int path3_tx_rtp_port    = 5008;
static int path3_tx_rtcp_port   = 5009;
static int path3_rx_rtp_port    = 5010;
static int path3_rx_rtcp_port   = 5011;

static int rtpbin_tx_rtcp_port  = 5013;
static int rtpbin_rx_rtcp_port  = 5015;
static gchar *logsdir           = NULL;
static gchar default_logsdir[255];

static gchar *yuvsrc_file        = NULL;
static gchar default_yuvsrc_file[255];
static int yuvsrc_width          = 352;
static int yuvsrc_height         = 288;

static gchar *path_1_rx_ip      = NULL;
static gchar default_path_1_rx_ip[255];
static gchar *path_2_rx_ip      = NULL;
static gchar default_path_2_rx_ip[255];
static gchar *path_3_rx_ip      = NULL;
static gchar default_path_3_rx_ip[255];

static gchar *path_1_tx_ip      = NULL;
static gchar default_path_1_tx_ip[255];
static gchar *path_2_tx_ip      = NULL;
static gchar default_path_2_tx_ip[255];
static gchar *path_3_tx_ip      = NULL;
static gchar default_path_3_tx_ip[255];

static int logging              = 1;
static int init_delay           = 0;
static int owd_th               = 1000;
static int discard_th           = 100;
static int lost_th              = 1000;
static int rtcp_interval_type   = 2;
static int report_timeout       = 0;
static int controlling_mode     = 2;
static int sending_target       = 200000;
static int path1_active         = 1;
static int path2_active         = 0;
static int path3_active         = 0;
static int fec_interval         = 0;
static int fec_min_window       = 5;
static int fec_max_window       = 20;
static int keep_alive_period    = 0;

static GOptionEntry entries[] =
{
    { "logsdir", 0, 0, G_OPTION_ARG_STRING, &logsdir, "Logsdir", NULL },
    { "yuvsrc_file", 0, 0, G_OPTION_ARG_STRING, &yuvsrc_file, "Yuvsrc file", NULL },
    { "yuvsrc_width", 0, 0, G_OPTION_ARG_INT, &yuvsrc_width, "Yuvsrc width", NULL },
    { "yuvsrc_height", 0, 0, G_OPTION_ARG_INT, &yuvsrc_height, "Yuvsrc width", NULL },
    { "path_1_rx_ip", 0, 0, G_OPTION_ARG_STRING, &path_1_rx_ip, "path_1_rx_ip", NULL },
    { "path_2_rx_ip", 0, 0, G_OPTION_ARG_STRING, &path_2_rx_ip, "path_2_rx_ip", NULL },
    { "path_3_rx_ip", 0, 0, G_OPTION_ARG_STRING, &path_3_rx_ip, "path_3_rx_ip", NULL },
    { "path_1_tx_ip", 0, 0, G_OPTION_ARG_STRING, &path_1_tx_ip, "path_1_tx_ip", NULL },
    { "path_2_tx_ip", 0, 0, G_OPTION_ARG_STRING, &path_2_tx_ip, "path_2_tx_ip", NULL },
    { "path_3_tx_ip", 0, 0, G_OPTION_ARG_STRING, &path_3_tx_ip, "path_3_tx_ip", NULL },
    { "path1_active", 0, 0, G_OPTION_ARG_INT, &path1_active, "path1_active", NULL },
    { "path2_active", 0, 0, G_OPTION_ARG_INT, &path2_active, "path2_active", NULL },
    { "path3_active", 0, 0, G_OPTION_ARG_INT, &path3_active, "path3_active", NULL },
    { "fec_interval", 0, 0, G_OPTION_ARG_INT, &fec_interval, "fec_interval", NULL },
    { "fec_min_window", 0, 0, G_OPTION_ARG_INT, &fec_min_window, "fec_min_window", NULL },
    { "fec_max_window", 0, 0, G_OPTION_ARG_INT, &fec_max_window, "fec_max_window", NULL },
    { "keep_alive_period", 0, 0, G_OPTION_ARG_INT, &keep_alive_period, "keep_alive_period", NULL },
    { "logging", 0, 0, G_OPTION_ARG_INT, &logging, "logging", NULL },
    { "init_delay", 0, 0, G_OPTION_ARG_INT, &init_delay, "init_delay", NULL },
    { "owd_th", 0, 0, G_OPTION_ARG_INT, &owd_th, "owd_th", NULL },
    { "discard_th", 0, 0, G_OPTION_ARG_INT, &discard_th, "discard_th", NULL },
    { "lost_th", 0, 0, G_OPTION_ARG_INT, &lost_th, "lost_th", NULL },
    { "rtcp_interval_type", 0, 0, G_OPTION_ARG_INT, &rtcp_interval_type, "rtcp_interval_type", NULL },
    { "report_timeout", 0, 0, G_OPTION_ARG_INT, &report_timeout, "report_timeout", NULL },
    { "controlling_mode", 0, 0, G_OPTION_ARG_INT, &controlling_mode, "controlling_mode", NULL },
    { "sending_target", 0, 0, G_OPTION_ARG_INT, &sending_target, "sending_target", NULL },
    { "path1_tx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path1_tx_rtp_port, "path1_tx_rtp_port", NULL },
    { "path1_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path1_tx_rtcp_port, "path1_tx_rtcp_port", NULL },
    { "path1_rx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path1_rx_rtp_port, "path1_rx_rtp_port", NULL },
    { "path1_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path1_rx_rtcp_port, "path1_rx_rtcp_port", NULL },

    { "path2_tx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path2_tx_rtp_port, "path2_tx_rtp_port", NULL },
    { "path2_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path2_tx_rtcp_port, "path2_tx_rtcp_port", NULL },
    { "path2_rx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path2_rx_rtp_port, "path2_rx_rtp_port", NULL },
    { "path2_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path2_rx_rtcp_port, "path2_rx_rtcp_port", NULL },

    { "path3_tx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path3_tx_rtp_port, "path3_tx_rtp_port", NULL },
    { "path3_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path3_tx_rtcp_port, "path3_tx_rtcp_port", NULL },
    { "path3_rx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path3_rx_rtp_port, "path3_rx_rtp_port", NULL },
    { "path3_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path3_rx_rtcp_port, "path3_rx_rtcp_port", NULL },

    { "rtpbin_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &rtpbin_tx_rtcp_port, "rtpbin_tx_rtcp_port", NULL },
    { "rtpbin_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &rtpbin_rx_rtcp_port, "rtpbin_rx_rtcp_port", NULL },

    { "info", 0, 0, G_OPTION_ARG_NONE, &info, "Info", NULL },
  { NULL }
};

static void _print_transfer_info(void)
{
  g_print("################### Network transfer port parameters ##############################\n");
  g_print("#                                                                                 #\n");
  g_print(" ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  g_print(" +     +                          10.0.0.0/24                               +     +\n");
  g_print(" +     +--------------------------------------------------------------------+     +\n");
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     +                               RTCP                                 +     +\n");
  g_print(" +     +                 TX o----------------------------->   %-13d +     +\n",rtpbin_tx_rtcp_port);
  g_print(" +     +                               RTCP                                 +     +\n");
  g_print(" +     +%19d  <-----------------------------o RX             +     +\n",rtpbin_rx_rtcp_port);
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     +--------------------------------------------------------------------+     +\n");
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     +                     .1      sync         .2                        +     +\n");
  g_print(" +     +                .----o------------------>  :%-18d      +     +\n",path1_tx_rtp_port);
  g_print(" +  P  +                | TX |       async                                  +  P  +\n");
  g_print(" +  E  +                '----o------------------>  :%-18d      +  E  +\n",path1_tx_rtcp_port);
  g_print(" +  E  +                                                                    +  E  +\n");
  g_print(" +  R  +                              sync                                  +  R  +\n");
  g_print(" +     +     %18d  <------------------o----.                  +     +\n",path1_rx_rtp_port);
  g_print(" +  1  +                              async         | RX |                  +  2  +\n");
  g_print(" +     +     %18d  <------------------o----'                  +     +\n",path1_rx_rtcp_port);
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++     +\n");
  g_print(" +     +                          10.0.1.0/24                               +     +\n");
  g_print(" +     +--------------------------------------------------------------------+     +\n");
  g_print(" +     +                     .1      sync         .2                        +     +\n");
  g_print(" +     +                .----o------------------>  :%-18d      +     +\n",path2_tx_rtp_port);
  g_print(" +  P  +                | TX |       async                                  +  P  +\n");
  g_print(" +  E  +                '----o------------------>  :%-18d      +  E  +\n",path2_tx_rtcp_port);
  g_print(" +  E  +                                                                    +  E  +\n");
  g_print(" +  R  +                              sync                                  +  R  +\n");
  g_print(" +     +     %18d  <------------------o----.                  +     +\n",path2_rx_rtp_port);
  g_print(" +  1  +                              async         | RX |                  +  2  +\n");
  g_print(" +     +     %18d  <------------------o----'                  +     +\n",path2_rx_rtcp_port);
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++     +\n");
  g_print(" +     +                          10.0.2.0/24                               +     +\n");
  g_print(" +     +--------------------------------------------------------------------+     +\n");
  g_print(" +     +                     .1      sync         .2                        +     +\n");
  g_print(" +     +                .----o------------------>   %-18d      +     +\n",path2_tx_rtp_port);
  g_print(" +  P  +                | TX |       async                                  +  P  +\n");
  g_print(" +  E  +                '----o------------------>   %-18d      +  E  +\n",path2_tx_rtcp_port);
  g_print(" +  E  +                                                                    +  E  +\n");
  g_print(" +  R  +                              sync                                  +  R  +\n");
  g_print(" +     +     %18d  <------------------o----.                  +     +\n",path2_rx_rtp_port);
  g_print(" +  1  +                              async         | RX |                  +  2  +\n");
  g_print(" +     +     %18d  <------------------o----'                  +     +\n",path2_rx_rtcp_port);
  g_print(" +     +                                                                    +     +\n");
  g_print(" ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  g_print("#                                                                                 #\n");
  g_print("# Example to setup a port: --path3_rx_rtcp_port=1234                              #\n");
  g_print("###################################################################################\n");
}


#define println(str) g_print(str"\n")

static void _print_info(void)
{
  println("[server|client] [--option=value]");
  println("--path[X]_[tx|rx]_[rtp|rtcp]_port\t "
          "Set up the port for subflow (X) at sender (tx) or receiver (rx) side for rtp or rtcp");
  println("--rtpbin_[rx|tx]_rtcp_port\tports for rtpbin");
  println("--path_[X]_active\tset the X path to join");
  println("--yuvsrc\tset the source of the yuv file");
  println("--path[X]_[rx|tx]_ip\tset the destination IP for subflow (X) at the receiver (rx) and at the sender (tx) side");
  println("--logging\tindicate weather the plugin do logging");
  println("--logsdir\tindicate where to store the logfiles");
  println("--owd_th\tone way delay window treshold at the receiver side");
  println("--discard_th\ta delay at misordering queue at the receiver side after the packet considered to be discarded by the mprtp plugin.");
  println("--lost_th\ta delay at discarded queue at the receiver side after the packet considered to be lost by the mprtp plugin.");
  println("--rtcp_interval_type\tset the rtcp reporting interval type. 0 - regular, 1 - early, 2 - immediate.");
  println("--report_timeout\tset the rtcp report timeout after the path considered to be passive at the sender side");
  println("--controlling_mode\tset the controlling mode of the sending rate for subflow or "
          "subflows at the sender side. 0 - no, 1 - no controlling but regular rtcp reports, 2 - fbra with marc");
  println("--reporting_mode\treporting mode at the receiver side. 0 - no, 1 - regular, 2 - fbra with marc");
  println("--fec_interval\t set the fec interval at the sender side");
  println("--fec_min_window\t set the fec_min_window at the receiver side");
  println("--fec_max_window\t set the fec_max_window at the receiver side");


}

static void _setup_test_params(void)
{

  if(path1_tx_rtp_port == 0){
    path1_tx_rtp_port  = 5000;
  }
  if(path1_tx_rtcp_port == 0){
      path1_tx_rtcp_port = 5001;
  }
  if(path1_rx_rtp_port == 0){
      path1_rx_rtp_port  = 5002;
  }
  if(path1_rx_rtcp_port == 0){
      path1_rx_rtcp_port = 5003;
  }


  if(path2_tx_rtp_port == 0){
    path2_tx_rtp_port  = 5004;
  }
  if(path2_tx_rtcp_port == 0){
    path2_tx_rtcp_port = 5005;
  }
  if(path2_rx_rtp_port == 0){
    path2_rx_rtp_port  = 5006;
  }
  if(path2_rx_rtcp_port == 0){
    path2_rx_rtcp_port = 5007;
  }

  if(path3_tx_rtp_port == 0){
    path3_tx_rtp_port  = 5008;
  }
  if(path3_tx_rtcp_port == 0){
    path3_tx_rtcp_port = 5009;
  }
  if(path3_rx_rtp_port == 0){
    path3_rx_rtp_port  = 5010;
  }
  if(path3_rx_rtcp_port == 0){
    path3_rx_rtcp_port = 5011;
  }

  if(rtpbin_tx_rtcp_port == 0){
      rtpbin_tx_rtcp_port  = 5013;
  }
  if(rtpbin_rx_rtcp_port == 0){
      rtpbin_rx_rtcp_port = 5015;
  }

  if(logsdir == NULL){
      sprintf(default_logsdir, "logs/");
      logsdir = default_logsdir;
    }

  if(yuvsrc_file == NULL){
      sprintf(default_yuvsrc_file, "foreman_cif.yuv");
      yuvsrc_file = default_yuvsrc_file;
    }

  if(path_1_rx_ip == NULL){
      sprintf(default_path_1_rx_ip, "10.0.0.1");
      path_1_rx_ip = default_path_1_rx_ip;
    }
  if(path_2_rx_ip == NULL){
      sprintf(default_path_2_rx_ip, "10.0.1.1");
      path_2_rx_ip = default_path_2_rx_ip;
    }
  if(path_3_rx_ip == NULL){
      sprintf(default_path_3_rx_ip, "10.0.2.1");
      path_3_rx_ip = default_path_3_rx_ip;
    }

  if(path_1_tx_ip == NULL){
      sprintf(default_path_1_tx_ip, "10.0.0.2");
      path_1_tx_ip = default_path_1_tx_ip;
    }
  if(path_2_tx_ip == NULL){
      sprintf(default_path_2_tx_ip, "10.0.1.2");
      path_2_tx_ip = default_path_2_tx_ip;
    }
  if(path_3_tx_ip == NULL){
      sprintf(default_path_3_tx_ip, "10.0.2.2");
      path_3_tx_ip = default_path_3_tx_ip;
    }

//  _print_transfer_info();

}



#endif /* TESTS_TEST_H_ */
