#!/bin/sh
# Baut das Paket (tools/build-deb.sh) und spielt es am N950 auf.
#
# Selbstgebaute Binäre starten unter aegis nur als root, wenn sie aus einem
# mit aegis-dpkg installierten Paket kommen -- deshalb der Umweg über das deb.
#
#   tools/deploy.sh            # Fassung aus VERSION oder 0.1.0
#   VERSION=0.2.0 tools/deploy.sh
set -e
cd "$(dirname "$0")/.."
VERSION=${VERSION:-${VER:-0.1.0}}
HOST=${N9_HOST:-192.168.1.8}
SSHOPT="-oHostKeyAlgorithms=+ssh-rsa -oPubkeyAcceptedAlgorithms=+ssh-rsa -i $HOME/.ssh/id_rsa_n9"
VERSION="$VERSION" sh tools/build-deb.sh >/dev/null
DEB="build/imira_${VERSION}_armel.deb"
scp $SSHOPT "$DEB" "user@$HOST:/home/user/" >/dev/null
TMP=$(mktemp)
# Das Gerät wird auch von anderen Sitzungen bespielt: auf das dpkg-Schloss
# warten, statt es wegzunehmen.
printf '%s\n' "set -e" \
    "i=0; while ps ax | grep -q '[d]pkg -i' && [ \$i -lt 60 ]; do sleep 2; i=\$((i+1)); done" \
    "aegis-dpkg -i /home/user/imira_${VERSION}_armel.deb 2>&1 | tail -2" \
    "ls -l /opt/imira/castd /opt/imira/bin/imira" > "$TMP"
scp $SSHOPT "$TMP" "user@$HOST:/home/user/inst.sh" >/dev/null
rm -f "$TMP"
ssh $SSHOPT "user@$HOST" 'sudo sh /home/user/inst.sh' 2>&1 \
    | grep -viE "warning|post-quantum|decrypt later|upgraded. See"
