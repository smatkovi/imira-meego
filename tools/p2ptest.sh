#!/bin/sh
# Wi-Fi-Direct-Versuch am N950 -- abgesetzt, mit Wachhund.
#
# Nimmt wlan0 fuer 150 s von wlancond/icd2 weg, laesst den eigenen
# wpa_supplicant P2P-Gegenstellen suchen und gibt das Gerat danach dem
# System zurueck -- auch wenn die SSH-Sitzung mit dem WLAN wegbricht.
LOG=/home/user/p2p-test.log
: > $LOG
exec >>$LOG 2>&1
set -x
B=/opt/imira
CLI="$B/wpa_cli -p /var/run/wpa_supplicant_imira"

# --- Wachhund: stellt den Werkszustand wieder her -------------------------
(
  sleep 150
  echo "=== WACHHUND: Rueckbau ==="
  P=$(pidof wpa_supplicant); [ -n "$P" ] && kill $P
  sleep 2
  P=$(pidof wpa_supplicant); [ -n "$P" ] && kill -9 $P
  /sbin/initctl start xsession/wlancond
  sleep 2
  /sbin/initctl start xsession/icd2
  echo "=== WACHHUND: fertig ==="
) >>$LOG 2>&1 &

echo "=== vorher ==="
date; ip addr show wlan0 | head -3
/sbin/initctl stop xsession/icd2
/sbin/initctl stop xsession/wlancond
sleep 2
ifconfig wlan0 up
WPA_P2P_AS_STATION=1 $B/wpa_supplicant -Dnl80211 -iwlan0 \
    -c /home/user/wpa-p2p.conf -dd -f /home/user/wpa-sup.log -B
sleep 4
echo "=== status ==="
$CLI status
echo "=== P2P an, als Quelle (WFD source) anmelden ==="
$CLI set wifi_display 1
$CLI wfd_subelems 00000600001c440050
$CLI p2p_find
sleep 30
echo "=== Gegenstellen ==="
$CLI p2p_peers
for m in $($CLI p2p_peers); do $CLI p2p_peer $m; done
$CLI p2p_stop_find
echo "=== scan nach DIRECT-Netzen ==="
$CLI scan; sleep 6; $CLI scan_results
echo "=== Ende, Wachhund raeumt auf ==="
