./snd_pipeline --source=FILE:foreman_cif.yuv:1:352:288:2:25/1 --extradelay=100 --codec=VP8 --stat=100:1000:1:triggered_stat --statlogsink=FILE:temp/snd_statlogs3.csv --packetlogsink=FILE:temp/snd_packetlogs3.csv --sender=MPRTP:1:1:10.0.0.6:5004 --scheduler=MPRTPFRACTAL:MPRTP:1:1:5005 