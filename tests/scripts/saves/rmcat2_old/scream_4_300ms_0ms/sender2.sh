./snd_pipeline --source=FILE:foreman_cif.yuv:1:352:288:2:25/1 --codec=VP8 --stat=100:1000:1:triggered_stat --statlogsink=FILE:temp/snd_statlogs2.csv --packetlogsink=FILE:temp/snd_packetlogs2.csv --sender=RTP:10.0.0.6:5002 --scheduler=SCREAM:RTP:5003 