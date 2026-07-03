#include "capcom_z80_video.h"

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
