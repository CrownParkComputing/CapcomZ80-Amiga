/* c1942_asprites.c -- 1942 AGA attached-pair hardware sprite multiplexer. */
#include "c1942_asprites.h"

#define SPR_WORDS   C1942_ASPR_WORDS
#define SPR_CTLW    (2 * SPR_WORDS)
#define SPR_LINEW   (2 * SPR_WORDS)
#define SPR1        (SPR_CTLW + 16 * SPR_LINEW)
#define SPRBLK      (C1942_ASPR_SLOTS * SPR1 + SPR_CTLW)
#define SPR_BASE_H  0x81
#define SPR_BASE_V  0x2c

static uint16_t *s_copper;
static uint16_t *s_spr;
static int s_relbase;
static int s_apair_cur[C1942_ASPR_PAIRS];
static int s_apair_lasty[C1942_ASPR_PAIRS];
static int s_aspr_n;
static int s_aspr_py[C1942_ASPR_PAIRS * C1942_ASPR_SLOTS];
static int s_aspr_pid[C1942_ASPR_PAIRS * C1942_ASPR_SLOTS];
static uint16_t s_aspr_pal[C1942_ASPR_PAIRS * C1942_ASPR_SLOTS][15];

static void write_aspr(int pair, int x, int y, const uint16_t *img4)
{
    int che = pair * 2, cho = pair * 2 + 1;
    uint16_t *be = s_spr + che * SPRBLK + s_apair_cur[pair];
    uint16_t *bo = s_spr + cho * SPRBLK + s_apair_cur[pair];
    int vstart = SPR_BASE_V + y, vstop = vstart + 16, hstart = SPR_BASE_H + x;
    uint16_t pos = (uint16_t)(((vstart & 0xff) << 8) | ((hstart >> 1) & 0xff));
    uint16_t ctl = (uint16_t)(((vstop & 0xff) << 8) | (((vstart >> 8) & 1) << 2)
                             | (((vstop >> 8) & 1) << 1) | (hstart & 1));
    be[0] = pos; bo[0] = pos;
    be[SPR_WORDS] = ctl;
    bo[SPR_WORDS] = (uint16_t)(ctl | 0x0080);     /* attach odd channel */
    for (int line = 0; line < 16; line++) {
        const uint16_t *src = img4 + line * 4 * SPR_WORDS;
        int d = SPR_CTLW + line * SPR_LINEW;
        for (int w = 0; w < SPR_WORDS; w++) {
            be[d + w]             = src[0 * SPR_WORDS + w];
            be[d + SPR_WORDS + w] = src[1 * SPR_WORDS + w];
            bo[d + w]             = src[2 * SPR_WORDS + w];
            bo[d + SPR_WORDS + w] = src[3 * SPR_WORDS + w];
        }
    }
    s_apair_cur[pair] += SPR1;
}

void c1942_asprites_init(uint16_t *copper, uint16_t *spr, int relbase_words)
{
    s_copper = copper;
    s_spr = spr;
    s_relbase = relbase_words;
    c1942_asprites_clear();
    c1942_asprites_finish();
}

void c1942_asprites_clear(void)
{
    for (int p = 0; p < C1942_ASPR_PAIRS; p++) {
        s_apair_cur[p] = 0;
        s_apair_lasty[p] = -1000000;
    }
    s_aspr_n = 0;
}

int c1942_asprites_add(int x, int y, const uint16_t *img4, int palid, const uint16_t *pal15)
{
    if (!s_spr || !s_copper) return 0;
    if (x < 0 || y < 0 || x + C1942_ASPR_W > 320 || y + 16 > 256) return 0;
    if (s_aspr_n >= C1942_ASPR_PAIRS * C1942_ASPR_SLOTS) return 0;
    if (SPR_BASE_V + y - 2 > 255) return 0;
    for (int k = 0; k < s_aspr_n; k++) {
        if (s_aspr_pid[k] == palid) continue;
        if (y < s_aspr_py[k] + 16 && s_aspr_py[k] < y + 16) return 0;
    }
    int pair = -1;
    for (int p = 0; p < C1942_ASPR_PAIRS; p++) {
        if (y < s_apair_lasty[p]) continue;
        if (s_apair_cur[p] + SPR1 > SPRBLK - SPR_CTLW) continue;
        pair = p;
        break;
    }
    if (pair < 0) return 0;
    write_aspr(pair, x, y, img4);
    s_apair_lasty[pair] = y + 17;
    s_aspr_py[s_aspr_n] = y;
    s_aspr_pid[s_aspr_n] = palid;
    for (int i = 0; i < 15; i++) s_aspr_pal[s_aspr_n][i] = pal15[i];
    s_aspr_n++;
    return 1;
}

void c1942_asprites_finish(void)
{
    if (!s_spr || !s_copper) return;
    for (int p = 0; p < C1942_ASPR_PAIRS; p++) {
        uint16_t *be = s_spr + (p * 2) * SPRBLK + s_apair_cur[p];
        uint16_t *bo = s_spr + (p * 2 + 1) * SPRBLK + s_apair_cur[p];
        be[0] = 0; be[SPR_WORDS] = 0;
        bo[0] = 0; bo[SPR_WORDS] = 0;
    }
    int w = s_relbase;
    int lim = s_relbase + C1942_ASPR_MAXREL * C1942_ASPR_RELWORDS;
    int lastpid = 0x7fffffff;
    for (int k = 0; k < s_aspr_n; k++) {
        if (s_aspr_pid[k] == lastpid) continue;
        lastpid = s_aspr_pid[k];
        int vp = SPR_BASE_V + s_aspr_py[k] - 2;
        if (vp < 0) vp = 0;
        if (vp > 255) continue;
        if (w + C1942_ASPR_RELWORDS > lim) break;
        s_copper[w++] = (uint16_t)((vp << 8) | 0x0001);
        s_copper[w++] = 0xff00;
        for (int i = 0; i < 15; i++) {
            s_copper[w++] = (uint16_t)(0x0180 + (17 + i) * 2);
            s_copper[w++] = s_aspr_pal[k][i];
        }
    }
    s_copper[w++] = 0xffff;
    s_copper[w++] = 0xfffe;
}

int c1942_asprites_count(void)
{
    return s_aspr_n;
}
