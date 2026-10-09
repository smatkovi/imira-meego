#!/bin/sh
# Baut castd (C, GStreamer-0.10-Glue) zusammen mit Imiras TS-Packer und
# RTP-Sender (C++) fuer Harmattan (armel) auf dem Arch-Rechner.
set -e
cd "$(dirname "$0")/.."
HOST=${BUILD_HOST:-$(sh "$HOME/ps/nfsshift-sfos/tools/buildhost.sh")}
echo "== Baurechner: $HOST"
ssh "$HOST" 'mkdir -p /tmp/castd-src'
scp -q src/castd.c src/tswrap.cpp src/tswrap.h src/tsmux.cpp src/tsmux.h \
       src/rtpsender.cpp src/rtpsender.h src/orient.c src/orient.h \
       "$HOST:/tmp/castd-src/"
ssh "$HOST" 'set -e; cd /tmp/castd-src
  SR=$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim
  X=/tmp/xgcc-harmattan/bin/arm-none-linux-gnueabi
  COMMON="-O2 -Wall -march=armv7-a -mtune=cortex-a8 -mfpu=neon"
  GST="-I$SR/usr/include/gstreamer-0.10 -I$SR/usr/include/glib-2.0 \
       -I$SR/usr/lib/glib-2.0/include -I$SR/usr/include/libxml2 \
       -I$SR/usr/include/dbus-1.0 -I$SR/usr/lib/dbus-1.0/include"
  $X-gcc $COMMON -std=gnu99 $GST -c castd.c -o castd.o
  $X-gcc $COMMON -std=gnu99 $GST -c orient.c -o orient.o
  for f in tswrap tsmux rtpsender; do
    $X-g++ $COMMON -std=gnu++14 -c $f.cpp -o $f.o
  done
  # Die C++-Laufzeit kommt mit ins Programm: Harmattans libstdc++ ist die von
  # GCC 4.4 und kennt die Symbole von GCC 14 nicht.
  $X-g++ $COMMON -static-libstdc++ -static-libgcc \
      -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
      -o castd castd.o orient.o tswrap.o tsmux.o rtpsender.o \
      -lgstreamer-0.10 -lgstapp-0.10 -lgstbase-0.10 -lgobject-2.0 -lglib-2.0 \
      -ldbus-1 -lrt
  file castd'
mkdir -p build && scp -q "$HOST:/tmp/castd-src/castd" build/castd && ls -l build/castd
