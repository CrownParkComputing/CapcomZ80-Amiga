#ifndef CAPCOM_Z80_VIDEO_H
#define CAPCOM_Z80_VIDEO_H

#include <stdint.h>

#define CAPCOM_Z80_SRC_W 256
#define CAPCOM_Z80_SRC_H 224
#define CAPCOM_Z80_GAME_W 224
#define CAPCOM_Z80_GAME_H 256
#define CAPCOM_Z80_ROT_YOFF 16
#define CAPCOM_Z80_FRAME_SIZE (CAPCOM_Z80_GAME_W * CAPCOM_Z80_GAME_H)

int capcom_z80_bit(const unsigned char *p, unsigned bit);
int capcom_z80_weight4(int v);
uint8_t capcom_z80_rgb332(unsigned r, unsigned g, unsigned b);
void capcom_z80_palette_rgb332_weighted4(const unsigned char *proms, uint8_t *out);
void capcom_z80_palette_rgb888_linear4(const unsigned char *proms, uint8_t *out);
void capcom_z80_clear_frame(uint8_t *frame, uint8_t pen);
uint8_t *capcom_z80_rotated_ptr(uint8_t *frame, int absx, int absy);
void capcom_z80_put_rotated(uint8_t *frame, int absx, int absy, uint8_t pen);
void capcom_z80_scale_rgb332(const uint8_t *src, const uint8_t *palette,
                             uint8_t *dst, int dst_stride, int dst_w, int dst_h);

#endif
