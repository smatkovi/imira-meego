# Imira für MeeGo Harmattan (N9/N950) — Vorarbeiten

Ziel: den Bildschirm des Nokia N9/N950 auf einen **Miracast-Dongle** an
Fernseher oder Beamer übertragen. Vorbild ist
[Imira](https://github.com/JimKnopfIoT/harbour-imira) (Miracast für Sailfish OS,
GPL-3.0-or-later); portiert wird die Idee, nicht der Code: Imiras Technik hängt
am Lipstick-Recorder, an droidmedia und an systemd, die es hier alle nicht gibt.

**Stand:** Vorarbeiten, noch keine App. Was läuft, was nicht und warum, steht
mit Messwerten in [`notes/BEFUND.md`](notes/BEFUND.md).

## Was schon geht

* **Bildstrecke:** Bildschirm (`/dev/fb0`, RGB565, gedreht) → DSP-H.264
  (`dsph264enc`, der Kodierer im OMAP3) → RTP. Geprüft bis zum dekodierten
  Einzelbild auf dem PC. ~9,5 Bilder/s bei 848x480, Hebel zum Beschleunigen
  sind benannt.
* **wpa_supplicant 2.11 für armel**, quergebaut mit P2P, WPS, Wi-Fi Display und
  einem zeitgemäßen EAP-Teil (TLS 1.2) — damit ist nebenbei **WPA2-Enterprise**
  möglich, das die Werkstechnik von 2011 nicht mehr schafft.

## Was noch nicht geht

Die **Wi-Fi-Direct-Entdeckung**. Der Treiber (`wl12xx`, Kernel 2.6.32) kennt
weder die AP- noch die P2P-Schnittstellenarten und trägt **keine Zusatz-IEs in
Probe Requests** (`max scan IE len = 0`). Das Telefon kann also nie
Gruppeninhaber sein und im P2P-Sinn auch nicht gefunden werden. Offen ist der
Weg, der bleibt: gewöhnlicher Scan findet die Gruppe des Dongles, Beitritt per
Aktionsrahmen (Provision Discovery) und WPS — beides kann der Treiber. Das
entscheidet erst ein echter Dongle.

## Teile

```
src/castd.c        Abgriff + Kodierer + RTP in einem Programm
src/fbcap.c        nur der Abgriff, für Proben
meego/wpa/         Rezept und Eingriff für wpa_supplicant
tools/             Bauen, Paketieren (aegis!), Aufspielen
notes/BEFUND.md    alle Messungen
```

Lizenz: GPL-3.0-or-later, wie Imira.
