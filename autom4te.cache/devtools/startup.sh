#!/bin/bash
sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv 7F0CEB10
echo 'deb http://downloads-distro.mongodb.org/repo/ubuntu-upstart dist 10gen' | sudo tee /etc/apt/sources.list.d/mongodb.list
sudo apt-get update
# sudo apt-get install gtk-doc-tools autoconf autopoint liborc-0.4-* libalsa* libopus-dev libvpx-dev libgdk-pixbuf2.0-dev libx264-dev libjpeg-dev libpulse-dev libwavpack-dev libcdparanoia-dev libvisual-0.4-dev libpango1.0-dev libtool make git curl mongodb-org bison flex libglib2.0-dev libtheora-dev libvorbis-dev g++ libpng-dev libflac-dev libspeex-dev yasm nettle-dev libgcrypt-dev libmp3lame-dev python3 python3-dev python-gobject-dev -y

sudo apt-get install git curl g++ autoconf autopoint gtk-doc-tools make bison flex libpcap-dev yasm nettle-dev libgcrypt-dev libmp3lame-dev python3 python3-dev python-gobject-dev -y
sudo apt-get build-dep gstreamer1.0-plugins-{base,good,bad,ugly} -y
# curl -sL https://deb.nodesource.com/setup | sudo bash -

mkdir gst-sources
cd gst-sources

libtoolize


# set -x
for TARGET in gstreamer gst-plugins-base gst-plugins-good gst-libav gst-plugins-bad gst-plugins-ugly
do
  git clone http://github.com/gstreamer/$TARGET
  cd $TARGET
  ./autogen.sh
  sudo make install
  cd ..
done

mkdir /home/ubuntu/gst/dev
sudo mount -t vboxsf -o uid=$UID,gid=$(id -g) home_gst_dev /home/ubuntu/gst/dev
