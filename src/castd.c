/* imira-castd (MeeGo-Fassung) -- Bildschirm abgreifen, in Hardware kodieren,
 * als H.264/RTP wegschicken.
 *
 * Warum ein eigener Dämon und nicht gst-launch mit einer Pipe:
 *   * Die Pipe liefert nur 64-KB-Häppchen (PIPE_BUF), gst sieht also keine
 *     ganzen Bilder; der DSP-Kodierer bekommt Bruchstücke und wirft
 *     MMUFAULT oder "Internal data flow error". appsrc schiebt ganze Bilder.
 *   * /dev/fb0 gehört der Gruppe video (root nötig), aber aegis lässt
 *     gst-launch als root nicht starten. Dieses Programm öffnet den
 *     Framebuffer als root und **legt die Rechte danach ab**, bevor
 *     GStreamer überhaupt anfängt.
 *   * Der DSP kann nur Puffer abbilden, die sein eigenes Element vergibt --
 *     deshalb steht ffmpegcolorspace davor: dessen Ausgabepuffer holt es vom
 *     Kodierer, und unsere RGB565-Bilder dürfen gewöhnlicher Heap sein.
 *
 * Der Framebuffer ist RGB565, 1712 Byte je Zeile, doppelt gepuffert und um
 * 90 Grad gedreht gegenüber dem Querformat, das ein Fernseher sehen will;
 * gedreht und skaliert wird hier, die Farbumrechnung macht GStreamer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <time.h>
#include <sys/resource.h>
#include <sched.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "tswrap.h"
#include "orient.h"
#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

#define TILE 32          /* Kachelbreite beim Drehen, gemessen am besten */
#define BAND 16          /* Zeilen je Durchgang -- ein Band bleibt im Zwischenspeicher */

struct cast {
    int fd;
    unsigned char *fb;
    unsigned char *copy;      /* sichtbare Seite im Arbeitsspeicher */
    size_t pagesz;
    size_t maplen;
    int sw, sh, stride;
    int dw, dh, fps;
    int *mapx, *mapy;         /* fuer 90/270 Grad */
    int *mapx0, *mapy0;       /* fuer 0/180 Grad */
    long long frames, limit;
    GstElement *pipeline, *src;
    GstPad *srcpad;           /* Pad des appsrc, fuer gst_pad_alloc_buffer */
    int i420;                 /* 1 = selbst nach I420 wandeln */
    int vpp;                  /* 1 = RGB565 an dspvpp, der DSP wandelt */
    int neon;                 /* 1 = drehen, dann mit NEON nach I420 */
    int rot_deg;              /* 0, 90, 180, 270 -- Lage des Bildschirminhalts */
    int ts;                   /* 1 = MPEG-TS/RTP wie Miracast es will */
    ts_sink *tssink;          /* Packer + Sender */
    long long aus;            /* gepackte Zugriffseinheiten */
    GstElement *enc;          /* der DSP-Kodierer, fuer die Schluesselbild-Bitte */
    int keyint;               /* Sekunden zwischen den Bitten */
    long long last_key_frame;
    const char *amon;         /* Mitschnitt-Quelle fuer den Ton, NULL = stumm */
    long long apackets;
    GstElement *apipe;        /* eigene Pipeline fuer den Ton */
    long long t0_us;          /* gemeinsamer Nullpunkt fuer Bild und Ton */
    unsigned short *band;     /* BAND Zeilen RGB565, zusammengetragen */
    int ox, oy, cw, ch;       /* Inhaltsfeld im Zielbild (Rest bleibt schwarz) */
    int stretch;              /* 1 = fuellen statt Seitenverhaeltnis wahren */
    int crop_x;               /* >=0: quer wird beschnitten statt skaliert */
    int auto_rot;             /* 1 = Lage beim Compositor erfragen */
    long long last_orient;    /* wann zuletzt gefragt (us) */
    int noalloc;              /* 1 = Kodierer gab keinen Puffer her */
    GMainLoop *loop;
    GstClockTime pts;
    long long t0, grab_us;      /* Start und reine Abgriffszeit */
    long long copy_us, rot_us, conv_us;   /* je Arbeitsschritt */
};

/* Eine Zeile RGB565 -> I420, sechzehn Bildpunkte je Schritt.
 *
 * Zeilenweise statt über das ganze Bild, und Helligkeit und Farbe in
 * **einem** Durchgang: vorher wurde jede Zeile zweimal gelesen (einmal für Y,
 * einmal für die Farbdifferenzen), und das Zwischenbild war mit 820 kB viel
 * zu groß für den Zwischenspeicher. Eine Zeile sind 1,7 kB und bleibt im L1.
 *
 * Y = (66R + 129G + 25B + 4224) >> 8 passt in 16 Bit ohne Vorzeichen; die
 * Farbdifferenzen passen mit Vorzeichen, wenn erst geschoben und dann 128
 * addiert wird. u/v dürfen NULL sein (ungerade Zeilen).
 */
static void rgb565_row_to_i420(const unsigned short *s, int w,
                               unsigned char *yp, unsigned char *up,
                               unsigned char *vp)
{
    int x = 0;
#ifdef __ARM_NEON__
    for (; x + 16 <= w; x += 16) {
        /* vld2q trennt gerade und ungerade Punkte -- die geraden tragen die
         * Farbe, beide zusammen die Helligkeit. So wird jeder Punkt genau
         * einmal geladen. */
        uint16x8x2_t two = vld2q_u16(s + x);
        uint16x8_t pe = two.val[0], po = two.val[1];

        uint16x8_t re = vshlq_n_u16(vshrq_n_u16(pe, 11), 3);
        uint16x8_t ge = vshlq_n_u16(vandq_u16(vshrq_n_u16(pe, 5), vdupq_n_u16(0x3f)), 2);
        uint16x8_t be = vshlq_n_u16(vandq_u16(pe, vdupq_n_u16(0x1f)), 3);
        uint16x8_t ro = vshlq_n_u16(vshrq_n_u16(po, 11), 3);
        uint16x8_t go = vshlq_n_u16(vandq_u16(vshrq_n_u16(po, 5), vdupq_n_u16(0x3f)), 2);
        uint16x8_t bo = vshlq_n_u16(vandq_u16(po, vdupq_n_u16(0x1f)), 3);

        uint16x8_t ye = vmlaq_n_u16(vdupq_n_u16(4224), re, 66);
        ye = vmlaq_n_u16(ye, ge, 129); ye = vmlaq_n_u16(ye, be, 25);
        uint16x8_t yo = vmlaq_n_u16(vdupq_n_u16(4224), ro, 66);
        yo = vmlaq_n_u16(yo, go, 129); yo = vmlaq_n_u16(yo, bo, 25);
        /* vst2 setzt gerade und ungerade wieder ineinander */
        uint8x8x2_t yy;
        yy.val[0] = vshrn_n_u16(ye, 8);
        yy.val[1] = vshrn_n_u16(yo, 8);
        vst2_u8(yp + x, yy);

        if (up) {
            int16x8_t r = vreinterpretq_s16_u16(re);
            int16x8_t g = vreinterpretq_s16_u16(ge);
            int16x8_t b = vreinterpretq_s16_u16(be);
            int16x8_t u = vmlsq_n_s16(vmulq_n_s16(b, 112), r, 38);
            u = vmlsq_n_s16(u, g, 74);
            int16x8_t v = vmlsq_n_s16(vmulq_n_s16(r, 112), g, 94);
            v = vmlsq_n_s16(v, b, 18);
            vst1_u8(up + (x >> 1), vqmovun_s16(vaddq_s16(vshrq_n_s16(u, 8),
                                                         vdupq_n_s16(128))));
            vst1_u8(vp + (x >> 1), vqmovun_s16(vaddq_s16(vshrq_n_s16(v, 8),
                                                         vdupq_n_s16(128))));
        }
    }
#endif
    for (; x < w; x++) {
        unsigned short p = s[x];
        int R = ((p >> 11) & 0x1f) << 3, G = ((p >> 5) & 0x3f) << 2,
            B = (p & 0x1f) << 3;
        yp[x] = (unsigned char)((66*R + 129*G + 25*B + 4224) >> 8);
        if (up && !(x & 1)) {
            up[x >> 1] = (unsigned char)(((112*B - 38*R - 74*G) >> 8) + 128);
            vp[x >> 1] = (unsigned char)(((112*R - 94*G - 18*B) >> 8) + 128);
        }
    }
}


static long long now_us(void);

/* Ton aus der Mitschnitt-Quelle: als LPCM in denselben Transportstrom.
 * Laeuft im Tonfaden -- tswrap sperrt, weil auch der Kodierfaden hineinschiebt. */
static void on_audio(GstElement *sink, gpointer data)
{
    struct cast *c = data;
    GstBuffer *b = gst_app_sink_pull_buffer(GST_APP_SINK(sink));
    if (!b) return;
    if (c->tssink) {
        /* Zeit des ersten Abtastwerts: jetzt minus die Laenge des Blocks. */
        int64_t dur_us = (int64_t)GST_BUFFER_SIZE(b) * 1000000 / (48000 * 2 * 2);
        int64_t pts_us = now_us() - c->t0_us - dur_us;
        if (pts_us < 0) pts_us = 0;
        ts_sink_audio(c->tssink, GST_BUFFER_DATA(b), GST_BUFFER_SIZE(b), pts_us);
        c->apackets++;
    }
    gst_buffer_unref(b);
}


static void on_encoded(GstElement *sink, gpointer data)
{
    struct cast *c = data;
    GstBuffer *b = gst_app_sink_pull_buffer(GST_APP_SINK(sink));
    if (!b) return;
    int64_t pts_us;
    if (GST_BUFFER_TIMESTAMP_IS_VALID(b))
        pts_us = (int64_t)(GST_BUFFER_TIMESTAMP(b) / 1000);
    else
        pts_us = c->aus * (1000000 / (c->fps > 0 ? c->fps : 30));
    ts_sink_feed(c->tssink, GST_BUFFER_DATA(b), GST_BUFFER_SIZE(b), pts_us);
    c->aus++;
    gst_buffer_unref(b);
}


/* Lage setzen: Inhaltsfeld und Abbildungstabellen neu rechnen. Darf im Lauf
 * passieren -- die Bildgroesse bleibt, nur das Feld darin aendert sich
 * (hochkant wird ein schmaler Streifen mit Balken links und rechts). */
static void set_rotation(struct cast *c, int deg)
{
    int srcw = (deg == 90 || deg == 270) ? c->sh : c->sw;
    int srch = (deg == 90 || deg == 270) ? c->sw : c->sh;
    c->rot_deg = deg;
    if (c->stretch) {
        c->ox = c->oy = 0; c->cw = c->dw; c->ch = c->dh;
    } else {
        long long byw = (long long)c->dw * srch;
        long long byh = (long long)c->dh * srcw;
        if (byw <= byh) { c->cw = c->dw; c->ch = (int)(byw / srcw); }
        else            { c->ch = c->dh; c->cw = (int)(byh / srch); }
        c->cw &= ~1; c->ch &= ~1;
        c->ox = ((c->dw - c->cw) / 2) & ~1;
        c->oy = ((c->dh - c->ch) / 2) & ~1;
    }
    /* Quer liegt die Breite oft dicht an der des Bildschirms (848 von 854).
     * Dann lieber die paar Spalten am Rand abschneiden als jeden Punkt
     * einzeln zu holen: so wird aus dem Sammeln ein memcpy. Unter zwei
     * Prozent Unterschied sieht das niemand. */
    c->crop_x = -1;
    if (!(deg == 90 || deg == 270) && c->cw <= c->sw &&
        (long long)c->cw * 100 >= (long long)c->sw * 98)
        c->crop_x = (c->sw - c->cw) / 2;
    for (int i = 0; i < c->cw; i++) {
        c->mapx[i]  = (int)((long long)i * c->sh / c->cw);
        c->mapx0[i] = c->crop_x >= 0 ? c->crop_x + i
                                     : (int)((long long)i * c->sw / c->cw);
    }
    for (int i = 0; i < c->ch; i++) {
        c->mapy[i]  = (int)((long long)i * c->sw / c->ch);
        c->mapy0[i] = (int)((long long)i * c->sh / c->ch);
    }
    fprintf(stderr, "Lage %d Grad, Inhaltsfeld %dx%d bei %d,%d in %dx%d%s\n",
            deg, c->cw, c->ch, c->ox, c->oy, c->dw, c->dh,
            c->crop_x >= 0 ? " (beschnitten statt skaliert)" : "");
}


static long long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static gboolean grab(gpointer data)
{
    struct cast *c = data;
    long long t_in = now_us();
    struct fb_var_screeninfo vi;
    guint size = (c->i420 || c->neon) ? (guint)c->dw * c->dh * 3 / 2
                                      : (guint)c->dw * c->dh * 2;
    GstBuffer *buf = NULL;
    if ((c->i420 || c->vpp || c->neon) && !c->noalloc) {
        /* Der DSP kann nur Speicher abbilden, den sein eigenes Element
         * vergibt. gst_pad_alloc_buffer reicht die Bitte nach unten durch,
         * der Kodierer antwortet mit einem Puffer aus seinem Vorrat -- und
         * wir schreiben das fertige I420 gleich hinein. Damit faellt
         * ffmpegcolorspace samt einer vollen Kopie weg. */
        GstFlowReturn ar = gst_pad_alloc_buffer(c->srcpad,
                GST_BUFFER_OFFSET_NONE, size, GST_PAD_CAPS(c->srcpad), &buf);
        if (ar != GST_FLOW_OK || !buf || GST_BUFFER_SIZE(buf) < size) {
            if (buf) { gst_buffer_unref(buf); buf = NULL; }
            if (!c->noalloc) {
                g_printerr("Kodierer gibt keinen Puffer her (%s) -- "
                           "gewoehnlicher Speicher\n", gst_flow_get_name(ar));
                c->noalloc = 1;
            }
        }
    }
    if (!buf) buf = gst_buffer_new_and_alloc(size);
    if (!buf) return FALSE;

    int yoff = 0;
    if (ioctl(c->fd, FBIOGET_VSCREENINFO, &vi) == 0)
        yoff = vi.yoffset;                 /* sichtbare Seite */
    const unsigned char *page = c->fb + (size_t)yoff * c->stride;

    /* Erst die ganze Seite am Stueck holen, dann drehen. Direkt aus dem
     * Framebuffer zu drehen kostet 66,5 ms je Bild (spaltenweise Zugriffe,
     * 1712 B Schrittweite); kopieren kostet 2,9 ms (281 MB/s) und das Drehen
     * aus dem Arbeitsspeicher in 32er-Kacheln noch 11,7 ms -- zusammen
     * fuenfeinhalbmal schneller. Alles am N950 nachgemessen (src/fbbench.c). */
    /* Der Zwischenabzug der ganzen Seite ist nur nötig, wo wir wild im Bild
     * herumgreifen (drehen oder skalieren): aus dem Framebuffer selbst ist
     * ein einzelner Punkt teuer, eine ganze Zeile am Stück aber nicht
     * (gemessen: 281 MB/s über mmap). Quer und unskaliert lesen wir deshalb
     * direkt -- das spart 4 ms je Bild. */
    const unsigned char *src;
    if (c->neon && (c->rot_deg == 0) && c->crop_x >= 0) {
        src = page;
    } else {
        long long t_copy = now_us();
        memcpy(c->copy, page, c->pagesz);
        c->copy_us += now_us() - t_copy;
        src = c->copy;
    }

    if (c->neon) {
        /* Zeilenweise: eine Zeile zusammentragen (drehen und skalieren) und
         * gleich wandeln. Das Zwischenbild über das ganze Bild ist weg -- eine
         * Zeile sind 1,7 kB und bleibt im schnellen Zwischenspeicher, während
         * 820 kB jedes Mal durch den Hauptspeicher mussten. Wo nichts zu
         * sammeln ist (quer, ohne Skalieren), wandelt NEON direkt aus dem
         * Abzug des Bildschirms. */
        long long t_rot = now_us();
        unsigned char *yp = GST_BUFFER_DATA(buf);
        unsigned char *up = yp + (size_t)c->dw * c->dh;
        unsigned char *vp = up + (size_t)c->dw * c->dh / 4;
        const int dw = c->dw, dh = c->dh, cw = c->cw, ch = c->ch;
        const int ox = c->ox, oy = c->oy, cwh = dw / 2;

        /* Rand schwarz: der Puffer kommt aus dem Vorrat des Kodierers und
         * trägt noch das vorige Bild. Schwarz ist Y=16, U=V=128. */
        if (oy > 0 || ox > 0) {
            for (int y = 0; y < dh; y++) {
                unsigned char *yl = yp + (size_t)y * dw;
                if (y < oy || y >= oy + ch) {
                    memset(yl, 16, dw);
                } else if (ox > 0) {
                    memset(yl, 16, ox);
                    memset(yl + ox + cw, 16, dw - ox - cw);
                }
            }
            for (int y = 0; y < dh / 2; y++) {
                unsigned char *ul = up + (size_t)y * cwh;
                unsigned char *vl = vp + (size_t)y * cwh;
                if (2 * y < oy || 2 * y >= oy + ch) {
                    memset(ul, 128, cwh); memset(vl, 128, cwh);
                } else if (ox > 0) {
                    memset(ul, 128, ox / 2); memset(vl, 128, ox / 2);
                    memset(ul + (ox + cw) / 2, 128, cwh - (ox + cw) / 2);
                    memset(vl + (ox + cw) / 2, 128, cwh - (ox + cw) / 2);
                }
            }
        }

        const int straight = (c->rot_deg == 0 || c->rot_deg == 180);
        const int flip = (c->rot_deg == 180);
        const int ccw = (c->rot_deg == 270);
        /* In Bändern von BAND Zeilen: beim Drehen liegen die Quellpunkte einer
         * Zielzeile spaltenweise, 1712 Byte auseinander -- jede Zeile für sich
         * zu sammeln heißt ein Fehlgriff je Punkt (gemessen: 50,9 ms bei
         * 640x480 hochkant). Ein Band deckt BAND benachbarte Quellspalten ab;
         * wer die Quellzeilen außen durchläuft, holt sie mit ein, zwei
         * Zugriffen. Das Band selbst (16 x 848 x 2 = 27 kB) bleibt im
         * Zwischenspeicher. */
        for (int by = 0; by < ch; by += BAND) {
            int bh = by + BAND < ch ? BAND : ch - by;
            if (straight) {
                for (int k = 0; k < bh; k++) {
                    int y = by + k;
                    int sy = flip ? c->sh - 1 - c->mapy0[y] : c->mapy0[y];
                    const unsigned short *sl = (const unsigned short *)
                            (src + (size_t)sy * c->stride);
                    unsigned short *d = c->band + (size_t)k * cw;
                    if (!flip && c->crop_x >= 0)
                        memcpy(d, sl + c->crop_x, (size_t)cw * 2);
                    else if (flip)
                        for (int x = 0; x < cw; x++) d[x] = sl[c->sw - 1 - c->mapx0[x]];
                    else
                        for (int x = 0; x < cw; x++) d[x] = sl[c->mapx0[x]];
                }
            } else {
                /* Quellzeilen außen, Bandzeilen innen: je Quellzeile werden
                 * BAND benachbarte Spalten geholt. */
                for (int x = 0; x < cw; x++) {
                    int sy = ccw ? c->mapx[x] : c->sh - 1 - c->mapx[x];
                    const unsigned short *sl = (const unsigned short *)
                            (src + (size_t)sy * c->stride);
                    for (int k = 0; k < bh; k++) {
                        int sx = ccw ? c->sw - 1 - c->mapy[by + k] : c->mapy[by + k];
                        c->band[(size_t)k * cw + x] = sl[sx];
                    }
                }
            }
            for (int k = 0; k < bh; k++) {
                int dy = by + k + oy;
                int even = !(dy & 1);
                rgb565_row_to_i420(c->band + (size_t)k * cw, cw,
                                   yp + (size_t)dy * dw + ox,
                                   even ? up + (size_t)(dy >> 1) * cwh + (ox >> 1) : NULL,
                                   even ? vp + (size_t)(dy >> 1) * cwh + (ox >> 1) : NULL);
            }
        }
        c->rot_us += now_us() - t_rot;
        goto pushed;
    }

    if (c->i420) {
        /* drehen und gleich nach I420 wandeln, in denselben Kacheln */
        unsigned char *yp = GST_BUFFER_DATA(buf);
        unsigned char *up = yp + (size_t)c->dw * c->dh;
        unsigned char *vp = up + (size_t)c->dw * c->dh / 4;
        int cw = c->dw / 2;
        for (int ty = 0; ty < c->dh; ty += TILE) {
            int ylim = ty + TILE < c->dh ? ty + TILE : c->dh;
            for (int tx = 0; tx < c->dw; tx += TILE) {
                int xlim = tx + TILE < c->dw ? tx + TILE : c->dw;
                for (int dy = ty; dy < ylim; dy++) {
                    int sx = c->mapy[dy];
                    unsigned char *yl = yp + (size_t)dy * c->dw;
                    for (int dx = tx; dx < xlim; dx++) {
                        int sy = c->sh - 1 - c->mapx[dx];
                        unsigned short px = *((const unsigned short *)
                                (src + (size_t)sy * c->stride) + sx);
                        int r = ((px >> 11) & 0x1f) << 3;
                        int g = ((px >> 5) & 0x3f) << 2;
                        int b = (px & 0x1f) << 3;
                        yl[dx] = (unsigned char)((66*r + 129*g + 25*b + 4224) >> 8);
                        if (!(dy & 1) && !(dx & 1)) {
                            size_t ci = (size_t)(dy >> 1) * cw + (dx >> 1);
                            up[ci] = (unsigned char)((-38*r - 74*g + 112*b + 32768) >> 8);
                            vp[ci] = (unsigned char)((112*r - 94*g - 18*b + 32768) >> 8);
                        }
                    }
                }
            }
        }
        goto pushed;
    }

    unsigned short *out = (unsigned short *)GST_BUFFER_DATA(buf);
    for (int ty = 0; ty < c->dh; ty += TILE) {
        int ylim = ty + TILE < c->dh ? ty + TILE : c->dh;
        for (int tx = 0; tx < c->dw; tx += TILE) {
            int xlim = tx + TILE < c->dw ? tx + TILE : c->dw;
            for (int dy = ty; dy < ylim; dy++) {
                int sx = c->mapy[dy];
                unsigned short *row = out + (size_t)dy * c->dw;
                for (int dx = tx; dx < xlim; dx++) {
                    int sy = c->sh - 1 - c->mapx[dx];
                    row[dx] = *((const unsigned short *)
                                (src + (size_t)sy * c->stride) + sx);
                }
            }
        }
    }

pushed:
    /* Lage nachfragen, hoechstens zweimal je Sekunde -- ein D-Bus-Umlauf
     * kostet unter einer Millisekunde, aber jedes Bild braucht es nicht. */
    if (c->auto_rot && t_in - c->last_orient > 500000) {
        c->last_orient = t_in;
        int a = orient_angle();
        if (a >= 0) {
            int want = (360 - a) % 360;
            if (want != c->rot_deg) set_rotation(c, want);
        }
    }

    /* Den Kodierer um ein Schluesselbild bitten. `keyframe-interval` wirkt in
     * dieser gst-dsp-Fassung nicht (nachgemessen: ein einziges Schluesselbild
     * beim Start), also der Umweg ueber das Ereignis, das spaetere
     * GStreamer-Fassungen dafuer kennen -- schadet nicht, wenn das Element es
     * nicht beachtet. */
    if (c->enc && c->keyint > 0 &&
        c->frames - c->last_key_frame >= (long long)c->keyint * c->fps) {
        c->last_key_frame = c->frames;
        gst_element_send_event(c->enc, gst_event_new_custom(
            GST_EVENT_CUSTOM_DOWNSTREAM,
            gst_structure_new("GstForceKeyUnit",
                              "all-headers", G_TYPE_BOOLEAN, TRUE, NULL)));
    }
    c->grab_us += now_us() - t_in;
    /* Zeitstempel aus der Uhr, nicht aus dem Zaehler: die Bildrate wird nie
     * genau getroffen (Ziel 20, erreicht 19,3), und der Ton laeuft nach
     * derselben Uhr -- sonst wandern Bild und Ton auseinander. */
    c->pts = (GstClockTime)(t_in - c->t0_us) * 1000;
    GST_BUFFER_TIMESTAMP(buf) = c->pts;
    GST_BUFFER_DURATION(buf) = GST_SECOND / c->fps;

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(c->src), buf);
    if (ret != GST_FLOW_OK) {
        g_printerr("push_buffer: %s\n", gst_flow_get_name(ret));
        g_main_loop_quit(c->loop);
        return FALSE;
    }
    c->frames++;
    if (c->limit && c->frames >= c->limit) {
        gst_app_src_end_of_stream(GST_APP_SRC(c->src));
        return FALSE;
    }
    return TRUE;
}

static gboolean on_bus(GstBus *bus, GstMessage *msg, gpointer data)
{
    struct cast *c = data;
    (void)bus;
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError *err = NULL; gchar *dbg = NULL;
        gst_message_parse_error(msg, &err, &dbg);
        g_printerr("FEHLER von %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
        if (dbg) { g_printerr("  %s\n", dbg); g_free(dbg); }
        g_error_free(err);
        g_main_loop_quit(c->loop);
        break;
    }
    case GST_MESSAGE_EOS:
        g_print("Ende des Datenstroms\n");
        g_main_loop_quit(c->loop);
        break;
    default: break;
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    struct cast c;
    memset(&c, 0, sizeof c);
    c.dw = 848; c.dh = 480; c.fps = 15;
    const char *dev = "/dev/fb0", *host = "127.0.0.1", *sink = NULL;
    int port = 5004, opt;
    uid_t drop_uid = 29999; gid_t drop_gid = 29999;

    int i420 = 0, vpp = 0, neon = 0, rot_deg = 90, ts_mode = 0;
    int keyint = 1, bitrate = 0, csd_every = 0, intra = 0, stretch = 0;
    int auto_rot = 0, nice_level = 0;
    const char *amon = "sink.music.monitor";
    while ((opt = getopt(argc, argv, "d:w:h:f:n:H:p:u:S:IVNr:Tk:b:K:RsA:P:")) != -1) {
        switch (opt) {
        case 'd': dev = optarg; break;
        case 'w': c.dw = atoi(optarg); break;
        case 'h': c.dh = atoi(optarg); break;
        case 'f': c.fps = atoi(optarg); break;
        case 'n': c.limit = atoll(optarg); break;
        case 'H': host = optarg; break;
        case 'p': port = atoi(optarg); break;
        case 'u': drop_uid = drop_gid = (uid_t)atoi(optarg); break;
        case 'S': sink = optarg; break;   /* eigenes Senken-Stück, z.B. Datei */
        case 'I': i420 = 1; break;   /* selbst nach I420 wandeln */
        case 'V': vpp = 1; break;    /* RGB565 an dspvpp, der DSP wandelt */
        case 'N': neon = 1; break;   /* drehen, dann mit NEON wandeln */
        case 'r':
            if (!strcmp(optarg, "auto")) auto_rot = 1;
            else rot_deg = atoi(optarg);
            break;
        case 'T': ts_mode = 1; break;  /* MPEG-TS/RTP statt rohem H.264/RTP */
        case 'k': keyint = atoi(optarg); break;
        case 'b': bitrate = atoi(optarg); break;
        case 'K': csd_every = atoi(optarg); break;
        case 'R': intra = 1; break;
        case 's': stretch = 1; break;  /* fuellen statt Balken */
        case 'P': nice_level = atoi(optarg); break;
        case 'A': amon = optarg; break;  /* Mitschnitt-Quelle, "off" = stumm */
        default:
            fprintf(stderr,
                "Aufruf: %s [-w 848] [-h 480] [-f 15] [-n Bilder]\n"
                "        [-H Empfaenger] [-p 5004] [-u uid] [-S gst-Senke] [-I]\n"
                "  -I: selbst nach I420 wandeln (spart ffmpegcolorspace)\n"
                "  -V: RGB565 an dspvpp geben (geht nicht: vpp_sn.dll64P fehlt)\n"
                "  -N: drehen, dann mit NEON nach I420 (schnellster Weg)\n"
                "  -r: Lage 0, 90 (Vorgabe), 180, 270 -- oder `auto`, dann\n"
                "      sagt der Compositor, wie die Oberflaeche steht\n"
                "  -T: als MPEG-TS/RTP schicken, wie Miracast es verlangt\n"
                "  -k: Abstand der Schluesselbilder in Sekunden (Vorgabe 1)\n"
                "  -b: Bitrate in bit/s (0 = der Kodierer entscheidet)\n"
                "  -K: SPS/PPS alle n Bilder wiederholen (Vorgabe: einmal je Sekunde)\n"
                "  -R: laufende Intra-Auffrischung im Kodierer\n"
                "  -s: Bild fuellen statt Seitenverhaeltnis wahren\n"
                "  -A: Mitschnitt-Quelle fuer den Ton (Vorgabe\n"
                "      sink.music.monitor; `off` schaltet den Ton ab)\n"
                "  -P: Nettigkeit des Abgriffs (Vorgabe 0; bringt gemessen nichts)\n",
                argv[0]);
            return 2;
        }
    }
    if (c.dw % 2 || c.dh % 2) { fprintf(stderr, "gerade Maße nötig\n"); return 2; }

    /* --- als root: Framebuffer aufmachen ---------------------------------- */
    c.fd = open(dev, O_RDONLY);
    if (c.fd < 0) { perror(dev); return 1; }
    struct fb_var_screeninfo vi; struct fb_fix_screeninfo fi;
    if (ioctl(c.fd, FBIOGET_VSCREENINFO, &vi) < 0 ||
        ioctl(c.fd, FBIOGET_FSCREENINFO, &fi) < 0) { perror("ioctl"); return 1; }
    if (vi.bits_per_pixel != 16) {
        fprintf(stderr, "nur RGB565, hier %u bpp\n", vi.bits_per_pixel); return 1;
    }
    c.sw = vi.xres; c.sh = vi.yres; c.stride = fi.line_length;
    c.maplen = (size_t)c.stride * vi.yres_virtual;
    c.fb = mmap(NULL, c.maplen, PROT_READ, MAP_SHARED, c.fd, 0);
    if (c.fb == MAP_FAILED) { perror("mmap"); return 1; }
    fprintf(stderr, "fb %dx%d, Zeile %d B, virtuell %ux%u -> %dx%d @ %d/s\n",
            c.sw, c.sh, c.stride, vi.xres_virtual, vi.yres_virtual,
            c.dw, c.dh, c.fps);

    /* --- Vorfahrt sichern, solange wir noch root sind --------------------- */
    /* Nachgemessen bringt die Vorfahrt nichts: unter derselben Speicherlast
     * lief der Abgriff mit Nettigkeit 0 genauso schnell wie mit -10 (7,4
     * gegen 10,6 ms, also eher schlechter). Deshalb standardmäßig aus -- wer
     * sie doch braucht, nimmt -P. */
    if (getuid() == 0 && nice_level) {
        if (setpriority(PRIO_PROCESS, 0, nice_level) == 0)
            fprintf(stderr, "Vorfahrt: Nettigkeit %d\n", nice_level);
        else
            perror("setpriority");
    }

    /* --- Rechte ablegen, bevor GStreamer anfaengt ------------------------- */
    if (getuid() == 0) {
        if (setgid(drop_gid) != 0 || setuid(drop_uid) != 0) {
            perror("setuid"); return 1;
        }
        fprintf(stderr, "Rechte abgelegt auf uid %d\n", (int)drop_uid);
    }

    c.pagesz = (size_t)c.stride * c.sh;
    c.copy = malloc(c.pagesz);
    if (!c.copy) { fprintf(stderr, "kein Speicher fuer die Seite\n"); return 1; }
    c.mapx = malloc(sizeof(int) * c.dw);
    c.mapy = malloc(sizeof(int) * c.dh);
    c.mapx0 = malloc(sizeof(int) * c.dw);
    c.mapy0 = malloc(sizeof(int) * c.dh);

    /* Nicht &argc/&argv: GStreamer wuerde unsere eigenen Schalter sehen und
     * -h als seine Hilfe deuten. */
    gst_init(NULL, NULL);
    if (rot_deg != 0 && rot_deg != 90 && rot_deg != 180 && rot_deg != 270) {
        fprintf(stderr, "Lage muss 0, 90, 180 oder 270 sein\n"); return 2;
    }
    c.rot_deg = rot_deg;
    c.stretch = stretch;
    c.neon = neon;
    c.i420 = i420 && !neon;
    c.vpp = vpp && !i420 && !neon;
    if (c.neon) {
        c.band = malloc((size_t)c.dw * BAND * 2);
        if (!c.band) { fprintf(stderr, "kein Speicher fuer das Band\n"); return 1; }
    }
    c.auto_rot = auto_rot;
    if (auto_rot && orient_open() == 0) {
        int a = orient_angle();
        if (a >= 0) rot_deg = (360 - a) % 360;
        fprintf(stderr, "Lage vom Compositor: %d Grad -> Drehung %d\n", a, rot_deg);
    } else if (auto_rot) {
        fprintf(stderr, "Lage nicht erfragbar, bleibe bei %d Grad\n", rot_deg);
        c.auto_rot = 0;
    }
    set_rotation(&c, rot_deg);
    c.ts = ts_mode;
    /* mode=streaming statt storage, und Schluesselbilder im festen Abstand:
     * ohne das liefert der DSP genau ein Schluesselbild beim Start, und ein
     * Empfaenger, der spaeter zuhoert, bekommt nie wieder SPS/PPS zu sehen
     * (nachgemessen: 1 Schluesselbild auf 40 Bilder). */
    gchar *encopt = g_strdup_printf("mode=streaming keyframe-interval=%d%s%s",
                                    keyint,
                                    bitrate ? g_strdup_printf(" bitrate=%d", bitrate) : "",
                                    intra ? " intra-refresh=true" : "");
    gchar *tail = ts_mode
        ? g_strdup("appsink name=tssink sync=false emit-signals=true "
                   "max-buffers=8 drop=false")
        : sink ? g_strdup(sink)
                       : g_strdup_printf("rtph264pay config-interval=1 "
                                         "! udpsink host=%s port=%d", host, port);
    gchar *desc;
    if (c.neon || c.i420)
        desc = g_strdup_printf(
            "appsrc name=src is-live=true do-timestamp=false format=time "
            "caps=\"video/x-raw-yuv,format=(fourcc)I420,width=(int)%d,"
            "height=(int)%d,framerate=(fraction)%d/1\" "
            "! dsph264enc %s ! %s", c.dw, c.dh, c.fps, encopt, tail);
    else if (c.vpp)
        desc = g_strdup_printf(
            "appsrc name=src is-live=true do-timestamp=false format=time "
            "caps=\"video/x-raw-rgb,bpp=(int)16,depth=(int)16,"
            "endianness=(int)1234,red_mask=(int)63488,green_mask=(int)2016,"
            "blue_mask=(int)31,width=(int)%d,height=(int)%d,framerate=(fraction)%d/1\" "
            "! dspvpp ! dsph264enc %s ! %s", c.dw, c.dh, c.fps, encopt, tail);
    else
        desc = g_strdup_printf(
            "appsrc name=src is-live=true do-timestamp=false format=time "
            "caps=\"video/x-raw-rgb,bpp=(int)16,depth=(int)16,"
            "endianness=(int)1234,red_mask=(int)63488,green_mask=(int)2016,"
            "blue_mask=(int)31,width=(int)%d,height=(int)%d,framerate=(fraction)%d/1\" "
            "! ffmpegcolorspace ! video/x-raw-yuv,format=(fourcc)I420 "
            "! dsph264enc %s ! %s", c.dw, c.dh, c.fps, encopt, tail);
    g_free(tail);
    g_free(encopt);

    GError *err = NULL;
    c.pipeline = gst_parse_launch(desc, &err);
    if (!c.pipeline) {
        g_printerr("Pipeline geht nicht: %s\n", err ? err->message : "?");
        return 1;
    }
    fprintf(stderr, "Pipeline: %s\n", desc);
    g_free(desc);
    c.src = gst_bin_get_by_name(GST_BIN(c.pipeline), "src");
    c.srcpad = gst_element_get_static_pad(c.src, "src");
    {
        GstIterator *it = gst_bin_iterate_elements(GST_BIN(c.pipeline));
        gpointer e = NULL;
        while (gst_iterator_next(it, &e) == GST_ITERATOR_OK) {
            const gchar *n = gst_element_get_name(GST_ELEMENT(e));
            if (n && strstr(n, "dsph264enc")) { c.enc = GST_ELEMENT(e); break; }
            gst_object_unref(GST_OBJECT(e));
        }
        gst_iterator_free(it);
    }
    c.keyint = keyint;
    if (c.ts) {
        c.amon = (amon && strcmp(amon, "off")) ? amon : NULL;
        c.tssink = ts_sink_new(c.dw, c.dh, c.fps, host, port,
                               csd_every > 0 ? csd_every : c.fps,
                               c.amon ? 1 : 0);
        if (!c.tssink) {
            g_printerr("TS-Sender geht nicht auf (%s:%d)\n", host, port);
            return 1;
        }
        GstElement *as = gst_bin_get_by_name(GST_BIN(c.pipeline), "tssink");
        g_signal_connect(as, "new-buffer", G_CALLBACK(on_encoded), &c);
        if (c.amon) {
            /* Eigene Pipeline: in einer gemeinsamen mit appsrc und appsink
             * blieb der Tonzweig unverbunden (`not-linked`). Die Zeitstempel
             * kommen fuer Bild und Ton aus derselben Uhr (c.t0_us), damit
             * bleibt die Lippensynchronitaet erhalten. */
            gchar *ad = g_strdup_printf(
                "pulsesrc device=%s ! "
                "audio/x-raw-int,rate=48000,channels=2,width=16,depth=16,"
                "signed=true,endianness=1234 ! "
                "appsink name=asink sync=false emit-signals=true "
                "max-buffers=16 drop=true", c.amon);
            GError *aerr = NULL;
            c.apipe = gst_parse_launch(ad, &aerr);
            g_free(ad);
            if (!c.apipe) {
                fprintf(stderr, "Ton geht nicht auf (%s), bleibe stumm\n",
                        aerr ? aerr->message : "?");
                c.amon = NULL;
            } else {
                GstElement *au = gst_bin_get_by_name(GST_BIN(c.apipe), "asink");
                g_signal_connect(au, "new-buffer", G_CALLBACK(on_audio), &c);
                fprintf(stderr, "Ton aus %s\n", c.amon);
            }
        }
        fprintf(stderr, "MPEG-TS/RTP nach %s:%d\n", host, port);
    }
    c.loop = g_main_loop_new(NULL, FALSE);
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(c.pipeline));
    gst_bus_add_watch(bus, on_bus, &c);
    gst_object_unref(bus);

    c.t0_us = now_us();
    gst_element_set_state(c.pipeline, GST_STATE_PLAYING);
    if (c.apipe) gst_element_set_state(c.apipe, GST_STATE_PLAYING);
    c.t0 = now_us();
    g_timeout_add(1000 / c.fps, grab, &c);
    g_main_loop_run(c.loop);

    gst_element_set_state(c.pipeline, GST_STATE_NULL);
    if (c.apipe) gst_element_set_state(c.apipe, GST_STATE_NULL);
    orient_close();
    if (c.tssink) {
        fprintf(stderr, "%lld Zugriffseinheiten, %lld Tonblöcke, "
                "%lu TS-Sendungen, %lu Byte\n", c.aus, c.apackets,
                ts_sink_packets(c.tssink), ts_sink_bytes(c.tssink));
        ts_sink_free(c.tssink);
    }
    {
        double dt = (now_us() - c.t0) / 1e6;
        if (c.frames) fprintf(stderr,
                "je Bild: kopieren %.1f ms, drehen %.1f ms, wandeln %.1f ms\n",
                c.copy_us / 1000.0 / c.frames, c.rot_us / 1000.0 / c.frames,
                c.conv_us / 1000.0 / c.frames);
        fprintf(stderr, "%lld Bilder in %.1f s = %.1f/s (Ziel %d/s), "
                "Abgriff je Bild %.1f ms\n", c.frames, dt,
                dt > 0 ? c.frames / dt : 0.0, c.fps,
                c.frames ? c.grab_us / 1000.0 / c.frames : 0.0);
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) == 0 && dt > 0) {
            double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 +
                         ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
            fprintf(stderr, "Rechenzeit %.1f s = %.0f %% einer CPU\n",
                    cpu, cpu / dt * 100.0);
        }
    }
    return 0;
}
