/* arcade_intro.h -- standalone, game-agnostic RTG/chunky "loader" intro.
 *
 * Extracted from the Tiger-Heli RTG loader and generalised so it can be tagged
 * onto ANY Amiga RTG arcade port. The host game:
 *   1. opens its Picasso96 8-bit custom screen (Screen* + Window* + a W*H chunky
 *      framebuffer),
 *   2. fills an ai_config (title, scroller, control hints, ProTracker .mod, an
 *      optional bezel, and two callbacks: ready() = "ROMs decoded?" and warmup()
 *      = "run one game frame behind the loader"),
 *   3. calls ai_run(): shows the animated loader + plays the .mod while warming
 *      the game up + decoding behind it, flips "DECODING ROMS" -> "READY" only
 *      once ready() is true, and waits for FIRE/START before returning 1.
 *
 * The host links the shared ptplayer (tc_ptplayer.68k + tc_ptplayer_glue.s) and
 * arcade_intro_glue.s (supervisor VBR fetch). See README.md.
 */
#ifndef ARCADE_INTRO_H
#define ARCADE_INTRO_H

struct Screen;
struct Window;

#define AI_CD32_ID        0x01
#define AI_CD32_PLAY      0x02
#define AI_CD32_LSHOULDER 0x04
#define AI_CD32_RSHOULDER 0x08
#define AI_CD32_GREEN     0x10
#define AI_CD32_YELLOW    0x20
#define AI_CD32_RED       0x40
#define AI_CD32_BLUE      0x80

/* ---- optional DIP-switch editor (F10 in the loader) ---- */
typedef struct { unsigned char val; const char *label; } ai_dip_opt;
typedef struct {
    const char *name;                  /* setting name, e.g. "LIVES"            */
    int   which;                       /* 0 = dsw1, 1 = dsw2                     */
    unsigned char mask;                /* bits this setting occupies            */
    int   nopts;
    const ai_dip_opt *opts;            /* value(masked)->label choices          */
} ai_dip_item;
typedef struct {
    const ai_dip_item *items;
    int   nitems;
    unsigned char *dsw1, *dsw2;        /* live DIP bytes (editor reads/writes)  */
    void (*apply)(void *ctx);          /* called after a change (e.g. re-init)  */
    void *ctx;
} ai_dip_config;

typedef struct {
    const char *title;                 /* big wavy title, e.g. "SKY KID"        */
    const char *scroller;              /* marquee text (uppercase/0-9/-/. only) */
    const char *const *key_lines;      /* NULL-terminated keyboard control hints */
    const char *const *pad_lines;      /* NULL-terminated CD32 pad control hints  */
    const unsigned char *mod;          /* ProTracker module start (or 0 = silent) */
    const unsigned char *mod_end;      /* module end                              */
    const unsigned char (*palette)[3]; /* 16-colour loader palette, or 0 = default */
    int   min_ticks;                   /* min loader frames before FIRE accepted (~150) */
    int  (*ready)(void *ctx);          /* return 1 once the game's ROMs are decoded */
    void (*warmup)(void *ctx);         /* run one arcade frame behind the loader (or 0) */
    void *ctx;                         /* opaque, passed to ready()/warmup()      */
    const ai_dip_config *dip;          /* optional DIP editor (F10), or 0         */
} ai_config;

/* Bind the host's RTG surface. fb is a w*h 8-bit chunky buffer; scr/win are the
 * Picasso96 custom screen + its window (for WriteChunkyPixels, LoadRGB32 and
 * raw-key polling). Call once after the screen is open. */
void ai_init(struct Screen *scr, struct Window *win, unsigned char *fb, int w, int h);

/* Run the whole intro. Returns 1 to proceed into the game, 0 if the user asked
 * to quit at the loader. After it returns, the host owns the screen/palette
 * again (re-upload your game palette). */
int  ai_run(const ai_config *cfg);

/* Open the DIP editor on demand (e.g. host F10 during the game). Returns 1 if a
 * setting changed (apply() is called for you). Restore your own palette after. */
int  ai_dip_open(const ai_dip_config *dip);

/* Shared CD32 pad helpers for ports using joystick port 1. The reader includes
 * the non-CD32/floating-port ghost rejection used by the Gaplus-derived ports. */
unsigned ai_read_cd32_port1(void);
int ai_cd32_dip_combo(unsigned cd32);       /* L + R + Play */
int ai_cd32_exit_combo(unsigned cd32);      /* all face buttons */

/* Helper: lay a w*h chunky bezel backdrop into fb (plain memcpy). Handy for the
 * host's per-frame game present so the play area sits inside a cabinet bezel. */
void ai_bezel_blit(unsigned char *fb, const unsigned char *bezel, int w, int h);
void ai_bezel_draw_simple(unsigned char *fb, int w, int h,
                          int game_w, int game_h,
                          const unsigned char *grad_pens, int ngrad,
                          unsigned char frame_pen);
void ai_blit_1x_at(unsigned char *fb, int w, int h,
                   const unsigned char *src, int sw, int sh, int x, int y);
void ai_blit_2x_at(unsigned char *fb, int w, int h,
                   const unsigned char *src, int sw, int sh, int x, int y);
void ai_blit_1x_center(unsigned char *fb, int w, int h,
                       const unsigned char *src, int sw, int sh);

/* AGA/chipset hosts: override the loader's present (WriteChunkyPixels) with a host
 * C2P that copies the framebuffer into the screen's bitplanes. Pass 0 to restore
 * the default. RTG/Picasso hosts never call this. */
void ai_set_present(void (*cb)(void));

/* Optional: force the fixed progress-bar loader path used by validation builds. */
void ai_set_loader_enabled(int e);

/* Optional per-game loader display guard for hosts that crop RTG edges. Keeps
 * compact-loader text away from the left/right edge; force_compact is useful
 * when a nominally larger RTG mode is still displayed through a cropped viewport. */
void ai_set_safe_margins(int x, int force_compact);

#endif
