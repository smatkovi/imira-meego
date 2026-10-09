#!/bin/sh
# Geht der Suchlauf jetzt durch? Abgesetzt, mit Wachhund.
LOG=/home/user/scan-test.log
: > $LOG
exec >>$LOG 2>&1
B=/opt/imira
CLI="$B/wpa_cli -p /var/run/wpa_supplicant_imira"
(
  sleep 140
  echo "=== WACHHUND ==="
  P=$(pidof wpa_supplicant); [ -n "$P" ] && kill $P; sleep 2
  P=$(pidof wpa_supplicant); [ -n "$P" ] && kill -9 $P
  /sbin/initctl start xsession/wlancond; sleep 2
  /sbin/initctl start xsession/icd2
  echo "=== WACHHUND fertig ==="
) >>$LOG 2>&1 &
date
/sbin/initctl stop xsession/icd2
/sbin/initctl stop xsession/wlancond
sleep 2
ifconfig wlan0 up
WPA_P2P_AS_STATION=1 $B/wpa_supplicant -Dnl80211 -iwlan0 \
    -c /home/user/wpa-p2p.conf -dd -f /home/user/wpa-scan.log -B
sleep 4
echo "=== 1. gewoehnlicher Suchlauf ==="
$CLI scan
sleep 8
$CLI scan_results
echo "=== 2. p2p_find (20 s) ==="
$CLI set wifi_display 1
$CLI wfd_subelem_set 0 0006001c440050
$CLI p2p_find
sleep 20
echo "--- Gegenstellen ---"; $CLI p2p_peers
echo "--- Suchergebnisse waehrend p2p_find ---"; $CLI scan_results
$CLI p2p_stop_find
echo "=== Fehler im Supplicant-Protokoll ==="
grep -c "Scan trigger failed" /home/user/wpa-scan.log
grep -iE "Zusatz-IEs weggelassen|Ratenmaske weggelassen|CTRL-EVENT-SCAN-RESULTS|SCAN-FAILED" /home/user/wpa-scan.log | tail -6
chmod 644 /home/user/wpa-scan.log
echo "=== fertig, Wachhund raeumt auf ==="
