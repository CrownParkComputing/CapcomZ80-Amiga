/* c1943_menu.h -- 1943 Tiger-Heli-style DIP-switch overlay (F10). Renders into a
 * plain 8-bit chunky buffer using its own 16-colour palette (c1943_menu_pal,
 * loaded into palette indices 0..15 while the menu is up). Operates directly on
 * the two 1943 dip bytes DSWA (0xc003) and DSWB (0xc004). */
#ifndef C1943_MENU_H
#define C1943_MENU_H
#include <stdint.h>

#define C1943_MENU_ITEMS 5     /* Difficulty, Coinage, Continue, Demo Sounds, Flip */

extern const uint8_t c1943_menu_pal[16][3];

void c1943_menu_draw(uint8_t *buf, int w, int h, int tick, int sel,
                     uint8_t dswa, uint8_t dswb);
void c1943_loader_draw(uint8_t *buf, int w, int h, int tick, int ready, int blink);
void c1943_menu_change(int sel, int dir, uint8_t *dswa, uint8_t *dswb);

#endif
