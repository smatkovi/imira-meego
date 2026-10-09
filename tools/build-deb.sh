#!/bin/sh
# Packt alles zu einem Harmattan-deb: Oberfläche, Technik, Startereintrag,
# Symbol. mkdeb.py schreibt das Paket selbst (Harmattans dpkg 1.15 will
# gzip-Glieder und keine Schrägstriche an den ar-Namen).
#
#   tools/build-deb.sh            # -> build/imira_<fassung>_armel.deb
#   VERSION=0.2.0 tools/build-deb.sh
set -e
cd "$(dirname "$0")/.."
VERSION=${VERSION:-0.1.0}
OUT=build
STAGE=$OUT/stage
[ -x $OUT/imira ] || { echo "Oberfläche fehlt: $OUT/imira (tools/build-ui.sh)" >&2; exit 1; }
[ -x $OUT/castd ] || { echo "castd fehlt (tools/build-castd.sh)" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/opt/imira/bin" "$STAGE/opt/imira/qml" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/icons/hicolor/80x80/apps" \
         "$STAGE/usr/share/themes/base/meegotouch/icons" \
         "$STAGE/usr/share/doc/imira"

cp $OUT/imira "$STAGE/opt/imira/bin/imira"
for f in castd fbcap fbbench wpa_supplicant wpa_cli; do
    [ -f $OUT/$f ] && cp $OUT/$f "$STAGE/opt/imira/$f"
done
cp device/wfd-proto.py "$STAGE/opt/imira/"
cp ui/qml/*.qml "$STAGE/opt/imira/qml/"
cp ui/icons/icon-80.png "$STAGE/usr/share/icons/hicolor/80x80/apps/imira.png"
cp ui/icons/icon-80.png "$STAGE/usr/share/themes/base/meegotouch/icons/imira-80.png"
cp meego/imira.desktop "$STAGE/usr/share/applications/imira.desktop"
[ -f LICENSE ] && cp LICENSE "$STAGE/usr/share/doc/imira/copyright"

find "$STAGE" -type f -exec chmod 644 {} +
chmod 755 "$STAGE/opt/imira/bin/imira" "$STAGE/opt/imira/wfd-proto.py"
for f in castd fbcap fbbench wpa_supplicant wpa_cli; do
    [ -f "$STAGE/opt/imira/$f" ] && chmod 755 "$STAGE/opt/imira/$f"
done
find "$STAGE" -type d -exec chmod 755 {} +

VERSION="$VERSION" ICON=ui/icons/icon-64.png python3 - meego/control.in "$STAGE/DEBIAN/control" <<'PY'
import base64, os, sys, textwrap
src, dst = sys.argv[1], sys.argv[2]
ctl = open(src, encoding="utf-8").read()
b64 = base64.b64encode(open(os.environ["ICON"], "rb").read()).decode("ascii")
icon = "\n".join(" " + line for line in textwrap.wrap(b64, 76))
ctl = ctl.replace("@VERSION@", os.environ["VERSION"]).replace("@ICON@", icon)
open(dst, "w", encoding="utf-8").write(ctl)
PY

DEB="$OUT/imira_${VERSION}_armel.deb"
python3 tools/mkdeb.py "$STAGE" "$DEB"
echo "== $DEB"
