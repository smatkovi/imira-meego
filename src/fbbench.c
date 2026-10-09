/* Wo geht die Zeit hin? Drei Wege, denselben Bildschirminhalt zu holen:
 *   A  die sichtbare Seite am Stueck in den Arbeitsspeicher kopieren
 *   B  direkt aus dem Framebuffer drehen (das macht castd heute)
 *   C  erst kopieren, dann aus dem Arbeitsspeicher drehen
 *   D  wie C, aber in Kacheln gedreht
 *   E  wie D, und gleich nach I420 umgerechnet (dann faellt ffmpegcolorspace weg)
 * Framebuffer-Speicher ist nicht zwischengespeichert; ob Kacheln helfen,
 * entscheidet erst die Messung. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

static long long us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (long long)t.tv_sec*1000000+t.tv_nsec/1000;}

#define DW 848
#define DH 480
#define TILE 32

int main(void)
{
    int fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0) { perror("/dev/fb0"); return 1; }
    struct fb_var_screeninfo vi; struct fb_fix_screeninfo fi;
    ioctl(fd, FBIOGET_VSCREENINFO, &vi); ioctl(fd, FBIOGET_FSCREENINFO, &fi);
    int sw = vi.xres, sh = vi.yres, stride = fi.line_length;
    size_t maplen = (size_t)stride * vi.yres_virtual;
    unsigned char *fb = mmap(NULL, maplen, PROT_READ, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }
    const unsigned char *page = fb + (size_t)vi.yoffset * stride;
    printf("fb %dx%d Zeile %d, Seite %u\n", sw, sh, stride, vi.yoffset);

    size_t pagesz = (size_t)stride * sh;
    unsigned char *copy = malloc(pagesz);
    unsigned short *out = malloc((size_t)DW * DH * 2);
    unsigned char *yp = malloc((size_t)DW*DH), *up = malloc((size_t)DW*DH/4),
                  *vp = malloc((size_t)DW*DH/4);
    int mx[DW], my[DH];
    for (int i = 0; i < DW; i++) mx[i] = (int)((long long)i * sh / DW);
    for (int i = 0; i < DH; i++) my[i] = (int)((long long)i * sw / DH);
    const int N = 20;
    long long t;

    t = us();
    for (int k = 0; k < N; k++) memcpy(copy, page, pagesz);
    printf("A  Seite kopieren        %6.1f ms  (%.1f MB/s)\n",
           (us()-t)/1000.0/N, pagesz*N/((us()-t)/1e6)/1e6);

    t = us();
    for (int k = 0; k < N; k++)
        for (int dy = 0; dy < DH; dy++) {
            int sx = my[dy]; unsigned short *row = out + (size_t)dy*DW;
            for (int dx = 0; dx < DW; dx++)
                row[dx] = *((const unsigned short *)(page + (size_t)(sh-1-mx[dx])*stride) + sx);
        }
    printf("B  aus fb drehen         %6.1f ms\n", (us()-t)/1000.0/N);

    t = us();
    for (int k = 0; k < N; k++) {
        memcpy(copy, page, pagesz);
        for (int dy = 0; dy < DH; dy++) {
            int sx = my[dy]; unsigned short *row = out + (size_t)dy*DW;
            for (int dx = 0; dx < DW; dx++)
                row[dx] = *((const unsigned short *)(copy + (size_t)(sh-1-mx[dx])*stride) + sx);
        }
    }
    printf("C  kopieren + drehen      %6.1f ms\n", (us()-t)/1000.0/N);

    t = us();
    for (int k = 0; k < N; k++) {
        memcpy(copy, page, pagesz);
        for (int ty = 0; ty < DH; ty += TILE)
            for (int tx = 0; tx < DW; tx += TILE)
                for (int dy = ty; dy < ty+TILE && dy < DH; dy++) {
                    int sx = my[dy]; unsigned short *row = out + (size_t)dy*DW;
                    for (int dx = tx; dx < tx+TILE && dx < DW; dx++)
                        row[dx] = *((const unsigned short *)(copy + (size_t)(sh-1-mx[dx])*stride) + sx);
                }
    }
    printf("D  kopieren + Kacheln     %6.1f ms\n", (us()-t)/1000.0/N);

    t = us();
    for (int k = 0; k < N; k++) {
        memcpy(copy, page, pagesz);
        for (int dy = 0; dy < DH; dy++) {
            int sx = my[dy];
            unsigned char *yl = yp + (size_t)dy*DW;
            for (int dx = 0; dx < DW; dx++) {
                unsigned short px = *((const unsigned short *)(copy + (size_t)(sh-1-mx[dx])*stride) + sx);
                int r = ((px>>11)&0x1f)<<3, g = ((px>>5)&0x3f)<<2, b = (px&0x1f)<<3;
                yl[dx] = (unsigned char)((66*r+129*g+25*b+4224)>>8);
                if (!(dy&1) && !(dx&1)) {
                    size_t ci = (size_t)(dy>>1)*(DW>>1)+(dx>>1);
                    up[ci] = (unsigned char)((-38*r-74*g+112*b+32768)>>8);
                    vp[ci] = (unsigned char)((112*r-94*g-18*b+32768)>>8);
                }
            }
        }
    }
    printf("E  kopieren + I420        %6.1f ms\n", (us()-t)/1000.0/N);
    return 0;
}
