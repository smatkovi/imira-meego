# Imira auf MeeGo/Harmattan — Machbarkeit (gemessen 09.10.2026, N950 RM680)

Quelle: https://github.com/JimKnopfIoT/harbour-imira (0.10.10, GPL-3.0-or-later),
Miracast-**Quelle** fuer Sailfish. ~9700 Zeilen, davon die Oberflaeche (6 QML-Seiten,
Silica) nur ein Zehntel; der Rest ist Plattformtechnik.

## Was am Geraet gemessen wurde

| Stueck | Befund |
|---|---|
| H.264-Kodierer in Hardware | **da.** `dsph264enc` (DSP, Firmware `h264venc_sn.dll64P`): 848x480 150 Bilder in 5,14 s (~29/s), 640x480 ~31/s, `dsphdh264enc` 1280x720 ~23/s — und das **ohne CPU** (0,3 s Benutzerzeit fuer 150 Bilder). |
| Bildschirm abgreifen | `/dev/fb0` lesbar (nur root, Gruppe `video`), 856x1536 RGB565 = doppelt gepuffert. **Leserate 25,8 MB/s** → ein Bild (822 kB) kostet 33 ms, also ~15 Bilder/s realistisch, wenn noch RGB565→I420 dazukommt. Kein Lipstick-Recorder, kein Wayland — `ximagesrc` gibt es auch. |
| Ton | PulseAudio ist da, `pulse-access` hat der Benutzer, aber `pacmd`/`pactl` fehlen; ob die Nokia-Policy eine Monitor-Aufnahme zulaesst, ist **ungemessen**. |
| Python fuer den RTSP-Handschlag (M1–M8) | 2.6.6 **und 3.11** am Geraet. |
| **Wi-Fi Direct (P2P)** | **nein.** nl80211 sagt (nachgemessen, Familie 17, phy0, wl12xx): Schnittstellenarten nur **ADHOC, STATION, MONITOR** — kein AP, kein P2P_GO, kein P2P_CLIENT. Firmware ist einrollig (`/lib/firmware/wl1271-fw.bin`, kein `-fw-ap`). Vorhanden sind `FRAME` (mgmt_tx), `REMAIN_ON_CHANNEL`, `NEW_BEACON`, `NEW_STATION` als Befehle, aber ohne AP-Rolle in der Firmware nuetzt das fuer eine eigene Gruppe nichts. |
| Konvergenz-Schreibtisch (eigener Wayland-Compositor, 1348 Zeilen) | auf X11/Harmattan **nicht portierbar**, faellt weg. |

## Folgerung

Das Telefon kann **nie Gruppeninhaber (GO)** sein — damit ist der Weg, den Imira
auf Sailfish geht (eigenes P2P-wpa_supplicant, Telefon als GO), hier zu.
Zwei Wege bleiben:

1. **Empfaenger als Gruppeninhaber / als normales WLAN** — viele Dongles spannen
   ein eigenes Netz auf (SSID + Passwort am Bildschirm) oder sind autonomer GO.
   Dann reicht die Harmattan-Bordtechnik zum Verbinden, und die App macht nur
   noch den WFD-RTSP-Handschlag (Port 7236) + MPEG-TS/RTP. **Das ist baubar.**
2. **P2P-Client von Hand** (Aktionsrahmen per mgmt_tx + remain_on_channel auf
   der STATION-Schnittstelle, P2P/WFD-IEs selbst gebaut, WPS zum
   Zugangsdatenholen) — Forschung, hoher Aufwand, Ausgang offen. wpa_supplicant
   schaltet P2P ohne die P2P-Schnittstellenarten gar nicht ein, also eigener Code.

Wiederverwendbar aus dem Quellbaum: `daemon/src/tsmux.cpp` (867 Z., MPEG-TS),
`rtpsender.cpp`, `convert.cpp`, `device/imira-wfd-proto.py` (334 Z., RTSP M1–M8),
die Oberflaeche. Neu zu bauen: Aufnahme (fb0 statt Lipstick), Kodierer (gst-dsp
statt droidmedia), Dienst (Upstart statt systemd/polkit).

**Offen und entscheidend: welcher Empfaenger steht zum Pruefen bereit?**

## 09.10.2026, zweiter Durchgang: P2P-Fähigkeiten der STATION-Schnittstelle

Gemessen werden sollte, ob ein selbstgebauter P2P-Client auf der vorhandenen
STATION-Schnittstelle überhaupt arbeiten kann: zweite virtuelle Schnittstelle,
`REGISTER_FRAME`, `REMAIN_ON_CHANNEL` (2412 MHz, 150 ms), `FRAME` (mgmt_tx
eines P2P-Public-Action-Rahmens).

**Der Lauf hat dem N950 das WLAN weggenommen** — nach dem Start der Messung war
das Gerät nicht mehr im Netz (voller /24-Scan, drei Minuten lang nichts). Das
Protokoll liegt am Gerät unter `/home/user/p2pcap.log` und wird gelesen, sobald
das WLAN wieder läuft. Dass die Firmware bei Off-Channel-Betrieb aussteigt, ist
selbst ein Befund: die einrollige STA-Firmware ist für diesen Weg wackelig.

## Dritter, vielversprechenderer Weg (noch nicht gemessen)

Statt P2P von Hand auf der alten Firmware: **Treiber und Firmware erneuern.**
`wl12xx` aus den Backports (compat-wireless unterstützt 2.6.32) gegen die
Harmattan-Kernelquellen bauen, dazu TIs **mehrrollige** Firmware
(`wl127x-fw-5-mr.bin` aus linux-firmware/ti-connectivity) — damit bekäme das
Gerät AP-Rolle und echte P2P-Schnittstellenarten, und Imiras Originalaufbau
(Telefon als Gruppeninhaber) wäre tragfähig. Offene Fragen: Plattformdaten des
Nokia-Board-Codes, `insmod` fremder Module unter aegis, SDIO (N9) vs SPI (N950).

## 09.10.2026, dritter Durchgang: eigener wpa_supplicant am Gerät

Gebaut: **wpa_supplicant 2.11 für armel** (GCC-14-Cross gegen das MADDE-Sysroot,
libnl 3.11 statisch dazu, `-Wl,--dynamic-linker=/lib/ld-linux.so.3`), mit P2P,
WPS, Wi-Fi Display, EAP (PEAP/TTLS/TLS) und interner TLS-Technik bis TLS 1.2.
Braucht am Gerät nur libc, librt, libgcc. Rezept: `meego/wpa/build-wpa.sh`.

**Eingriff** (`meego/wpa/patch-p2p.py`, nur aktiv wenn `WPA_P2P_AS_STATION=1`):
wpa_supplicant schaltet P2P ohne die P2P-Schnittstellenarten gar nicht ein.
Der Eingriff setzt die Fähigkeit trotzdem und bildet `P2P_CLIENT`/`P2P_DEVICE`
auf `STATION` ab. Nachgemessen am Gerät: P2P läuft damit an
(`nl80211: P2P forced on without P2P iftypes`, eigener Listen-Kanal 81:11,
`p2p_find` sagt OK).

**Aegis:** ein selbstgebautes Binär startet als root nicht
("Operation not permitted", im dmesg `source origin check`). Es muss in einem
deb stecken, das mit `aegis-dpkg -i` installiert wurde — danach läuft es. Das
Paket `imira-wpa` (tools/mkdeb.py) tut genau das, nach /opt/imira.

**Wachhund bewährt:** der Versuch nimmt wlan0 per
`initctl stop xsession/{icd2,wlancond}`, und ein abgesetzter Zeitgeber gibt es
nach 150 s zurück. Das Gerät war danach von selbst wieder im Heimnetz.

### Zwei harte Grenzen des Treibers (nachgemessen)

1. `REGISTER_FRAME` für **Probe Requests** (type 0x40) → EINVAL. Die
   STATION-Rolle darf keine Probe Requests beantworten, das Gerät kann also im
   P2P-Sinn nicht *gefunden* werden.
2. **`max scan IE len: 0`** (und `max scan SSIDs: 1`). Der Treiber trägt
   *keine* Zusatz-IEs in Probe Requests. Jede P2P-Suche von wpa_supplicant
   hängt WPS- und P2P-IEs an (hier 148 Byte) → `Scan trigger failed: EINVAL`.
   **Damit ist die normale P2P-Geräteentdeckung auf diesem Gerät nicht
   machbar** — kein Eingriff in wpa_supplicant ändert das, die Grenze sitzt im
   Treiber.

### Was dadurch bleibt (der Weg zum Dongle)

Aktionsrahmen (`mgmt_tx`, off-channel) und `remain_on_channel` gehen. Also:

1. **gewöhnlicher Scan** (ohne IEs) findet die Gruppe des Dongles als
   `DIRECT-xx-...`-Netz; dessen Beacon enthält die P2P-Kennung, die wir selbst
   auslesen können.
2. **Beitritt als normaler Client**: WPS-PBC als Anmelder (die IEs der
   Anmeldung sind eine andere Grenze als die des Scans) oder mit dem Passwort,
   das viele Dongles anzeigen.
3. Falls der Dongle eine Einladung braucht: GO-Negotiation/Provision-Discovery
   als Aktionsrahmen direkt an seine Kennung — das kann der Treiber.
4. Danach ist alles **nur noch IP**: WFD-RTSP auf Port 7236 + MPEG-TS/RTP.

Offen bleibt, was nur ein echter Dongle beantwortet: beacont er eine
beitretbare Gruppe, und nimmt er WPS-PBC von einem gewöhnlichen Client an.
Die Bildstrecke selbst (Abgriff → DSP-Kodierer → TS/RTP) lässt sich ohne
Dongle über das Heimnetz gegen eine Senke am Arch-Rechner prüfen.

## 09.10.2026, vierter Durchgang: die Bildstrecke läuft

**Erreicht:** Bildschirm des N950 → gedreht → DSP-H.264 → RTP über das
Heimnetz → Arch-Rechner, dort dekodiert und als Einzelbild geprüft (848x480,
Baseline, Text lesbar, Lage richtig). Dafür gibt es `src/castd.c` (ein
Programm, kein Pipe-Gebastel) und `src/fbcap.c` (Abgriff allein, für Proben).

**Vier Fallen, alle nachgemessen:**

1. **aegis:** ein selbstgebautes Binär startet als root nur, wenn es aus einem
   mit `aegis-dpkg -i` installierten deb kommt. `setgid video` hilft **nicht**
   (aegis legt es ab) — `/dev/fb0` braucht also wirklich root.
2. **aegis lässt `gst-launch-0.10` als root nicht laufen.** Deshalb öffnet
   `castd` den Framebuffer als root und **legt die Rechte ab** (`setuid` auf
   den Benutzer), bevor GStreamer anfängt. Beides in einem Programm.
3. **Der DSP kann nur Puffer abbilden, die sein eigenes Element vergibt.**
   `fdsrc`/`filesrc`-auf-FIFO liefert gewöhnlichen Heap → `got DSP MMUFAULT`
   bzw. `dsp_thread: failed waiting for events: 5`. Abhilfe: **`ffmpegcolorspace`
   davor** — dessen Ausgabepuffer holt es vom Kodierer. Deshalb gibt `castd`
   gedrehtes **RGB565** aus und lässt GStreamer nach I420 wandeln.
4. **Pipes liefern 64-KB-Häppchen** (PIPE_BUF, und `F_SETPIPE_SZ` gibt es im
   2.6.32 noch nicht), nie ganze Bilder. `appsrc` schiebt ganze Bilder.

**Leistung (nachgemessen, 848x480):** Ziel 25/s → 125 Bilder in 13,1 s, also
**~9,5 Bilder/s bei 85 % einer CPU**. Der Engpass ist das Drehen (spaltenweise
Zugriffe auf den Framebuffer, 1712 B Schrittweite) plus die Farbumrechnung.
Offene Hebel, in dieser Reihenfolge:
  * drehen in Kacheln (16x16) statt spaltenweise — Zwischenspeicher-freundlich;
  * den I420-Puffer per `gst_pad_alloc_buffer` direkt vom Kodierer holen und
    selbst hineinschreiben, dann fällt `ffmpegcolorspace` ganz weg;
  * **`dspvpp`** (DSP VPP filter, ist am Gerät vorhanden) — Farbe, Drehung und
    Skalierung auf dem DSP statt auf der CPU.

**RTP-Empfang:** der Strom des `dsph264enc` ist Annex-B (`00 00 00 01 67 ...`).
Auf der Empfangsseite braucht es `rtph264depay !
video/x-h264,stream-format=byte-stream,alignment=au`, sonst liegt im Datei
etwas, das kein Dekoder anfasst.

**Werkzeuge im Baum:** `tools/build-castd.sh`, `tools/build-fbcap.sh`,
`tools/deploy.sh` (baut das deb und installiert es per `aegis-dpkg`),
`tools/mkdeb.py`, `meego/wpa/*` (Supplicant-Rezept und Eingriff).

## 09.10.2026, fünfter Durchgang: Tempo

`src/fbbench.c` misst, wo die Zeit hingeht (848x480, N950):

| Weg | je Bild |
|---|---|
| sichtbare Seite am Stück in den Arbeitsspeicher | **2,9 ms** (281 MB/s) |
| direkt aus dem Framebuffer drehen (castd bis 0.3.x) | **66,5 ms** |
| kopieren, dann drehen | 14,8 ms |
| kopieren, dann in 32er-Kacheln drehen | **11,7 ms** |
| kopieren, drehen und gleich nach I420 | 27,1 ms |

Der Framebuffer ist also gar nicht langsam — die 25,8 MB/s der ersten Messung
waren der `read()`-Weg von `dd`; über `mmap` sind es 281 MB/s. Teuer war das
spaltenweise Drehen **im** Framebuffer.

Eingebaut (castd 0.4.x): erst kopieren, dann in Kacheln drehen. Ergebnis:

* 848x480: **9,5 → 24,1 Bilder/s**, dabei 99 % einer CPU (gesättigt)
* 640x360: **29,1 Bilder/s** bei 77 % einer CPU

Nächste Hebel, in dieser Reihenfolge: den I420-Puffer per
`gst_pad_alloc_buffer` vom Kodierer holen und selbst füllen (dann fällt
`ffmpegcolorspace` samt einer vollen Kopie weg), danach `dspvpp` — Drehen und
Wandeln auf dem DSP.

## 09.10.2026, sechster Durchgang: Suchlauf geht jetzt durch

Zweiter Eingriff `meego/wpa/patch-scan.py` (wieder nur mit
`WPA_P2P_AS_STATION=1`): keine Zusatz-IEs im Scan, höchstens eine SSID, keine
`SCAN_FLAGS`, keine Ratenmaske bei `p2p_probe`.

Gemessen am N950: **`Scan trigger failed` kommt null Mal** (vorher bei jedem
Versuch), der Supplicant protokolliert stattdessen
„148 Byte Zusatz-IEs weggelassen", findet sieben Netze, und `p2p_find` läuft
ohne Fehler durch. Damit ist der Suchlauf als Weg offen: ein Dongle, der seine
Gruppe beacon-t, erscheint hier als `DIRECT-…` und wpa_supplicant liest seine
P2P-Kennung aus dem Beacon.

**Prüf-Gegenstelle am Arch-Rechner gescheitert** (und wieder vollständig
abgeräumt): `p2p_group_add` startet die Gruppe, und der Supplicant entfernt die
Schnittstelle im selben Augenblick wieder („Removing interface", direkt nach
`AP-ENABLED`) — er läuft unter der Regie des NetworkManagers, und ein
`managed=0`-Schnipsel ändert das nicht. Dazu kommt: der PC hängt auf einem
5-GHz-DFS-Kanal, und der WL1271 im N950 kann ohnehin nur 2,4 GHz. Eine zweite
`managed`-Schnittstelle erlaubt die Karte nicht (`#{managed} <= 1`), eine
eigene Supplicant-Instanz bräuchte also die einzige WLAN-Karte des PCs — und
das ist dessen einzige Leitung.

**Offen bleibt damit genau ein Stück:** der WPS-Beitritt ohne Tippen. Das lässt
sich auch ohne Dongle prüfen — am **WPS-Knopf des Heimrouters**: `wps_pbc` mit
unserem Supplicant beweist genau den Mechanismus (WPS als Anmelder, IEs in der
Anmeldung statt im Scan) auf dieser Hardware.

**Nebenwirkung, die zählt:** jeder Funkversuch nimmt `icd2`/`wlancond` für rund
2,5 Minuten weg. Solange geht am Gerät keine Verbindungsliste auf, und danach
muss icd2 erst neu suchen. Funkversuche deshalb nur noch nach Absprache.
`tools/wlan-heilen.sh` stellt den Werkszustand her (Supplicant töten, Dienste
neu starten, wlan0 einmal aus und an).

## 09.10.2026, siebter Durchgang: Tempo, zweiter Teil

| Fassung | Bilder/s (848x480) | CPU | Abgriff je Bild |
|---|---|---|---|
| 0.3.x direkt aus dem Framebuffer drehen | 9,5 | 85 % | 66 ms |
| 0.4.x Seite kopieren + Kacheln | 24,1 | 99 % | 30 ms |
| 0.6.x `-I`: I420 selbst, Puffer vom Kodierer | 28,6 | 93 % | 30,5 ms |
| 0.8.x `-N`: drehen, dann NEON | **28,7** | **67 %** | 21,3 ms |
| 0.9.x `-N -r 0` (Oberfläche schon quer) | **29,1** | **56 %** | 17,8 ms |

Damit ist **nicht mehr die CPU der Engpass, sondern der DSP-Kodierer** — der
schafft bei 848x480 rund 29 Bilder/s (eigene Messung mit `fakesrc`: 150 Bilder
in 5,14 s). Bei Ziel 60 bricht der Strom ab (`appsrc` meldet
`Internal data flow error`, der Kodierer kommt nicht nach), 1280x720 kann
`dsph264enc` nicht — dafür gäbe es `dsphdh264enc`.

**`gst_pad_alloc_buffer` auf dem appsrc-Pad liefert Puffer, die der DSP
abbilden kann** (der Kodierer vergibt sie). Damit braucht es kein
`ffmpegcolorspace` mehr: wir schreiben das fertige I420 direkt in den Puffer
des Kodierers.

**`dspvpp` fällt aus:** das Element gibt es (RGB565 rein, I420 raus), aber
seine DSP-Firmware **`/lib/dsp/vpp_sn.dll64P` fehlt im Harmattan-Abbild**
(dmesg: `cod_open: error status 0x80008033`). `omap3-dsp-libraries-ti` liefert
18 Knoten, VPP ist nicht dabei. Also bleibt die Farbumrechnung bei der CPU —
mit NEON kostet sie noch 6 ms (vorher ~19 ms als Skalarschleife).

**Neu offen: die Lage.** Der Framebuffer folgt der Drehung der Oberfläche —
eine Hochkant-Ansicht liegt quer im Puffer, eine Quer-Ansicht liegt gerade.
Eine feste 90°-Drehung passt deshalb immer nur zu einer von beiden
(Belegbilder: beide Lagen nachgestellt). `castd -r 0|90|180|270` stellt sie
von Hand; richtig wäre, sie zur Laufzeit zu lesen — auf Harmattan über das
Context-Framework (`Screen.TopEdge`) oder den Lagesensor, wie Imiras
`orientation.cpp` es auf Sailfish macht.

## 09.10.2026, achter Durchgang: MPEG-TS/RTP

Imiras `tsmux.cpp` (867 Z.) und `rtpsender.cpp` (191 Z.) sind unverändert
übernommen (beide aus aethercast portiert, LGPL-3/Apache-2 im Kopf vermerkt).
Dazwischen liegt `src/tswrap.cpp`: castd bleibt C, die beiden sind C++ — die
Schicht reicht eine C-Anbindung durch, erkennt im Annex-B-Strom des Kodierers
SPS/PPS/IDR und uebergibt den Kopfsatz einmalig per `setCodecConfig`.
Gebaut wird jetzt mit `g++ -static-libstdc++ -static-libgcc` (Harmattans
libstdc++ ist die von GCC 4.4 und kennt die Symbole von GCC 14 nicht).

Die Kette steht: Bildschirm → NEON-I420 → DSP-H.264 → MPEG-TS → RTP →
Arch-Rechner, dort mit `rtpmp2tdepay` aufgenommen, von ffprobe als
`h264, 848x480` erkannt und als Einzelbild geprueft (Browserfenster, Text
scharf). 240 Zugriffseinheiten, 32 % einer CPU bei 19,3 Bildern/s; mit
bewegtem Bildschirminhalt faellt die Rate auf ~13/s, weil der Abgriff dann mit
der Oberflaeche um den Speicher streitet.

**Die wichtigste Entdeckung dabei: der DSP-Kodierer liefert genau EIN
Schluesselbild — beim Start.** Danach nur noch P-Scheiben (nachgezaehlt im
rohen Strom: SPS, PPS, 1 IDR, 99 P). `keyframe-interval` wirkt nicht, `mode=
streaming` aendert nichts, und das `GstForceKeyUnit`-Ereignis beachtet
gst-dsp auch nicht. Folgen und Gegenmittel:

* Ein Empfaenger, der **spaeter** zuhoert, bekam gar nichts (`non-existing
  PPS 0 referenced`). Deshalb wiederholt `tswrap` den Kopfsatz: jedes n-te
  Bild wird dem Packer als Schluesselbild gemeldet, der stellt SPS/PPS voran
  (nachgezaehlt: 7 SPS und 7 PPS auf 60 Bilder bei `-K 10`).
* Das eine echte Schluesselbild wird **dreimal** geschickt. Geht es verloren,
  bliebe der Fernseher sonst fuer immer schwarz.
* Fuer Miracast reicht das: die Senke hoert ab dem RTSP-Handschlag zu, also
  vom ersten Bild an. Ein Neuaufbau mitten im Lauf ginge nur ueber einen
  Neustart des Kodierers — offen, falls ein Dongle das braucht.

Schalter von `castd`: `-T` MPEG-TS/RTP, `-K n` Kopfsatz alle n Bilder,
`-k s` Schluesselbildabstand (wirkungslos, bleibt fuer den Fall der Faelle),
`-b` Bitrate, `-R` Intra-Auffrischung, `-r` Lage, `-N` NEON-Weg.
Zum Pruefen: `IMIRA_TS_FILE` schreibt den TS mit, `IMIRA_ES_FILE` den rohen
Kodiererstrom, `IMIRA_TS_TRACE=n` beschreibt die ersten n Zugriffseinheiten.

## 09.10.2026, neunter Durchgang: RTSP-Handschlag M1–M8

`device/wfd-proto.py` ist Imiras `imira-wfd-proto.py`, angepasst: `python3.11`
fest im Kopf (ein `python3` gibt es auf Harmattan nicht), `/bin/sh` statt bash,
Streamer heisst `castd` und will `-H/-p`, kein AAC (der DSP kann nur H.264),
und das angebotene Videoformat ist einstellbar — Vorgabe **640x480p60**
(CEA-Bit 0, muss jede Senke koennen; 720p schafft `dsph264enc` nicht).

Dazu `tools/wfd-sink-sim.py`: eine **Senken-Attrappe** fuer den Arch-Rechner.
Sie tut, was ein Dongle nach dem Aufbau der Gruppe tut — nur ueber das
gewoehnliche Netz: verbindet sich zu Quelle:7236, beantwortet M1, schickt M2,
fragt mit M3 ab, nimmt M4/M5, schickt SETUP (M6) und PLAY (M7), nimmt den
RTP-Strom auf und beendet mit TEARDOWN. Damit laesst sich die ganze
Senderseite ohne Dongle pruefen.

**Ergebnis:** Handschlag vollstaendig durchgelaufen (M1-M8 samt Keepalive nach
15 s), 1812 RTP-Pakete / 2,2 MB angekommen, als `h264 640x480` erkannt und als
Bild geprueft — Startbildschirm des N950, richtig gedreht, mit Balken.

**Zwei Fehler dabei gefunden und behoben:**

1. **Das Bild war gequetscht.** 854x480 (16:9) auf 640x480 (4:3) gezogen.
   castd rechnet jetzt ein Inhaltsfeld aus und laesst den Rest schwarz
   (`-s` fuellt wieder, wenn jemand das will). Der Rand wird einmal
   geschrieben, nicht je Bild.
2. **Das Schluesselbild ging verloren.** Rund 40 kB als ~30 RTP-Pakete ohne
   Pause hintereinander — einmal kam es an, einmal gar nicht (im Mitschnitt:
   15 SPS, 15 PPS, **0 IDR**, und der Dekoder brachte kein Bild). Jetzt geht
   der Strom in Haeppchen von acht Paketen mit 0,4 ms Pause heraus, das
   Schluesselbild dreimal mit 3 ms Abstand. Danach kam es in jedem Lauf an.

Nebenbei: `pkill -x` kennt busybox **nicht** — der Streamer ueberlebte den
TEARDOWN. Jetzt `killall` mit `pidof` als Rueckfall.

## 09.10.2026, zehnter Durchgang: Lage automatisch

Harmattan hat drei Quellen für die Lage, und nur eine taugt:

| Quelle | Befund |
|---|---|
| `/sys/devices/platform/lis3lv02d/position` | roher Beschleunigungsmesser, lesbar ohne alles — sagt aber nur, wie das **Gerät** liegt |
| `Screen.TopEdge` (contextkit, **System**bus, `com.nokia.SensorService`, Pfad `/org/maemo/contextkit/Screen/TopEdge`) | geht, liefert `"left"` usw. — ebenfalls Gerätelage, nicht die der Oberfläche |
| **`/Screen/CurrentWindow/OrientationAngle`** (contextkit, **Sitzungs**bus, `org.maemo.mcompositor.context`) | **das Richtige**: der Compositor sagt, wie die Oberfläche gerade steht. Achtung: dieser Anbieter hängt seine Objekte **direkt in die Wurzel**, nicht unter `/org/maemo/contextkit/` — gefunden per Introspektion |

Gemessen: Oberfläche hochkant → **270**, und von Hand gebraucht wurde dafür
eine Drehung von **90**. Also `Drehung = (360 − Winkel) % 360`. Der Fall 270
ist nachgemessen, die übrigen drei folgen der Regel (durch Drehen des Geräts
zu bestätigen).

Eingebaut als `src/orient.c` (libdbus, im MADDE-Sysroot vorhanden) und
`castd -r auto`: alle 500 ms ein D-Bus-Umlauf, bei Änderung werden
Inhaltsfeld und Abbildungstabellen neu gerechnet und der Rand neu geschwärzt —
die Bildgröße bleibt, also läuft der Kodierer weiter.

**`sudo -E` gibt es auf Harmattan nicht** (sudo 1.6.8), die Umgebung ist unter
sudo also leer. `orient.c` liest die Busadresse deshalb notfalls aus
`/tmp/session_bus_address.user`. Das ist der Weg, der am Gerät funktioniert.

Probe durch die ganze Kette: Handschlag, 1059 RTP-Pakete, und das Bild an der
Senke zeigt den Sperrbildschirm **aufrecht**, mit Balken links und rechts.

## 09.10.2026, elfter Durchgang: Ton

**Nokias PulseAudio lässt den Mitschnitt zu.** Jede Senke hat eine
Mitschnitt-Quelle (`sink.music.monitor`, `sink.hw0.monitor`, …), alle im
Zustand „ausgesetzt", und `pulsesrc device=sink.music.monitor` geht ohne
Weiteres auf — keine Policy-Abfuhr, **keine Umleitung auf das Mikrofon**
(bei Stille kommen lauter Nullen, nicht Rauschen). Das war die offene Frage.

Drei Dinge dabei gelernt:

* **`audiotestsrc` gibt es am Gerät nicht**, der erste „Tonversuch" war
  deshalb stumm und beweiskräftig gar nichts. Zum Prüfen taugen die
  Systemklänge: `/usr/share/sounds/ui-tones/snd_default_beep.wav` über
  `filesrc ! wavparse ! audioconvert ! pulsesink device=sink.music`.
* **PulseAudio läuft im Systemmodus und hat eine D-Bus-Schnittstelle**
  (`/var/run/pulse/dbus-socket`). `pacmd`/`pactl` fehlen, aber
  `dbus-send --address=unix:path=/var/run/pulse/dbus-socket` listet Senken,
  Quellen und Ströme — so ist überhaupt herausgekommen, welche Monitore es
  gibt.
* **Zwei Ketten in einer GStreamer-Pipeline vertragen sich hier nicht** mit
  appsrc/appsink: der Tonzweig blieb `not-linked`. Der Ton hat deshalb eine
  **eigene Pipeline**, und damit Bild und Ton trotzdem zusammenpassen,
  kommen **beide Zeitstempel aus derselben Uhr** (`CLOCK_MONOTONIC` ab
  einem gemeinsamen Nullpunkt) statt aus Bildzähler bzw. Pipeline-Takt.

Eingebaut: `castd -A <quelle>` (Vorgabe `sink.music.monitor`, `off` schaltet
ab), `tswrap` meldet die LPCM-Spur **vor** dem ersten Paket an (die PMT trägt
eine feste Fassungsnummer) und sperrt mit einem Mutex, weil jetzt zwei Fäden
hineinschieben.

**Geprüft an der Senken-Attrappe:** 3577 RTP-Pakete, im Transportstrom zwei
Ströme (h264 und einer, den ffmpeg nicht kennt — WFD-LPCM kennt nur eine
Miracast-Senke). Selbst ausgepackt (PID 0x1100, PES `private_stream_1`,
4 Byte LPCM-Unterkopf, **big endian**): 1445 PES-Pakete, 1,39 Mio.
Abtastwerte = 14,4 s bei 48 kHz stereo, **Spitze 5826 — genau der Wert des
direkten Mitschnitts**. Die Signaltöne sind also unverfälscht angekommen.

**Kosten:** 52 % einer CPU mit Ton gegen 48 % ohne — der Ton selbst ist
billig. Teuer ist die Leitung: LPCM sind 1,5 Mbit/s **dauernd** (im
Mitschnitt 15891 Ton-TS-Pakete gegen 6058 für das Bild). Auf 2,4 GHz ist das
spürbar; `libgstnokiaaacenc` liegt am Gerät, AAC wäre also später der Hebel —
WFD erlaubt es als Alternative, und `tsmux` kann es schon.

## 09.10.2026, zwölfter Durchgang: Oberfläche

Qt 4.7 + `com.nokia.meego`, gebaut nach demselben Rezept wie die anderen
MeeGo-Portierungen (GCC-14-Cross, moc aus dem Qt des Simulators,
`-static-libstdc++`). Vier Seiten: Empfängerliste, Einstellungen (Bildgröße,
Bildrate, Ton, Lage), Protokoll, Über. `ui/caster.cpp` startet
`wfd-proto.py`, liest dessen Protokoll mit und leitet den Zustand ab.

**Die App sucht den Empfänger selbst** — über **icd2**, denselben Dienst, den
auch die Einstellungen benutzen. Das nimmt dem System das Funkgerät *nicht*
weg. Zwei Dinge dabei gelernt:

* `scan_req` schickt die Treffer **nur an den Aufrufer**, und nur solange der
  verbunden bleibt: mit `dbus-send` kommt nichts an (der Aufrufer ist sofort
  weg), `dbus-monitor` sieht die Unicast-Signale auch nicht. Die App muss
  selbst fragen und dranbleiben.
* Die Werte im `scan_result_sig` liegen **anders, als die Beschreibung der
  Schnittstelle zählt** (15 Stück): 7 = Netztyp, **8 = Name**, 10 = Kennung
  als Bytes, 12 = Güte, **13 = Stationskennung**, **14 = dBm**. Mit den
  Indizes aus der Beschreibung kam genau ein Treffer durch; richtig gezählt
  sind es **12 Netze aus 46 Signalen** (Dubletten je Station
  zusammengefasst, die stärkste gilt).

Miracast-Gruppen (`DIRECT-…`) stehen in der Liste oben, der Rest darunter —
mancher Dongle spannt ein Netz mit eigenem Namen auf.

**Offen bleibt der Beitritt.** Welches Netz ein Dongle aufspannt (offen,
Passwort, WPS auf Zuruf) entscheidet der Dongle; ohne einen in der Hand wäre
jede Fassung geraten. `castTo()` sagt das im Protokoll ehrlich, statt zu
raten.

**Symbol:** `ui/icons/make-icon.py` zeichnet Fernseher samt Funkwellen gleich
für den Squircle (statt den Kreis des Sailfish-Originals) und schneidet mit
dem Alphakanal des Standardsymbols — 320 px gezeichnet, am Ende auf 80 und 64
verkleinert.

**Prüfung ohne Bildschirm:** `IMIRA_QML=/opt/imira/qml/check.qml` lädt ein
Prüfstück, das jede Seite einmal baut und meldet, was klemmt (`ALLE SEITEN
OK`). Dabei kam heraus, dass `Qt.quit()` ohne
`connect(engine, SIGNAL(quit()), app, SLOT(quit()))` wirkungslos ist.

Paket `imira_0.1.6_armel.deb`: Oberfläche, Technik, Startereintrag und Symbol
in einem, Installation per `aegis-dpkg`.

## 09.10.2026, dreizehnter Durchgang: Tempo bei bewegtem Bild

Der Abgriff wurde Schritt für Schritt vermessen (castd meldet jetzt die Zeit
je Arbeitsschritt) und daraufhin umgebaut. 848x480, ruhiger Bildschirm:

| Fassung | quer | hochkant |
|---|---|---|
| 2.0 (ganzes Bild drehen, dann ganzes Bild wandeln) | 19,1 ms / 63 % CPU | 19 ms |
| 2.3 (Bänder + zeilenweise wandeln in einem Durchgang) | 12,4 ms / 44 % | 11,6 ms / 42 % |
| 2.5 (quer ohne Zwischenabzug) | **6,7 ms / 30 %** | 11,7 ms / 45 % |

Drei Schritte, drei Einsichten:

1. **Die Umrechnung las jede Zeile zweimal** (einmal für die Helligkeit,
   einmal für die Farbe) und arbeitete über das ganze Bild — 820 kB passen in
   keinen Zwischenspeicher. Jetzt eine Zeile, ein Durchgang, `vld2q`/`vst2q`
   für gerade und ungerade Punkte: aus 8,0 ms wurde ein Teil der 6,5 ms, die
   das ganze Drehen-und-Wandeln noch kostet.
2. **Zeilenweise sammeln war ein Rückschritt** (50,9 ms bei 640x480 hochkant):
   die Quellpunkte einer Zielzeile liegen beim Drehen 1712 Byte auseinander,
   das ist ein Fehlgriff je Punkt. Jetzt in **Bändern** von 16 Zeilen, mit den
   Quellzeilen außen — je Quellzeile werden 16 benachbarte Spalten geholt, und
   das Band (27 kB) bleibt im Zwischenspeicher.
3. **Quer braucht es den Zwischenabzug der ganzen Seite gar nicht.** Wenn die
   Zielbreite nahe an der des Bildschirms liegt (848 von 854), wird
   **beschnitten statt skaliert** — aus dem Sammeln wird ein `memcpy`, und das
   kann direkt aus dem Framebuffer kommen (281 MB/s über mmap). Spart die
   vollen 4,3 ms des Abzugs.

**Was nichts gebracht hat:** eine bessere Nettigkeit für den Abgriff. Unter
einer Speicherlast lief er mit Nettigkeit 0 genauso schnell wie mit -10
(7,4 gegen 10,6 ms). Der Schalter `-P` bleibt, die Vorgabe ist 0.

**Ehrlich dazu:** ein einzelner Lauf zeigte unter Speicherlast einmal 44,3 ms
je Bild bei nur 18 % eigener Rechenzeit — der Abgriff wartete also, statt zu
rechnen. Mit dem neuen Code war das nicht mehr nachzustellen (7,4 ms unter
derselben Last). Auf dem Gerät arbeitet zwischendurch auch eine andere
Sitzung; ob der Ausreißer daher kam, ist offen. Was ein wirklich bewegter
Bildschirm kostet, lässt sich erst am entsperrten Gerät messen.
