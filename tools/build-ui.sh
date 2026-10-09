#!/bin/sh
# Baut die Oberfläche (Qt 4.7 + com.nokia.meego) für Harmattan.
#
# Gleiches Rezept wie die anderen MeeGo-Portierungen: GCC-14-Cross gegen das
# MADDE-Sysroot, moc aus dem Qt des Simulators (dasselbe 4.7.4). Die moderne
# C++-Laufzeit bleibt im Programm (-static-libstdc++), damit sie nicht in das
# mit GCC 4.4 gebaute Qt hineinregiert.
set -e
cd "$(dirname "$0")/.."
HOST=${BUILD_HOST:-$(sh "$HOME/ps/nfsshift-sfos/tools/buildhost.sh")}
echo "== Baurechner: $HOST"
ssh "$HOST" 'rm -rf /tmp/imira-ui && mkdir -p /tmp/imira-ui'
scp -q ui/main.cpp ui/caster.cpp ui/caster.h "$HOST:/tmp/imira-ui/"
ssh "$HOST" 'set -e; cd /tmp/imira-ui
  SYSROOT=$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim
  XGCC=/tmp/xgcc-harmattan
  SIMQT=$HOME/QtSDK/Simulator/Qt/gcc
  CXX=$XGCC/bin/arm-none-linux-gnueabi-g++
  MOC=$SIMQT/bin/moc
  QTINC=$SYSROOT/usr/include/qt4
  CXXFLAGS="--sysroot=$SYSROOT -std=gnu++17 -O2 -Wall -Wno-deprecated-declarations \
    -DQT_NO_DEBUG -I$QTINC -I."
  for m in QtCore QtGui QtDeclarative QtNetwork QtDBus; do CXXFLAGS="$CXXFLAGS -I$QTINC/$m"; done
  LDFLAGS="--sysroot=$SYSROOT -static-libstdc++ -static-libgcc -Wl,-O1 \
    -Wl,--as-needed -Wl,--exclude-libs,ALL -Wl,--dynamic-linker=/lib/ld-linux.so.3"
  $MOC caster.h -o moc_caster.cpp
  $CXX $CXXFLAGS -c moc_caster.cpp -o moc_caster.o
  $CXX $CXXFLAGS -c caster.cpp -o caster.o
  $CXX $CXXFLAGS -c main.cpp -o main.o
  $CXX $LDFLAGS -o imira main.o caster.o moc_caster.o \
      -lQtDeclarative -lQtDBus -lQtGui -lQtCore -lpthread
  $XGCC/bin/arm-none-linux-gnueabi-strip imira
  file imira; ls -l imira'
mkdir -p build
scp -q "$HOST:/tmp/imira-ui/imira" build/imira
ls -l build/imira
