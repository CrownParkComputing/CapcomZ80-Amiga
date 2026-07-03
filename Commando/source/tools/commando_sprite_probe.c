#include "z80emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ccommando_load(const unsigned char *maincpu);
void ccommando_init(MY_LITTLE_Z80 *z);
void ccommando_run_frame(MY_LITTLE_Z80 *z);
void ccommando_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2);
void ccommando_set_dsw(unsigned char dsw0, unsigned char dsw1);
unsigned char ccommando_spritebuf_peek(unsigned o);
unsigned char ccommando_peek(MY_LITTLE_Z80 *z, unsigned a);

static unsigned char mainrom[0xc000];
static MY_LITTLE_Z80 z;

static void load_file(const char *path, unsigned char *dst, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    if (fread(dst, 1, n, f) != n) { fprintf(stderr, "short read: %s\n", path); exit(1); }
    fclose(f);
}

static int count_heli(void)
{
    int n = 0;
    for (unsigned o = 0; o < 0x180; o += 4) {
        int b0 = ccommando_spritebuf_peek(o + 0);
        int b1 = ccommando_spritebuf_peek(o + 1);
        int code = b0 | ((b1 & 0xc0) << 2);
        int col = (b1 >> 4) & 3;
        if (col == 1 && code >= 736 && code <= 767) n++;
    }
    return n;
}

static void dump_heli(int frame)
{
    printf("frame %d heli=%d scroll=%03x,%03x pc=%04x\n", frame, count_heli(),
           ccommando_peek(&z, 0xc808) | (ccommando_peek(&z, 0xc809) << 8),
           ccommando_peek(&z, 0xc80a) | (ccommando_peek(&z, 0xc80b) << 8),
           z.state.pc & 0xffff);
}

static void dump_sprites(int frame)
{
    int n = 0;
    printf("frame %d scroll=%03x,%03x pc=%04x e000=%02x eda0=%02x\n",
           frame,
           ccommando_peek(&z, 0xc808) | (ccommando_peek(&z, 0xc809) << 8),
           ccommando_peek(&z, 0xc80a) | (ccommando_peek(&z, 0xc80b) << 8),
           z.state.pc & 0xffff, ccommando_peek(&z, 0xe000), ccommando_peek(&z, 0xeda0));
    for (unsigned o = 0; o < 0x180; o += 4) {
        int b0 = ccommando_spritebuf_peek(o + 0);
        int b1 = ccommando_spritebuf_peek(o + 1);
        int b2 = ccommando_spritebuf_peek(o + 2);
        int b3 = ccommando_spritebuf_peek(o + 3);
        if (!(b0 | b1 | b2 | b3)) continue;
        int code = b0 | ((b1 & 0xc0) << 2);
        int col = (b1 >> 4) & 3;
        printf("  e=%02u code=%03d col=%d sx=%4d sy=%3d attr=%02x raw=%02x %02x %02x %02x\n",
               o / 4, code, col, b3 - ((b1 & 1) << 8), b2, b1, b0, b1, b2, b3);
        if (++n >= 24) break;
    }
}

int main(void)
{
    load_file("build/rcommando/main.bin", mainrom, sizeof mainrom);
    ccommando_load(mainrom);
    ccommando_set_dsw(0xff, 0x1f);
    ccommando_init(&z);
    ccommando_set_dsw(0xff, 0x1f);

    for (int f = 0; f < 900; f++) {
        unsigned char sys = 0xff;
        if (f >= 20 && f < 30) sys &= (unsigned char)~0x80;  /* coin */
        if (f >= 50 && f < 90) sys &= (unsigned char)~0x01;  /* start */
        ccommando_set_inputs(sys, 0xff, 0xff);
        ccommando_run_frame(&z);
        if (f == 120 || f == 180 || f == 240 || f == 300 || f == 360 || count_heli()) {
            dump_sprites(f);
        }
        if (count_heli()) {
            printf("frame %d heli=%d scroll=%03x,%03x pc=%04x e000=%02x eda0=%02x\n",
                   f, count_heli(),
                   ccommando_peek(&z, 0xc808) | (ccommando_peek(&z, 0xc809) << 8),
                   ccommando_peek(&z, 0xc80a) | (ccommando_peek(&z, 0xc80b) << 8),
                   z.state.pc & 0xffff, ccommando_peek(&z, 0xe000), ccommando_peek(&z, 0xeda0));
            for (unsigned o = 0; o < 0x180; o += 4) {
                int b0 = ccommando_spritebuf_peek(o + 0);
                int b1 = ccommando_spritebuf_peek(o + 1);
                int b2 = ccommando_spritebuf_peek(o + 2);
                int b3 = ccommando_spritebuf_peek(o + 3);
                int code = b0 | ((b1 & 0xc0) << 2);
                int col = (b1 >> 4) & 3;
                if (col == 1 && code >= 736 && code <= 767)
                    printf("  e=%02u code=%03d col=%d sx=%4d sy=%3d attr=%02x raw=%02x %02x %02x %02x\n",
                           o / 4, code, col, b3 - ((b1 & 1) << 8), b2, b1, b0, b1, b2, b3);
            }
            break;
        }
    }
    return 0;
}
