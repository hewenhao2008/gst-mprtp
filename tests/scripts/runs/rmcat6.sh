#!/bin/bash
programname=$0

NSSND="ns_snd"
NSRCV="ns_rcv"
LOGSDIR="temp"
SCRIPTSDIR="scripts"
CONFDIR=$SCRIPTSDIR"/configs"
TEMPDIR=$SCRIPTSDIR"/temp"
SYNCTOUCHFILE="triggered_stat"

mkdir $LOGSDIR

#setup defaults
DURATION=120
OWD_RCV=100
JITTER=0

if [ -z "$1" ] 
then
  OWD_SND=50
else 
  OWD_SND=$1
fi


sudo ip netns exec ns_mid tc qdisc change dev veth2 root handle 1: netem delay "$OWD_SND"ms "$JITTER"ms
sudo ip netns exec ns_mid tc qdisc change dev veth1 root handle 1: netem delay "$OWD_RCV"ms "$JITTER"ms

echo "sudo tcpdump -n tcp -ni veth2 -s0 -w $LOGSDIR/tcps.pcap &" > $LOGSDIR"/tcpdump.sh"
chmod 777 $LOGSDIR"/tcpdump.sh"

echo "./bcex $CONFDIR/rmcat6.cmds " > $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw.csv 1 3500 3000" >> $LOGSDIR"/ntrt.sh"
echo "./$SCRIPTSDIR/runs/postproc/rmcat6.sh" >> $LOGSDIR"/ntrt.sh"
chmod 777 $LOGSDIR"/ntrt.sh"


#Lets Rock
touch $SYNCTOUCHFILE

sudo ip netns exec ns_mid $LOGSDIR"/tcpdump.sh"
sudo ip netns exec ns_snd $LOGSDIR"/ntrt.sh" 
 


