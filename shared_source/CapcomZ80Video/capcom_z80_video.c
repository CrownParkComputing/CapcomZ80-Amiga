#include "capcom_z80_video.h"
#include <string.h>

int capcom_z80_bit(const unsigned char *p, unsigned bit)
{
    return (p[bit >> 3] >> (7 - (bit & 7))) & 1;
}

int capcom_z80_weight4(int v)
{
    v &= 0xf;
    return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) +
           0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1);
}

uint8_t capcom_z80_rgb332(unsigned r, unsigned g, unsigned b)
{
    return (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

void capcom_z80_palette_rgb332_weighted4(const unsigned char *proms, uint8_t *out)
{
    for (int i = 0; i < 256; i++)
        out[i] = capcom_z80_rgb332((unsigned)capcom_z80_weight4(proms[0x000 + i]),
                                   (unsigned)capcom_z80_weight4(proms[0x100 + i]),
                                   (unsigned)capcom_z80_weight4(proms[0x200 + i]));
}

void capcom_z80_palette_rgb888_linear4(const unsigned char *proms, uint8_t *out)
{
    for (int i = 0; i < 256; i++) {
        out[i * 3 + 0] = (uint8_t)((proms[0x000 + i] & 0xf) * 17);
        out[i * 3 + 1] = (uint8_t)((proms[0x100 + i] & 0xf) * 17);
        out[i * 3 + 2] = (uint8_t)((proms[0x200 + i] & 0xf) * 17);
    }
}

void capcom_z80_clear_frame(uint8_t *frame, uint8_t pen)
{
    memset(frame, pen, CAPCOM_Z80_FRAME_SIZE);
}

uint8_t *capcom_z80_rotated_ptr(uint8_t *frame, int absx, int absy)
{
    int x = absy - CAPCOM_Z80_ROT_YOFF;
    int y = 255 - absx;
    if ((unsigned)x >= CAPCOM_Z80_GAME_W || (unsigned)y >= CAPCOM_Z80_GAME_H)
        return 0;
    return frame + (size_t)y * CAPCOM_Z80_GAME_W + x;
}

void capcom_z80_put_rotated(uint8_t *frame, int absx, int absy, uint8_t pen)
{
    uint8_t *p = capcom_z80_rotated_ptr(frame, absx, absy);
    if (p) *p = pen;
}

void capcom_z80_scale_rgb332(const uint8_t *src, const uint8_t *palette,
                             uint8_t *dst, int dst_stride, int dst_w, int dst_h)
{
    for (int y = 0; y < dst_h; y++) {
        int sy = (y * CAPCOM_Z80_GAME_H) / dst_h;
        uint8_t *d = dst + (size_t)y * dst_stride;
        const uint8_t *s = src + (size_t)sy * CAPCOM_Z80_GAME_W;
        for (int x = 0; x < dst_w; x++) {
            int sx = (x * CAPCOM_Z80_GAME_W) / dst_w;
            d[x] = palette[s[sx]];
        }
    }
}
