/* gaplus_menu.h -- shared chunky-buffer renderer for the Tiger-Heli-style boot
 * loader + DIP-switch screen, used by both the RTG (864x486) and AGA (320x288)
 * presenters. All drawing targets a plain 8-bit chunky buffer (buf,w,h) using the
 * 16-colour menu palette below, so each presenter only supplies its own C2P /
 * palette-upload / input glue. Layout adapts to the buffer width (big vs small).
 *
 * The DIP routines work in MAME DIP value-space m[4] (m = ~game-read nibbles, see
 * gaplus_machine.c): m[0]=DSWB_HIGH, m[1]=DSWA_HIGH, m[2]=DSWB_LOW, m[3]=DSWA_LOW.
 */
#ifndef GAPLUS_MENU_H
#define GAPLUS_MENU_H
#include <stdint.h>

#define GM_DIP_ITEMS 7

/* 16-colour menu palette (8-bit guns) shared by loader + DIP screen */
extern const uint8_t gm_palette[16][3];

/* low-level primitives (exposed so presenters can add their own decorations) */
void gm_put_px(uint8_t *buf,int w,int h,int x,int y,uint8_t c);
void gm_fill_rect(uint8_t *buf,int w,int h,int x,int y,int rw,int rh,uint8_t c);
void gm_draw_text(uint8_t *buf,int w,int h,const char *s,int x,int y,int scale,uint8_t c);
void gm_draw_text_center(uint8_t *buf,int w,int h,const char *s,int y,int scale,uint8_t c);

/* whole-screen renders (clear + draw) */
void gm_draw_loader(uint8_t *buf,int w,int h,int tick,int ready,int blink);
void gm_draw_dip(uint8_t *buf,int w,int h,int tick,int sel,const uint8_t m[4]);

/* DIP option model (MAME value-space) */
const char *gm_dip_value_text(int sel,const uint8_t m[4]);
void gm_dip_change(int sel,int dir,uint8_t m[4]);

#endif
