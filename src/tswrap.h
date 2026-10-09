/* C-Anbindung an Imiras MPEG-TS-Packer und RTP-Sender.
 *
 * castd ist C (GStreamer-0.10-Glue), tsmux/rtpsender sind C++ -- diese
 * Schicht reicht das Noetige durch, damit castd.c beides benutzen kann,
 * ohne selbst C++ zu werden.
 */
#ifndef IMIRA_TSWRAP_H_
#define IMIRA_TSWRAP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ts_sink ts_sink;

/* Packer + Sender aufsetzen; ip/port ist der Empfaenger. */
ts_sink *ts_sink_new(int width, int height, int fps,
                     const char *ip, int port, int csd_every, int with_audio);
/* csd_every: alle wieviel Bilder SPS/PPS erneut mitgeschickt werden. Der
 * DSP-Kodierer liefert genau ein Schluesselbild beim Start (nachgemessen,
 * `keyframe-interval` wirkt nicht), ein spaeter zuhoerender Empfaenger haette
 * sonst nie einen Kopfsatz. 0 schaltet die Wiederholung ab. */

/* Eine Zugriffseinheit (Annex-B, wie sie der DSP-Kodierer liefert)
 * einpacken und wegschicken. SPS/PPS erkennt die Schicht selbst und
 * uebergibt sie einmalig als Kodierer-Vorspann. */
int ts_sink_feed(ts_sink *s, const uint8_t *au, size_t len, int64_t pts_us);
/* Ton: S16LE, 48 kHz, stereo, so wie er aus der Mitschnitt-Quelle kommt.
 * pts_us ist die Zeit des ersten Abtastwerts. */
int ts_sink_audio(ts_sink *s, const uint8_t *pcm, size_t len, int64_t pts_us);

/* Programmuhr allein schicken (PAT/PMT/PCR), wenn gerade kein Bild faellt. */
int ts_sink_clock(ts_sink *s, int64_t now_us);
void ts_sink_free(ts_sink *s);
/* Zaehler fuer die Statistik */
unsigned long ts_sink_packets(const ts_sink *s);
unsigned long ts_sink_bytes(const ts_sink *s);

#ifdef __cplusplus
}
#endif
#endif
