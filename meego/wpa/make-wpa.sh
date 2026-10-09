#!/bin/bash
# Rebuild the already unpacked (and patched) tree in place.
set -e
W=/tmp/wpa-harmattan
export PATH=/tmp/xgcc-harmattan/bin:$PATH
CROSS=arm-none-linux-gnueabi
export CC=$CROSS-gcc AR=$CROSS-ar RANLIB=$CROSS-ranlib
cd $W/wpa_supplicant-2.11/wpa_supplicant
export CFLAGS="-O2 -I$W/nl3/include/libnl3"
export LDFLAGS="-L$W/nl3/lib -Wl,--dynamic-linker=/lib/ld-linux.so.3"
export LIBNL3_CFLAGS="-I$W/nl3/include/libnl3"
export LIBNL3_LIBS="-L$W/nl3/lib -lnl-genl-3 -lnl-3"
export PKG_CONFIG_PATH=$W/nl3/lib/pkgconfig
make -j8 wpa_supplicant wpa_cli 2>&1 | tail -8
$CROSS-strip wpa_supplicant wpa_cli
ls -l wpa_supplicant wpa_cli
