# Nach den Funkversuchen wieder in den Werkszustand bringen.
set -x
# 1. nichts Eigenes darf mehr laufen
P=$(pidof wpa_supplicant); [ -n "$P" ] && kill -9 $P
rm -rf /var/run/wpa_supplicant_imira
# 2. Dienste sauber neu starten (icd2 zuerst stoppen, dann beide hoch)
/sbin/initctl stop xsession/icd2 2>/dev/null
/sbin/initctl stop xsession/wlancond 2>/dev/null
sleep 1
# 3. Schnittstelle in einen frischen Zustand
ifconfig wlan0 down 2>/dev/null
sleep 1
ifconfig wlan0 up
sleep 1
/sbin/initctl start xsession/wlancond
sleep 3
/sbin/initctl start xsession/icd2
sleep 4
echo "--- Lage ---"
/sbin/initctl list | grep -E "icd2|wlancond"
pidof wpa_supplicant || echo "kein fremder Supplicant"
ip addr show wlan0 | head -4
