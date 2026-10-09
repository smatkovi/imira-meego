#!/bin/bash
# Cross-build wpa_supplicant for MeeGo Harmattan (armel), with P2P, WPS,
# Wi-Fi Display and a modern EAP stack. libnl is linked statically so the
# binary does not depend on the device's libnl-2.
set -e
W=/tmp/wpa-harmattan
TC=/tmp/xgcc-harmattan/bin
CROSS=arm-none-linux-gnueabi
export PATH=$TC:$PATH
export CC=$CROSS-gcc AR=$CROSS-ar RANLIB=$CROSS-ranlib LD=$CROSS-ld
cd $W

# --- libnl 3, static ------------------------------------------------------
if [ ! -f $W/nl3/lib/libnl-3.a ]; then
  [ -f libnl-3.11.0.tar.gz ] || curl -sSLO --max-time 300 \
    https://github.com/thom311/libnl/releases/download/libnl3_11_0/libnl-3.11.0.tar.gz
  rm -rf libnl-3.11.0 && tar xzf libnl-3.11.0.tar.gz
  cd libnl-3.11.0
  CFLAGS="-O2 -D_GNU_SOURCE -include time.h" ./configure --host=$CROSS --prefix=$W/nl3 --disable-shared --enable-static \
      --disable-cli --disable-pthreads >$W/libnl-conf.log 2>&1
  make -j8 >$W/libnl-make.log 2>&1
  make install >>$W/libnl-make.log 2>&1
  cd $W
fi
echo "libnl: $(ls -l $W/nl3/lib/libnl-3.a | awk '{print $5}') bytes"

# --- wpa_supplicant -------------------------------------------------------
rm -rf wpa_supplicant-2.11 && tar xzf wpa_supplicant-2.11.tar.gz
cd wpa_supplicant-2.11/wpa_supplicant
cat > .config <<CFG
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_CTRL_IFACE=y
CONFIG_BACKEND=file
CONFIG_P2P=y
CONFIG_WPS=y
CONFIG_WIFI_DISPLAY=y
CONFIG_AP=y
CONFIG_IEEE8021X_EAPOL=y
CONFIG_EAP_MD5=y
CONFIG_EAP_MSCHAPV2=y
CONFIG_EAP_TLS=y
CONFIG_EAP_PEAP=y
CONFIG_EAP_TTLS=y
CONFIG_EAP_GTC=y
CONFIG_EAP_OTP=y
CONFIG_EAP_LEAP=y
CONFIG_TLS=internal
CONFIG_INTERNAL_LIBTOMMATH=y
CONFIG_TLSV11=y
CONFIG_TLSV12=y
CONFIG_PEERKEY=y
CONFIG_DEBUG_FILE=y
CONFIG_IEEE80211N=y
CFG
export CFLAGS="-O2 -I$W/nl3/include/libnl3"
export LDFLAGS="-L$W/nl3/lib -Wl,--dynamic-linker=/lib/ld-linux.so.3"
export PKG_CONFIG_PATH=$W/nl3/lib/pkgconfig
export LIBNL3_CFLAGS="-I$W/nl3/include/libnl3"
export LIBNL3_LIBS="-L$W/nl3/lib -lnl-genl-3 -lnl-3"
make -j8 wpa_supplicant wpa_cli 2>&1 | tail -25
echo "=== result ==="
file wpa_supplicant wpa_cli
ls -l wpa_supplicant wpa_cli
$CROSS-readelf -d wpa_supplicant | grep NEEDED
