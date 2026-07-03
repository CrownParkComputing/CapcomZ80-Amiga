/* demo_core.h -- WhittyDemo: pure-C old-school demo intro renderer.
 *
 * OS-free chunky renderer in the family shared-core style: the same code runs
 * on the Linux host harness (PPM validation) and inside the Amiga exe. The
 * host binds a w*h 8-bit chunky framebuffer; demo_frame() is a pure function
 * of the tick so host and Amiga render byte-identical frames.
 *
 * Effects (demos gone by): copper-gradient backdrop + bouncing rainbow raster
 * bars, parallax starfield, a picture painted piece by piece inside a rainbow
 * frame, a wavy rainbow title and a big rainbow sine scroller. Standalone mode
 * can also render the filled-vector ring; game/selector modes keep the loader
 * cleaner and leave the centrepiece unobscured.
 */
#ifndef DEMO_CORE_H
#define DEMO_CORE_H

/* 256-entry RGB palette the presenter must upload (LoadRGB32 / PPM). */
extern unsigned char demo_pal[256][3];

/* One-time init: builds palette, gradient, reveal permutation. */
void demo_init(int w, int h);

/* Scroller text variant: 0 = standalone demo (default), 1 = game boot intro,
 * 2 = Commando / GunSmoke selector. */
void demo_set_text_variant(int v);

/* Selector highlight: 0 = Commando, 1 = GunSmoke. Only that game's square panel
 * is unveiled. Ignored outside variant 2. */
void demo_set_selector_selection(int sel);

/* Render one frame into fb (w*h pens). Pure function of tick. */
void demo_frame(unsigned char *fb, int tick);

#endif
