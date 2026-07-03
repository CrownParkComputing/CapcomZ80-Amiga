#ifndef CAPCOM_Z80_VIDEO_H
#define CAPCOM_Z80_VIDEO_H

#include <stdint.h>
#include <string.h>

#define CAPCOM_Z80_SRC_W 256
#define CAPCOM_Z80_SRC_H 224
#define CAPCOM_Z80_GAME_W 224
#define CAPCOM_Z80_GAME_H 256
#define CAPCOM_Z80_ROT_YOFF 16
#define CAPCOM_Z80_FRAME_SIZE (CAPCOM_Z80_GAME_W * CAPCOM_Z80_GAME_H)

static inline int capcom_z80_bit(const unsigned char *p, unsigned bit)
{
    return (p[bit >> 3] >> (7 - (bit & 7))) & 1;
}

static inline int capcom_z80_weight4(int v)
{
    v &= 0xf;
    return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) +
           0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1);
}

static inline uint8_t capcom_z80_rgb332(unsigned r, unsigned g, unsigned b)
{
    return (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

static inline void capcom_z80_clear_frame(uint8_t *frame, uint8_t pen)
{
    memset(frame, pen, CAPCOM_Z80_FRAME_SIZE);
}

static inline uint8_t *capcom_z80_rotated_ptr(uint8_t *frame, int absx, int absy)
{
    int x = absy - CAPCOM_Z80_ROT_YOFF;
    int y = 255 - absx;
    if ((unsigned)x >= CAPCOM_Z80_GAME_W || (unsigned)y >= CAPCOM_Z80_GAME_H)
        return 0;
    return frame + (unsigned)y * CAPCOM_Z80_GAME_W + x;
}

static inline void capcom_z80_put_rotated(uint8_t *frame, int absx, int absy, uint8_t pen)
{
    uint8_t *p = capcom_z80_rotated_ptr(frame, absx, absy);
    if (p) *p = pen;
}

void capcom_z80_palette_rgb332_weighted4(const unsigned char *proms, uint8_t *out);
void capcom_z80_palette_rgb888_linear4(const unsigned char *proms, uint8_t *out);
void capcom_z80_scale_rgb332(const uint8_t *src, const uint8_t *palette,
                             uint8_t *dst, int dst_stride, int dst_w, int dst_h);

#endif
