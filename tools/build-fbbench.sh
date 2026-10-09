#!/bin/sh
# Baut src/fbbench.c fuer Harmattan (armel) gegen GStreamer 0.10 im MADDE-Sysroot.
set -e
cd "$(dirname "$0")/.."
HOST=${BUILD_HOST:-$(sh "$HOME/ps/nfsshift-sfos/tools/buildhost.sh")}
echo "== Baurechner: $HOST"
scp -q src/fbbench.c "$HOST:/tmp/fbbench.c"
ssh "$HOST" 'SR=$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim; \
  X=/tmp/xgcc-harmattan/bin/arm-none-linux-gnueabi-gcc; \
  $X -O2 -std=gnu99 -Wall -march=armv7-a -mtune=cortex-a8 \
     \
     \
     -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
     -o /tmp/fbbench /tmp/fbbench.c \
     -lrt \
  && file /tmp/fbbench'
mkdir -p build && scp -q "$HOST:/tmp/fbbench" build/fbbench && ls -l build/fbbench
