./snd_pipeline --source=FILE:foreman_cif.yuv:1:352:288:2:25/1 --codec=VP8 --stat=triggered_stat:temp/snd_packets_2.csv:0 --sender=RTP:10.0.0.6:5002 --scheduler=SCREAM:RTP:5003 