./snd_pipeline --source=FILE:foreman_cif.yuv:1:352:288:2:25/1 --codec=VP8 --stat=100:1000:1:triggered_stat --statlogsink=FILE:temp/snd_statlogs2.csv --packetlogsink=FILE:temp/snd_packetlogs2.csv --sender=MPRTP:1:1:10.0.0.1:5002 --scheduler=MPRTPFRACTAL:MPRTP:1:1:5003 