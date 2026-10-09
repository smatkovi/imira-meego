/* fbcap -- Bildschirm des N9/N950 abgreifen und als I420-Rohbild ausgeben.
 *
 * Harmattan hat keinen Lipstick-Recorder und kein Wayland: das fertige Bild
 * steht im Framebuffer. /dev/fb0 ist RGB565, 856 Bytes je Zeile mal zwei,
 * doppelt gepuffert (virtuell 856x1536) und um 90 Grad gedreht gegenueber dem
 * Querformat, das ein Fernseher sehen will. Gemessen am N950: der Puffer liest
 * mit 25,8 MB/s, ein Vollbild (822 kB) kostet also 33 ms -- deshalb wird nur
 * die sichtbare Seite gelesen (FBIOGET_VSCREENINFO sagt, welche) und die
 * Umrechnung bleibt so schlank wie moeglich.
 *
 * Ausgabe: I420 (Y-Ebene, dann U, dann V) nach stdout, ein Bild je Takt, in
 * Querformat gedreht. Damit kann die Kette ohne eigenen Kodierer geprueft
 * werden:
 *
 *   fbcap -w 848 -h 480 -f 15 | gst-launch-0.10 fdsrc blocksize=610560 ! \
 *     'video/x-raw-yuv,format=(fourcc)I420,width=848,height=480,framerate=15/1' ! \
 *     dsph264enc ! rtph264pay ! udpsink host=... port=5000
 *
 * Braucht root (fb0 gehoert der Gruppe video).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

static long long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* RGB565 -> Y/U/V, BT.601, ganzzahlig. Die Quelle ist um 90 Grad gedreht:
 * Quellzeile y, Spalte x landet im Ziel bei (sh-1-y, x) -- also Querformat.
 * Zusaetzlich wird auf die Zielgroesse skaliert (nearest, einmal vorberechnet). */
struct map { int *sx, *sy; };

static void build_map(struct map *m, int dw, int dh, int sw, int sh)
{
    int i;
    m->sx = malloc(sizeof(int) * dw);
    m->sy = malloc(sizeof(int) * dh);
    /* Ziel-x laeuft ueber die Quell-y-Achse (gedreht), Ziel-y ueber Quell-x. */
    for (i = 0; i < dw; i++)
        m->sx[i] = (int)((long long)i * sh / dw);
    for (i = 0; i < dh; i++)
        m->sy[i] = (int)((long long)i * sw / dh);
}

int main(int argc, char **argv)
{
    const char *dev = "/dev/fb0";
    int dw = 848, dh = 480, fps = 15, frames = 0, c, rgb = 0;
    while ((c = getopt(argc, argv, "d:w:h:f:n:R")) != -1) {
        switch (c) {
        case 'd': dev = optarg; break;
        case 'R': rgb = 1; break;   /* gedrehtes RGB565 statt I420 */
        case 'w': dw = atoi(optarg); break;
        case 'h': dh = atoi(optarg); break;
        case 'f': fps = atoi(optarg); break;
        case 'n': frames = atoi(optarg); break;
        default:
            fprintf(stderr, "Aufruf: %s [-d /dev/fb0] [-w 848] [-h 480]"
                            " [-f 15] [-n Bilder] [-R]\n"
                            "  -R: gedrehtes RGB565 ausgeben (die Umrechnung"
                            " nach I420 macht dann ffmpegcolorspace, dessen"
                            " Ausgabepuffer der DSP abbilden kann)\n", argv[0]);
            return 2;
        }
    }
    if (dw % 2 || dh % 2) { fprintf(stderr, "Breite und Hoehe muessen gerade sein\n"); return 2; }

    int fd = open(dev, O_RDONLY);
    if (fd < 0) { perror(dev); return 1; }
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0) { perror("ioctl"); return 1; }
    if (vi.bits_per_pixel != 16) {
        fprintf(stderr, "nur RGB565 (16 bpp) wird gelesen, hier %u bpp\n",
                vi.bits_per_pixel);
        return 1;
    }
    int sw = vi.xres, sh = vi.yres, stride = fi.line_length;
    fprintf(stderr, "fb: %dx%d, %u bpp, Zeile %d B, virtuell %ux%u\n",
            sw, sh, vi.bits_per_pixel, stride, vi.xres_virtual, vi.yres_virtual);

    size_t maplen = (size_t)stride * vi.yres_virtual;
    unsigned char *fb = mmap(NULL, maplen, PROT_READ, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }

    struct map m;
    build_map(&m, dw, dh, sw, sh);
    size_t ysz = (size_t)dw * dh, csz = ysz / 4;
    unsigned char *y = malloc(ysz), *u = malloc(csz), *v = malloc(csz);
    unsigned short *rp = rgb ? malloc(ysz * 2) : NULL;
    if (!y || !u || !v || (rgb && !rp)) { fprintf(stderr, "kein Speicher\n"); return 1; }

    long long period = 1000000LL / (fps > 0 ? fps : 15), next = now_us();
    long long n = 0, t0 = now_us();
    for (;;) {
        /* sichtbare Seite: yoffset sagt, welche Haelfte gerade gezeigt wird */
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) == 0) { }
        const unsigned char *page = fb + (size_t)vi.yoffset * stride;

        if (rgb) {
            /* nur drehen und skalieren, Farben bleiben RGB565 */
            for (int dy = 0; dy < dh; dy++) {
                int sx = m.sy[dy];
                unsigned short *rl = rp + (size_t)dy * dw;
                for (int dx = 0; dx < dw; dx++) {
                    int sy = sh - 1 - m.sx[dx];
                    rl[dx] = *((const unsigned short *)
                        (page + (size_t)sy * stride) + sx);
                }
            }
            if (fwrite(rp, 1, ysz * 2, stdout) != ysz * 2) break;
            fflush(stdout);
            goto paced;
        }
        for (int dy = 0; dy < dh; dy++) {
            int sx = m.sy[dy];                 /* Quell-Spalte */
            unsigned char *yl = y + (size_t)dy * dw;
            for (int dx = 0; dx < dw; dx++) {
                int sy = sh - 1 - m.sx[dx];    /* Quell-Zeile, gedreht */
                const unsigned short *p = (const unsigned short *)
                    (page + (size_t)sy * stride) + sx;
                unsigned short px = *p;
                int r = ((px >> 11) & 0x1f) << 3;
                int g = ((px >> 5) & 0x3f) << 2;
                int b = (px & 0x1f) << 3;
                yl[dx] = (unsigned char)((66 * r + 129 * g + 25 * b + 4224) >> 8);
                if ((dy & 1) == 0 && (dx & 1) == 0) {
                    size_t ci = (size_t)(dy >> 1) * (dw >> 1) + (dx >> 1);
                    u[ci] = (unsigned char)((-38 * r - 74 * g + 112 * b + 32768) >> 8);
                    v[ci] = (unsigned char)((112 * r - 94 * g - 18 * b + 32768) >> 8);
                }
            }
        }
        if (fwrite(y, 1, ysz, stdout) != ysz ||
            fwrite(u, 1, csz, stdout) != csz ||
            fwrite(v, 1, csz, stdout) != csz) break;
        fflush(stdout);
paced:
        n++;
        if (frames && n >= frames) break;
        next += period;
        long long wait = next - now_us();
        if (wait > 0) usleep((useconds_t)wait);
        else next = now_us();          /* zu langsam: Takt neu setzen */
    }
    long long dt = now_us() - t0;
    fprintf(stderr, "%lld Bilder in %lld ms = %.1f/s\n", n, dt / 1000,
            dt ? n * 1000000.0 / dt : 0.0);
    return 0;
}
