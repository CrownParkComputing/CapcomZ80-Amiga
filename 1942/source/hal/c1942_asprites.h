#ifndef C1942_ASPRITES_H
#define C1942_ASPRITES_H

#include <stdint.h>

#define C1942_ASPR_W       64
#define C1942_ASPR_WORDS   (C1942_ASPR_W / 16)
#define C1942_ASPR_IMGWORDS (16 * 4 * C1942_ASPR_WORDS)

#define C1942_ASPR_NSPR    8
#define C1942_ASPR_PAIRS   4
#define C1942_ASPR_SLOTS   12
#define C1942_ASPR_MAXREL  24
#define C1942_ASPR_RELWORDS 32

void c1942_asprites_init(uint16_t *copper, uint16_t *spr, int relbase_words);
void c1942_asprites_clear(void);
int  c1942_asprites_add(int x, int y, const uint16_t *img4, int palid, const uint16_t *pal15);
void c1942_asprites_finish(void);
int  c1942_asprites_count(void);

#endif
