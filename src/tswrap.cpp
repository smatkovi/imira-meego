/* C-Anbindung an tsmux/rtpsender -- siehe tswrap.h. */
#include "tswrap.h"
#include "tsmux.h"
#include "rtpsender.h"

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

namespace {

/* Findet in einem Annex-B-Strom den Anfang der ersten Bild-NAL (Typ 1 oder 5)
 * und sagt, ob es eine IDR ist. Alles davor (SPS/PPS/SEI) ist Vorspann. */
struct Scan {
    size_t first_vcl = 0;   /* Anfang des Startcodes der ersten Bild-NAL */
    bool idr = false;
    bool has_vcl = false;
    bool has_csd = false;
};

Scan scan_annexb(const uint8_t *d, size_t n)
{
    Scan s;
    size_t i = 0;
    while (i + 4 <= n) {
        size_t sc = 0;
        if (d[i] == 0 && d[i+1] == 0 && d[i+2] == 1) sc = 3;
        else if (i + 4 <= n && d[i] == 0 && d[i+1] == 0 && d[i+2] == 0 && d[i+3] == 1) sc = 4;
        if (!sc) { i++; continue; }
        size_t nal = i + sc;
        if (nal >= n) break;
        int type = d[nal] & 0x1f;
        if (type == 7 || type == 8) s.has_csd = true;
        if (type == 1 || type == 5) {
            if (!s.has_vcl) { s.first_vcl = i; s.has_vcl = true; }
            if (type == 5) s.idr = true;
        }
        i = nal + 1;
    }
    return s;
}

/* Grosse Bilder in Haeppchen schicken statt in einem Schwall.
 *
 * Ein Schluesselbild sind rund 40 kB, als RTP ueber 30 Pakete -- und die
 * gingen nacheinander ohne Pause heraus. Gemessen gegen die Senken-Attrappe:
 * einmal kam das Schluesselbild an, einmal gar nicht (15 SPS, 15 PPS, 0 IDR
 * im Mitschnitt). Weder Sende- noch Empfangspuffer halten so einen Stoss
 * zuverlaessig; ein paar hundert Mikrosekunden Pause je acht Paketen kosten
 * nichts und machen den Unterschied. */
void send_paced(imira::RtpSender &rtp, const uint8_t *ts, size_t len,
                int64_t pts_us)
{
    const size_t chunk = 7 * 188 * 8;          /* acht RTP-Pakete */
    size_t off = 0;
    while (off < len) {
        size_t n = len - off < chunk ? len - off : chunk;
        rtp.send(ts + off, n, pts_us);
        off += n;
        if (off < len) {
            struct timespec t = {0, 400000};   /* 0,4 ms */
            nanosleep(&t, nullptr);
        }
    }
}

} // namespace

struct ts_sink {
    /* Zwei Faeden schieben hier hinein: der Kodierfaden mit den Bildern und
     * der Tonfaden mit dem Mitschnitt. Packer, Puffer und Sender gehoeren
     * immer nur einem davon. */
    std::mutex lock;
    imira::TsMux mux;
    imira::RtpSender rtp;
    std::vector<uint8_t> ts;
    bool csd_done = false;
    int64_t last_pat_us = -1000000;
    unsigned long packets = 0;
    unsigned long bytes = 0;
    unsigned long idrs = 0;
    int csd_every = 0;
    unsigned long seen = 0;
    int trace = 0;          /* IMIRA_TS_TRACE: erste Einheiten beschreiben */
    FILE *dump = nullptr;   /* IMIRA_TS_FILE: Strom zusaetzlich mitschreiben */
    FILE *es = nullptr;     /* IMIRA_ES_FILE: das rohe H.264 des Kodierers */
};

extern "C" ts_sink *ts_sink_new(int width, int height, int fps,
                                const char *ip, int port, int csd_every,
                                int with_audio)
{
    ts_sink *s = new ts_sink();
    s->csd_every = csd_every;
    if (s->mux.addH264Track(width, height, fps, 1) < 0) { delete s; return nullptr; }
    /* Die Tonspur muss vor dem ersten Paket da sein: die PMT traegt eine feste
     * Fassungsnummer, ein Empfaenger muss eine spaetere Aenderung nicht
     * bemerken. */
    if (with_audio && s->mux.addLpcmTrack(48000, 2) < 0) {
        fprintf(stderr, "Ton: LPCM-Spur geht nicht, bleibe stumm\n");
    }
    /* Die Bilder entstehen beim Abgriff und erreichen die Senke spaeter als
     * ihre Aufnahmezeit; ohne Vorlauf wirft die Senke sie als "zu spaet" weg.
     * 150 ms ist der Wert, den Imira auf Sailfish gemessen hat. */
    s->mux.setPresentationDelayUs(150000);
    if (!s->rtp.open(ip ? ip : "127.0.0.1", (uint16_t)port, 0)) {
        delete s; return nullptr;
    }
    if (getenv("IMIRA_TS_TRACE")) s->trace = atoi(getenv("IMIRA_TS_TRACE"));
    if (const char *f = getenv("IMIRA_TS_FILE")) s->dump = fopen(f, "wb");
    if (const char *f = getenv("IMIRA_ES_FILE")) s->es = fopen(f, "wb");
    return s;
}

extern "C" int ts_sink_feed(ts_sink *s, const uint8_t *au, size_t len,
                            int64_t pts_us)
{
    if (!s || !au || !len) return 0;
    std::lock_guard<std::mutex> g(s->lock);
    Scan sc = scan_annexb(au, len);
    if (s->es) fwrite(au, 1, len, s->es);
    if (!s->csd_done && sc.has_csd && sc.has_vcl && sc.first_vcl > 0) {
        /* Der Kodierer schickt SPS und PPS vor dem ersten Bild mit; tsmux
         * will sie einmal als Vorspann und setzt sie dann selbst vor jede
         * IDR. */
        s->mux.setCodecConfig(au, sc.first_vcl);
        s->csd_done = true;
    }
    const uint8_t *payload = au;
    size_t plen = len;
    if (s->csd_done && sc.has_vcl && sc.first_vcl > 0) {
        payload = au + sc.first_vcl;
        plen = len - sc.first_vcl;
    }
    bool with_pat = (pts_us - s->last_pat_us) >= 100000;   /* alle 100 ms */
    if (with_pat) s->last_pat_us = pts_us;
    if (sc.idr) s->idrs++;
    /* SPS/PPS regelmaessig wiederholen: tsmux stellt sie jedem als
     * Schluesselbild gemeldeten Zugriff voran. Der Kodierer liefert nur ein
     * einziges echtes Schluesselbild, und ohne Kopfsatz faengt kein Empfaenger
     * an, der spaeter zuhoert oder ein Paket verloren hat. */
    bool mark_idr = sc.idr;
    s->seen++;
    if (!mark_idr && s->csd_every > 0 && s->csd_done &&
        (s->seen % (unsigned long)s->csd_every) == 0)
        mark_idr = true;
    if (s->trace > 0) {
        fprintf(stderr, "AU %zu B: csd=%d vcl=%d idr=%d erstes_vcl=%zu -> "
                "Nutzlast %zu B", len, sc.has_csd, sc.has_vcl, sc.idr,
                sc.first_vcl, plen);
        s->trace--;
    }
    if (!s->mux.packetize(payload, plen, pts_us, mark_idr, with_pat, s->ts)) {
        if (s->trace >= 0) fprintf(stderr, " -- packetize sagt nein\n");
        return 0;
    }
    if (s->trace > 0 || (s->trace == 0 && s->packets < 3))
        fprintf(stderr, " -> TS %zu B\n", s->ts.size());
    if (s->dump && !s->ts.empty())
        fwrite(s->ts.data(), 1, s->ts.size(), s->dump);
    if (s->ts.empty()) return 1;
    /* Das einzige Schluesselbild der Sitzung dreimal schicken. Der DSP liefert
     * genau eines, beim Start (keyframe-interval und das ForceKeyUnit-Ereignis
     * wirken beide nicht, nachgemessen) -- geht es auf dem Weg verloren, bleibt
     * der Fernseher fuer immer schwarz. Drei Sendungen kosten ein paar
     * Kilobyte und decken den ueblichen Einzelverlust ab. */
    s->packets++;
    s->bytes += s->ts.size();
    send_paced(s->rtp, s->ts.data(), s->ts.size(), pts_us);
    if (sc.idr) {
        /* Das einzige Schluesselbild der Sitzung dreimal, mit Pausen: der DSP
         * liefert genau eines, beim Start. Geht es verloren, bleibt der
         * Fernseher fuer immer schwarz. */
        for (int i = 0; i < 2; i++) {
            struct timespec t = {0, 3000000};  /* 3 ms zwischen den Durchgaengen */
            nanosleep(&t, nullptr);
            send_paced(s->rtp, s->ts.data(), s->ts.size(), pts_us);
        }
        s->bytes += 2 * s->ts.size();
    }
    return 1;
}

extern "C" int ts_sink_audio(ts_sink *s, const uint8_t *pcm, size_t len,
                             int64_t pts_us)
{
    if (!s || !pcm || !len) return 0;
    std::lock_guard<std::mutex> g(s->lock);
    std::vector<uint8_t> out;
    if (!s->mux.packetizeAudio(pcm, len, pts_us, out)) return 0;
    if (out.empty()) return 1;        /* noch keine ganze Zugriffseinheit */
    s->bytes += out.size();
    send_paced(s->rtp, out.data(), out.size(), pts_us);
    return 1;
}

extern "C" int ts_sink_clock(ts_sink *s, int64_t now_us)
{
    if (!s) return 0;
    std::lock_guard<std::mutex> g(s->lock);
    if (!s->mux.packetizeClock(s->ts) || s->ts.empty()) return 0;
    s->last_pat_us = now_us;
    s->bytes += s->ts.size();
    return s->rtp.send(s->ts.data(), s->ts.size(), now_us) ? 1 : 0;
}

extern "C" void ts_sink_free(ts_sink *s)
{
    if (s) {
        fprintf(stderr, "%lu Schluesselbilder gesehen\n", s->idrs);
        if (s->dump) fclose(s->dump);
        if (s->es) fclose(s->es);
    }
    delete s;
}
extern "C" unsigned long ts_sink_packets(const ts_sink *s) { return s ? s->packets : 0; }
extern "C" unsigned long ts_sink_bytes(const ts_sink *s) { return s ? s->bytes : 0; }
