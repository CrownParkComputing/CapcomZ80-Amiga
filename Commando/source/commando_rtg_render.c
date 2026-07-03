#include "commando_rtg_render.h"
#include <string.h>

extern const unsigned char commando_rom_g1[], commando_rom_g2[], commando_rom_g3[], commando_rom_proms[];
extern unsigned char ccommando_peek(MY_LITTLE_Z80 *z, unsigned a);
extern int ccommando_scrollx(void);
extern int ccommando_scrolly(void);
extern int ccommando_control(void);

static unsigned char frame[CAPCOM_Z80_FRAME_SIZE];
static unsigned char char_px[1024][64];
static unsigned char bg_px[1024][256];
static unsigned char spr_px[768][256];
static unsigned char pen8[256];
static int ready;

static const int CXO[8] = { 0,1,2,3,8,9,10,11 };
static const int SXO[16] = { 0,1,2,3,8,9,10,11,256,257,258,259,264,265,266,267 };

static int cpix(int code, int x, int y){
    unsigned o = (unsigned)code * 128 + (unsigned)y * 16 + CXO[x];
    return (capcom_z80_bit(commando_rom_g1, o + 4) << 1) | capcom_z80_bit(commando_rom_g1, o);
}

static int bgpix(int code, int x, int y){
    unsigned xo = (x < 8) ? (unsigned)x : 128u + (unsigned)(x - 8);
    unsigned o = (unsigned)code * 256 + (unsigned)y * 8 + xo;
    return (capcom_z80_bit(commando_rom_g2, o) << 2) |
           (capcom_z80_bit(commando_rom_g2, o + 0x40000) << 1) |
           capcom_z80_bit(commando_rom_g2, o + 0x80000);
}

static int sppix(int code, int x, int y){
    const unsigned H = 0x60000;
    unsigned o = (unsigned)code * 512 + (unsigned)y * 16 + SXO[x];
    return (capcom_z80_bit(commando_rom_g3, o + H + 4) << 3) |
           (capcom_z80_bit(commando_rom_g3, o + H) << 2) |
           (capcom_z80_bit(commando_rom_g3, o + 4) << 1) |
           capcom_z80_bit(commando_rom_g3, o);
}

void commando_rtg_render_init(void){
    if(ready) return;
    capcom_z80_palette_rgb332_weighted4(commando_rom_proms, pen8);
    for(int cd=0; cd<1024; cd++)
        for(int y=0; y<8; y++)
            for(int x=0; x<8; x++)
                char_px[cd][y*8+x] = (unsigned char)cpix(cd, x, y);
    for(int cd=0; cd<1024; cd++)
        for(int y=0; y<16; y++)
            for(int x=0; x<16; x++)
                bg_px[cd][y*16+x] = (unsigned char)bgpix(cd, x, y);
    for(int cd=0; cd<768; cd++)
        for(int y=0; y<16; y++)
            for(int x=0; x<16; x++)
                spr_px[cd][y*16+x] = (unsigned char)sppix(cd, x, y);
    ready = 1;
}

static void draw_bg(MY_LITTLE_Z80 *z){
    int scx = ccommando_scrollx() & 0x1ff, scy = ccommando_scrolly() & 0x1ff;
    int ctcol = scx >> 4, fhx = scx & 15;
    int ctrow = scy >> 4, fvy = scy & 15;
    capcom_z80_clear_frame(frame, 0);
    for(int i=0;i<=16;i++) for(int j=0;j<=16;j++){
        int wtrow = (ctrow + i) & 31, wtcol = (ctcol + j) & 31;
        int idx = wtcol * 32 + wtrow;
        int code = ccommando_peek(z, 0xd800 + idx);
        int attr = ccommando_peek(z, 0xdc00 + idx);
        int cd = (code | ((attr & 0xc0) << 2)) & 1023;
        int col = attr & 0x0f, fx = attr & 0x10, fy = attr & 0x20;
        int nv0 = i * 16 - fvy, nh0 = j * 16 - fhx;
        const unsigned char *tile = bg_px[cd];
        for(int y=0;y<16;y++){
            int nv = nv0 + y; if((unsigned)nv >= 256) continue;
            int yy = fy ? 15 - y : y;
            for(int x=0;x<16;x++){
                int nh = nh0 + x; if((unsigned)nh >= 256) continue;
                int xx = fx ? 15 - x : x;
                int px = tile[yy*16+xx];
                unsigned char pen = (unsigned char)(col * 8 + px);
                capcom_z80_put_rotated(frame, nh, nv, pen);
            }
        }
    }
}

static void draw_sprite(int code, int col, int fx, int fy, int sx, int sy){
    if((unsigned)code >= 768) return;
    const unsigned char *tile = spr_px[code];
    for(int y=0;y<16;y++){
        int yy = sy + y; if((unsigned)yy >= 256) continue;
        int ty = fy ? 15 - y : y;
        for(int x=0;x<16;x++){
            int xx = sx + x; if((unsigned)xx >= 256) continue;
            int tx = fx ? 15 - x : x;
            int px = tile[ty*16+tx];
            if(px != 15) capcom_z80_put_rotated(frame, xx, yy, (unsigned char)(128 + col * 16 + px));
        }
    }
}

static void draw_sprites(MY_LITTLE_Z80 *z){
    int flip = ccommando_control() & 0x80;
    for(unsigned a=0xff7c; a>=0xfe00; a-=4){
        int b0 = ccommando_peek(z, a + 0);
        int b1 = ccommando_peek(z, a + 1);
        int b2 = ccommando_peek(z, a + 2);
        int b3 = ccommando_peek(z, a + 3);
        if(!(b0 | b1 | b2 | b3)) {
            if(a == 0xfe00) break;
            continue;
        }
        int code = b0 | ((b1 & 0xc0) << 2);
        if(code >= 768) {
            if(a == 0xfe00) break;
            continue;
        }
        int col = (b1 >> 4) & 3;
        int fx = b1 & 0x04;
        int fy = b1 & 0x08;
        int sx = b3 - ((b1 & 0x01) << 8);
        int sy = b2;
        if(sy < 16 || sy > 239) {
            if(a == 0xfe00) break;
            continue;
        }
        if(flip){
            sx = 240 - sx;
            sy = 240 - sy;
            fx = !fx;
            fy = !fy;
        }
        draw_sprite(code, col, fx, fy, sx, sy);
        if(a == 0xfe00) break;
    }
}

static void draw_fg(MY_LITTLE_Z80 *z){
    for(int crow=0; crow<32; crow++) for(int ccol=0; ccol<32; ccol++){
        int idx = crow * 32 + ccol;
        int code = ccommando_peek(z, 0xd000 + idx);
        int attr = ccommando_peek(z, 0xd400 + idx);
        int cd = (code | ((attr & 0xc0) << 2)) & 1023;
        int col = attr & 0x0f, fx = attr & 0x10, fy = attr & 0x20;
        const unsigned char *tile = char_px[cd];
        for(int y=0;y<8;y++){
            int yy = crow * 8 + y;
            int ty = fy ? 7 - y : y;
            for(int x=0;x<8;x++){
                int xx = ccol * 8 + x;
                int tx = fx ? 7 - x : x;
                int px = tile[ty*8+tx];
                if(px != 3) capcom_z80_put_rotated(frame, xx, yy, (unsigned char)(192 + col * 4 + px));
            }
        }
    }
}

void commando_rtg_render(MY_LITTLE_Z80 *z, uint8_t *dst, int dst_stride, int dst_w, int dst_h){
    if(!ready) commando_rtg_render_init();
    draw_bg(z);
    draw_sprites(z);
    draw_fg(z);
    capcom_z80_scale_rgb332(frame, pen8, dst, dst_stride, dst_w, dst_h);
}
