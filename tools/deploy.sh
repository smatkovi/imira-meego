#!/bin/sh
# Packt, was in build/ liegt, in ein deb und installiert es am N950.
# Selbstgebaute Binaere starten unter aegis nur als root, wenn sie aus einem
# mit aegis-dpkg installierten Paket kommen -- deshalb der Umweg ueber das deb.
set -e
cd "$(dirname "$0")/.."
VER=${VER:-0.2.1}
HOST=${N9_HOST:-192.168.1.8}
SSHOPT="-oHostKeyAlgorithms=+ssh-rsa -oPubkeyAcceptedAlgorithms=+ssh-rsa -i $HOME/.ssh/id_rsa_n9"
ST=$(mktemp -d)
mkdir -p "$ST/opt/imira" "$ST/DEBIAN"
cp build/fbcap "$ST/opt/imira/"
[ -f build/fbbench ] && cp build/fbbench "$ST/opt/imira/"
[ -f build/castd ] && cp build/castd "$ST/opt/imira/"
[ -f build/wpa_supplicant ] && cp build/wpa_supplicant build/wpa_cli "$ST/opt/imira/"
[ -f device/wfd-proto.py ] && cp device/wfd-proto.py "$ST/opt/imira/"
chmod 755 "$ST"/opt/imira/*
sed "s/@VERSION@/$VER/" meego/control.in > "$ST/DEBIAN/control"
python3 tools/mkdeb.py "$ST" "build/imira_${VER}_armel.deb"
scp $SSHOPT "build/imira_${VER}_armel.deb" "user@$HOST:/home/user/" >/dev/null
# Das Geraet wird auch von anderen Sitzungen bespielt: auf das dpkg-Schloss
# warten, statt es wegzunehmen.
printf '%s\n' "set -e" \
    "i=0; while ps ax | grep -q '[d]pkg -i' && [ \$i -lt 60 ]; do sleep 2; i=\$((i+1)); done" \
    "aegis-dpkg -i /home/user/imira_${VER}_armel.deb >/dev/null" \
    "ls -l /opt/imira/" > "$ST/inst.sh"
scp $SSHOPT "$ST/inst.sh" "user@$HOST:/home/user/inst.sh" >/dev/null
ssh $SSHOPT "user@$HOST" 'sudo sh /home/user/inst.sh' 2>&1 | grep -viE "warning|post-quantum|decrypt later|upgraded. See"
rm -rf "$ST"
