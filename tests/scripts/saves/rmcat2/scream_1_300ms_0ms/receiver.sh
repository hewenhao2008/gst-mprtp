./rcv_pipeline --sink=FAKESINK --codec=VP8 --stat=triggered_stat:temp/rcv_packets_1.csv:0 --plystat=triggered_stat:temp/ply_packets_1.csv:0 --receiver=RTP:5000 --playouter=SCREAM:RTP:10.0.0.1:5001 