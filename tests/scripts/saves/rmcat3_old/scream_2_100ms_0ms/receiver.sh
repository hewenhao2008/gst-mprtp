./rcv_pipeline --sink=FAKESINK --codec=VP8 --stat=100:1000:1:triggered_stat --statlogsink=FILE:temp/rcv_statlogs.csv --packetlogsink=FILE:temp/rcv_packetlogs.csv --receiver=RTP:5100 --playouter=SCREAM:RTP:10.0.0.1:5101 