/* src/hal/cgunsmoke_loader.c -- classic Amiga LOADER / INTRO for Gun.Smoke.
 *
 * Gun-Smoke-namespaced sibling of src/hal/pl_loader.c (Pac-Land) and
 * src/hal/tc_loader.c (Terra Cresta). Shows the GUN.SMOKE title IMAGE on its own
 * AGA bitplane display and plays a ProTracker .mod ("sanxion") on Paula (via Frank
 * Wille's PD ptplayer) WHILE the game boots in the background. The driver
 * (cgunsmoke_hwmain.c) advances the Z80 each frame with NO game render until the
 * picture has fully drawn in, then ARMS a flashing "PRESS FIRE TO CONTINUE"
 * prompt; when the player presses fire/start it calls gunsmoke_loader_close() and
 * hands the chipset to the real Gun.Smoke renderer + audio. The user never sees
 * the boot / credits card; only the intro, then fire -> game.
 *
 * Display: a minimal single-playfield copperlist (bitplane pointers + end) built
 * from the embedded image blob (src/hal/gunsmoke_loaderimg.s, made by
 * tools/make_gs_loader_img.py). The blob's pen 0 is BLACK, so the slow cube reveal
 * draws the picture in on a black canvas. The LAST palette pen (ncol-1) holds the
 * prompt text: kept == background (invisible) until armed, then flashed.
 *
 * Audio: ptplayer's CIA-B / level-6 (EXTER) timer interrupt drives replay. We
 * reuse the same proven objects the Terra Cresta / Pac-Land builds link
 * (src/hal/tc_ptplayer.68k + tc_ptplayer_glue.s) -- read-only. On handoff the mod
 * is stopped and the CIA-B interrupt removed BEFORE the game's YM2203 Paula audio
 * starts, so they never fight over Paula / the level-6 vector.
 */
#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <stdint.h>

/* embedded blobs (Gun-Smoke-namespaced) */
extern const unsigned char gunsmoke_loaderimg[];           /* image: header+pal+planes */
extern const unsigned char gunsmoke_loader_mod[];          /* the .mod tune            */
extern const unsigned char gunsmoke_loader_mod_end[];

/* ptplayer C-callable glue (src/hal/tc_ptplayer_glue.s -> tc_ptplayer.68k), reused */
extern void tc_pt_install(void *vbr, long palflag);
extern void tc_pt_init(void *mod, void *samples, long pos);
extern void tc_pt_start(void);
extern void tc_pt_end(void);
extern void tc_pt_remove(void);
extern void tc_pt_mastervol(long vol);
extern void *gunsmoke_get_vbr(void) asm("gunsmoke_get_vbr");

/* graphics.library base (shared global, also used by hwscroll.c) */
extern struct GfxBase *GfxBase;

#define CUSTOM    ((volatile uint16_t *)0xdff000)
#define R_DMACON  (0x096/2)
#define R_INTENA  (0x09a/2)
#define R_INTREQ  (0x09c/2)
#define R_DIWSTRT (0x08e/2)
#define R_DIWSTOP (0x090/2)
#define R_DDFSTRT (0x092/2)
#define R_DDFSTOP (0x094/2)
#define R_BPLCON0 (0x100/2)
#define R_BPLCON1 (0x102/2)
#define R_BPLCON2 (0x104/2)
#define R_BPLCON3 (0x106/2)
#define R_BPL1MOD (0x108/2)
#define R_BPL2MOD (0x10a/2)
#define R_COLOR00 (0x180/2)
#define R_COP1LCH (0x080/2)
#define R_VPOSR   (0x004/2)

static uint16_t rd16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* allocated chip resources (freed in gunsmoke_loader_close) */
static void  *img_chunk = 0;   static long img_size = 0;   /* planes + copperlist */
static void  *mod_chunk = 0;   static long mod_size = 0;   /* the module (chip) */
static uint16_t *lcopper = 0;
static int    loader_ok = 0;

/* prompt-flash state: the prompt text lives in pen (nc-1). It is invisible
 * (colour == background) until gunsmoke_loader_arm() is called, then it flashes. */
static int      prompt_pen   = 0;
static uint16_t prompt_on    = 0x0FF0;
static uint16_t prompt_off   = 0x0000;
static int      prompt_armed = 0;
static int      prompt_phase = 0;
static int      prompt_tick  = 0;
static int      prompt_want  = 0;   /* arm requested; honoured once the reveal completes */

static void wait_safe_line(void)
{
    volatile uint16_t *c = CUSTOM;
    long g = 0;
    while (g++ < 200000) { if ((c[R_VPOSR] & 0x1ff) > 0x80) break; }
}

static void wait_vbl(void)
{
    volatile uint32_t *vp = (volatile uint32_t *)0xdff004;
    unsigned long g = 0;
    for (;;) { uint32_t r = *vp; uint32_t v = (((r >> 16) & 1) << 8) | ((r >> 8) & 0xff);
               if (v < 300) break; if (++g > 600000UL) break; }
    g = 0;
    for (;;) { uint32_t r = *vp; uint32_t v = (((r >> 16) & 1) << 8) | ((r >> 8) & 0xff);
               if (v >= 300) break; if (++g > 600000UL) break; }
}

static void set_color(int idx, uint16_t rgb)
{
    volatile uint16_t *c = CUSTOM;
    c[R_BPLCON3] = (uint16_t)((idx >> 5) << 13);
    c[R_COLOR00 + (idx & 31)] = rgb;
    c[R_BPLCON3] = 0;
}

static void load_palette(const unsigned char *pal, int ncol)
{
    for (int i = 0; i < ncol && i < 256; i++)
        set_color(i, rd16(pal + i * 2));          /* 12-bit 0x0RGB */
}

/* progressive C64-style "cube" image reveal -- one small BLOCK at a time, in reading
 * order (left->right, then row by row). SMOOTHER + QUICKER than the old 16x8/13s:
 * 8x8 cubes over a 320x256 picture = 40x32 = 1280 cubes; 4 cubes EVERY vblank
 * (RV_DIV=1) -> 1280/4 = 320 vblanks ~= 6.4s. Smaller blocks make the frontier
 * advance finely (smooth, not chunky) and stepping every vblank keeps it fluid.
 * Tunables: RV_BLOCK_W must be a multiple of 8 (planar bytes are 8px wide).
 *   RV_BLOCK_W/H       cube size in px (8x8). Smaller = smoother, finer frontier.
 *   RV_CUBES_PER_STEP  cubes plotted per reveal step.
 *   RV_DIV             vblanks between steps (1 = every vblank = smoothest/fastest).
 * For a slower draw raise RV_DIV or lower RV_CUBES_PER_STEP; for quicker do the
 * opposite. Total draw time ~= (rv_nblk / RV_CUBES_PER_STEP) * RV_DIV / 50 s. */
#define RV_BLOCK_W         8
#define RV_BLOCK_H         8
#define RV_CUBES_PER_STEP  4
#define RV_DIV             1
static const unsigned char *rv_src; static uint8_t *rv_dst;
static long rv_planesz, rv_stride; static int rv_np, rv_w, rv_h;
static int rv_nbx, rv_nby, rv_nblk, rv_blk, rv_tick;

void gunsmoke_loader_open(void)
{
    volatile uint16_t *c = CUSTOM;
    const unsigned char *img = gunsmoke_loaderimg;
    int w  = rd16(img + 0), h = rd16(img + 2);
    int np = rd16(img + 4), nc = rd16(img + 6);
    const unsigned char *pal    = img + 8;
    const unsigned char *planar = pal + nc * 2;
    long stride   = w / 8;
    long plane_sz = stride * h;

    prompt_pen = nc - 1;
    prompt_on  = rd16(pal + prompt_pen * 2);      /* bright prompt colour from blob   */
    prompt_off = rd16(pal + 0);                   /* background pen 0 (invisible text) */
    prompt_armed = 0; prompt_phase = 0; prompt_tick = 0; prompt_want = 0;

    /* take the display from the OS (kills the Workbench view + OS sprites). */
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 0);
    if (GfxBase) { LoadView(0); WaitTOF(); WaitTOF(); }

    /* read the VBR for ptplayer's level-6 vector (privileged -> supervisor). */
    APTR oldstk = SuperState();
    void *vbr = gunsmoke_get_vbr();
    UserState(oldstk);

    /* chip block: np bitplanes (image pixels, CLEARED) + the copperlist. */
    long copwords = np * 4 + 2;                    /* 2 ptr-moves/plane + end */
    img_size = np * plane_sz + copwords * 2;
    img_chunk = AllocMem((unsigned long)img_size, MEMF_CHIP | MEMF_CLEAR);
    if (!img_chunk) return;
    uint8_t *bpls = (uint8_t *)img_chunk;
    rv_src = planar; rv_dst = bpls; rv_planesz = plane_sz; rv_stride = stride;
    rv_np = np; rv_w = w; rv_h = h;
    rv_nbx = (w + RV_BLOCK_W - 1) / RV_BLOCK_W;
    rv_nby = (h + RV_BLOCK_H - 1) / RV_BLOCK_H;
    rv_nblk = rv_nbx * rv_nby; rv_blk = 0; rv_tick = 0;
    lcopper = (uint16_t *)(bpls + np * plane_sz);

    int wi = 0;
    for (int p = 0; p < np; p++) {
        uint32_t a = (uint32_t)(bpls + (long)p * plane_sz);
        lcopper[wi++] = (uint16_t)(0x00E0 + p * 4); lcopper[wi++] = (uint16_t)(a >> 16);
        lcopper[wi++] = (uint16_t)(0x00E2 + p * 4); lcopper[wi++] = (uint16_t)(a & 0xffff);
    }
    lcopper[wi++] = 0xFFFF; lcopper[wi++] = 0xFFFE;

    /* copy the module into CHIP RAM (Paula DMA + ptplayer require it). */
    mod_size = (long)(gunsmoke_loader_mod_end - gunsmoke_loader_mod);
    mod_chunk = AllocMem((unsigned long)mod_size, MEMF_CHIP);
    if (mod_chunk) {
        uint8_t *d = (uint8_t *)mod_chunk; const uint8_t *s = gunsmoke_loader_mod;
        for (long i = 0; i < mod_size; i++) d[i] = s[i];
    }

    /* program the display (full 320x256 lores, single playfield). */
    wait_safe_line();
    c[R_DMACON]  = 0x7FFF;
    c[R_DIWSTRT] = 0x2C81;
    c[R_DIWSTOP] = 0x2CC1;
    c[R_DDFSTRT] = 0x0038;
    c[R_DDFSTOP] = 0x00D0;
    c[R_BPL1MOD] = 0; c[R_BPL2MOD] = 0;
    c[R_BPLCON0] = (np >= 8) ? 0x0211 : (uint16_t)(0x0201 | (np << 12));
    c[R_BPLCON1] = 0; c[R_BPLCON2] = 0; c[R_BPLCON3] = 0;
    load_palette(pal, nc);
    set_color(prompt_pen, prompt_off);              /* prompt hidden until armed */
    { uint32_t a = (uint32_t)lcopper; c[R_COP1LCH] = (uint16_t)(a >> 16); c[R_COP1LCH + 1] = (uint16_t)(a & 0xffff); }
    c[R_DMACON]  = 0x8380;                          /* SET | DMAEN | BPLEN | COPEN */

    /* Mask all OS interrupts, then (LOADER_MUSIC) start the .mod replay. */
    c[R_INTENA] = 0x7FFF; c[R_INTREQ] = 0x7FFF;
#ifdef LOADER_MUSIC
    if (mod_chunk) {
        tc_pt_install(vbr, 1);                      /* PAL CIA-B timer, level-6 EXTER */
        tc_pt_init(mod_chunk, 0, 0);                /* samples follow patterns (a1=NULL) */
        tc_pt_mastervol(64);
        tc_pt_start();                              /* mt_Enable=1 -> play the music */
        c[R_INTREQ] = 0x2000;                       /* clear stale EXTER */
        c[R_INTENA] = 0xE000;                       /* SET | INTEN | EXTER */
    }
#else
    (void)vbr;                                      /* silent loader: no replay, no static */
#endif
    Forbid();                                       /* no task switching during takeover */
    (void)w; (void)h;
    loader_ok = 1;
}

/* arm the flashing "PRESS FIRE TO CONTINUE" prompt -- the driver calls this every
 * frame; the actual arm is deferred until the picture has fully drawn in. */
void gunsmoke_loader_arm(void)
{
    if (!loader_ok) return;
    prompt_want = 1;
}

int gunsmoke_loader_is_armed(void) { return prompt_armed; }

/* keep the loader display + music alive for one frame (CPU advances in the driver). */
void gunsmoke_loader_frame(void)
{
    volatile uint16_t *c = CUSTOM;
    if (!loader_ok) return;
    wait_vbl();
    { uint32_t a = (uint32_t)lcopper; c[R_COP1LCH] = (uint16_t)(a >> 16); c[R_COP1LCH + 1] = (uint16_t)(a & 0xffff); }
    c[R_DMACON] = 0x8380;                           /* keep bitplane+copper DMA asserted */

    /* SMOOTH cube draw-in: plot RV_CUBES_PER_STEP RV_BLOCK_WxRV_BLOCK_H (8x8) blocks
     * every RV_DIV (1) vblanks, in reading order (~6.4s). The prompt is never snapped
     * over a half-drawn image -- it is held off (and fire ignored, since is_armed()
     * stays false) until the reveal completes. */
    if (rv_blk < rv_nblk) {
        if (++rv_tick >= RV_DIV) {
            rv_tick = 0;
            for (int n = 0; n < RV_CUBES_PER_STEP && rv_blk < rv_nblk; n++, rv_blk++) {
                int bx = rv_blk % rv_nbx, by = rv_blk / rv_nbx;
                int x0  = bx * RV_BLOCK_W, y0 = by * RV_BLOCK_H;
                int cb0 = x0 >> 3;
                int cb1 = (x0 + RV_BLOCK_W + 7) >> 3;
                int y1  = y0 + RV_BLOCK_H;
                if (cb1 > rv_stride) cb1 = (int)rv_stride;
                if (y1  > rv_h)      y1  = rv_h;
                for (int p = 0; p < rv_np; p++) {
                    uint8_t *b = rv_dst + (long)p * rv_planesz;
                    const unsigned char *s = rv_src + (long)p * rv_planesz;
                    for (int y = y0; y < y1; y++) {
                        long ro = (long)y * rv_stride;
                        for (int cx = cb0; cx < cb1; cx++) b[ro + cx] = s[ro + cx];
                    }
                }
            }
        }
        return;                                  /* picture not finished -> no prompt yet */
    }

    /* picture fully revealed: arm the prompt now (if asked) and flash it ~every 25 vbl. */
    if (prompt_want && !prompt_armed) {
        prompt_armed = 1; prompt_phase = 1; prompt_tick = 0;
        set_color(prompt_pen, prompt_on);
    }
    if (prompt_armed && ++prompt_tick >= 25) {
        prompt_tick = 0;
        prompt_phase ^= 1;
        set_color(prompt_pen, prompt_phase ? prompt_on : prompt_off);
    }
}

/* ---- loader "PRESS FIRE TO CONTINUE": fire/start detection -----------------
 * OS-takeover register banging, mirroring cgunsmoke_input.c. Returns 1 if any
 * fire (joystick button1/2/3) / direction / START('1') / SPACE / L-CTRL / CD32 Play is down.
 * Does NOT feed the game -- it only gates when the loader hands off. */
extern unsigned gunsmoke_read_cd32_port1(void);
#define CD32_PLAY 0x02
#define JOY1DAT  (*(volatile unsigned short *)0xdff00c)
#define CIAA_PRA (*(volatile unsigned char  *)0xbfe001)
#define POTGO    (*(volatile unsigned short *)0xdff034)
#define POTINP   (*(volatile unsigned short *)0xdff016)
#define CIAA_SDR (*(volatile unsigned char  *)0xbfec01)
#define CIAA_ICR (*(volatile unsigned char  *)0xbfed01)
#define CIAA_CRA (*(volatile unsigned char  *)0xbfee01)
#define LK_1     0x01
#define LK_SPACE 0x40
#define LK_LCTRL 0x63

static unsigned char lk_down[128];

static void loader_poll_keyboard(void)
{
    int guard = 0;
    while ((CIAA_ICR & 0x08) && ++guard < 16) {
        unsigned char raw = CIAA_SDR;
        CIAA_CRA |= 0x40;
        for (volatile int i = 0; i < 350; i++) { }
        CIAA_CRA &= ~0x40;
        unsigned char code = (unsigned char)~((raw >> 1) | (raw << 7));
        if (code & 0x80) lk_down[code & 0x7f] = 0;             /* key up   */
        else             lk_down[code & 0x7f] = 1;             /* key down */
    }
}

/* Return 1 if the player wants to start (any fire / direction / start key). Only
 * meaningful once the prompt is armed (driver gates on gunsmoke_loader_is_armed()). */
int gunsmoke_loader_fire(void)
{
    static int potinit = 0;
    if (!potinit) { POTGO = 0xFF00; potinit = 1; }   /* same as the game input read */
    loader_poll_keyboard();
    if (lk_down[LK_1] || lk_down[LK_SPACE] || lk_down[LK_LCTRL]) return 1;
    if (gunsmoke_read_cd32_port1() & CD32_PLAY) return 1;
    unsigned v = JOY1DAT;
    int up    = ((v >> 9) ^ (v >> 8)) & 1;
    int down  = ((v >> 1) ^ v) & 1;
    int left  = (v >> 9) & 1;
    int right = (v >> 1) & 1;
    unsigned short pot = POTINP;
    int fire = !(CIAA_PRA & 0x80) || !(pot & 0x4000) || !(pot & 0x1000);
    if (fire || up || down || left || right) return 1;
    return 0;
}

/* stop the music + release the loader display so the game can take the chipset.
 * Caller then opens the game video (gunsmoke_hw_splash/open) + game audio. */
void gunsmoke_loader_close(void)
{
    volatile uint16_t *c = CUSTOM;
    if (!loader_ok) return;
#ifdef LOADER_MUSIC
    if (mod_chunk) {
        tc_pt_end();                                /* stop song + sfx */
        tc_pt_remove();                             /* remove CIA-B int, restore timers */
    }
#endif
    c[R_INTENA] = 0x7FFF; c[R_INTREQ] = 0x7FFF;     /* drop EXTER (game re-takes ints) */
    c[R_DMACON] = 0x000F;                           /* silence all 4 audio channels */
    wait_safe_line();
    c[R_DMACON] = 0x0180;                           /* CLR BPLEN|COPEN (stop fetching loader copper) */
    if (img_chunk) { FreeMem(img_chunk, (unsigned long)img_size); img_chunk = 0; lcopper = 0; }
    if (mod_chunk) { FreeMem(mod_chunk, (unsigned long)mod_size); mod_chunk = 0; }
    loader_ok = 0;
}
