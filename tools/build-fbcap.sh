#!/bin/sh
# Baut src/fbcap.c fuer Harmattan (armel) auf dem Arch-Rechner.
set -e
cd "$(dirname "$0")/.."
HOST=${BUILD_HOST:-$(sh "$HOME/ps/nfsshift-sfos/tools/buildhost.sh")}
echo "== Baurechner: $HOST"
scp -q src/fbcap.c "$HOST:/tmp/fbcap.c"
ssh "$HOST" 'X=/tmp/xgcc-harmattan/bin/arm-none-linux-gnueabi-gcc; \
  $X -O2 -std=gnu99 -Wall -march=armv7-a -mtune=cortex-a8 \
     -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
     -o /tmp/fbcap /tmp/fbcap.c -lrt && file /tmp/fbcap && ls -l /tmp/fbcap'
mkdir -p build
scp -q "$HOST:/tmp/fbcap" build/fbcap
ls -l build/fbcap
