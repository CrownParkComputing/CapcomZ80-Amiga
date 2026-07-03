/* demo_main.c -- WhittyDemo Amiga host: opens the family-standard 864x486
 * 8-bit RTG screen, uploads the 256-colour demo palette, plays the house
 * ProTracker mod via the shared ptplayer, and presents demo_core frames with
 * WriteChunkyPixels at WaitTOF pace. ESC / fire / mouse button exits. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/displayinfo.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <string.h>
#include "demo_core.h"

#define SCR_W 864
#define SCR_H 486

#define CUSTOM_REGS ((volatile UWORD *)0xdff000)
#define R_DMACON  (0x096/2)
#define R_INTENA  (0x09a/2)
#define R_INTREQ  (0x09c/2)
#define R_JOY1DAT (0x00c/2)
#define CIAA_PRA  (*(volatile unsigned char *)0xbfe001)
#define P1_FIRE   0x80
#define KEY_1     0x01
#define KEY_2     0x02
#define KEY_SPACE 0x40
#define KEY_RET   0x44
#define KEY_LEFT  0x4f
#define KEY_RIGHT 0x4e
#define KEY_ESC   0x45
#define KEY_UP    0x4c
#define KEY_DOWN  0x4d

/* shared ptplayer + supervisor VBR fetch (ArcadeIntro glue) */
extern void tc_pt_install(void *vbr, long palflag);
extern void tc_pt_init(void *mod, void *samples, long pos);
extern void tc_pt_start(void);
extern void tc_pt_end(void);
extern void tc_pt_remove(void);
extern void tc_pt_mastervol(long vol);
extern void *ai_get_vbr(void) asm("ai_get_vbr");
extern unsigned char ai_default_mod[];
extern unsigned char ai_default_mod_end[];

static void *mod_chunk;
static ULONG mod_size;
static int music_on;

static void music_start(void)
{
    volatile UWORD *c = CUSTOM_REGS;
    mod_size = (ULONG)(ai_default_mod_end - ai_default_mod);
    mod_chunk = AllocMem(mod_size, MEMF_CHIP);
    if (!mod_chunk) return;
    memcpy(mod_chunk, ai_default_mod, mod_size);
    {
        APTR oldstk = SuperState();
        void *vbr = ai_get_vbr();
        UserState(oldstk);
        tc_pt_install(vbr, 1);
        tc_pt_init(mod_chunk, 0, 0);
        tc_pt_mastervol(64);
        tc_pt_start();
        c[R_INTREQ] = 0x2000;
        c[R_INTENA] = 0xe000;
    }
    music_on = 1;
}

static void music_stop(void)
{
    if (music_on) {
        tc_pt_end();
        tc_pt_remove();
        CUSTOM_REGS[R_DMACON] = 0x000f;
    }
    music_on = 0;
    if (mod_chunk) {
        FreeMem(mod_chunk, mod_size);
        mod_chunk = 0;
    }
}

static void write_selector_choice(int rc)
{
    const char *path = 0;
    char marker = '1';
    BPTR fh;

    if (rc == 0)
        path = "ENV:WHITTY_CG_COMMANDO";
    else if (rc == 5)
        path = "ENV:WHITTY_CG_GUNSMOKE";
    else if (rc >= 10)
        path = "ENV:WHITTY_CG_QUIT";

    if (!path)
        return;

    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (fh) {
        Write(fh, &marker, 1);
        Close(fh);
    }
}

/* Non-selector: -1 keep running, 0 proceed, 5 quit.
 * Selector:     -1 keep running, 0 Commando, 5 GunSmoke, 10 quit. */
static int poll_input(struct Window *win, int selector, int *selection)
{
    struct IntuiMessage *msg;
    int rc = -1;
    UWORD joy;
    static int fire_was_down = 1;
    static int joy_left_was_down = 0;
    static int joy_right_was_down = 0;
    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cls = msg->Class;
        UWORD code = msg->Code;
        WORD mouse_x = msg->MouseX;
        ReplyMsg((struct Message *)msg);
        if (cls == IDCMP_RAWKEY && !(code & 0x80)) {
            UWORD key = code & 0x7f;
            if (key == KEY_ESC) rc = selector ? 10 : 5;
            else if (selector && key == KEY_1) {
                *selection = 0;
                demo_set_selector_selection(*selection);
                rc = 0;
            } else if (selector && key == KEY_2) {
                *selection = 1;
                demo_set_selector_selection(*selection);
                rc = 5;
            }
            else if (selector && key == KEY_LEFT) {
                *selection = 0;
                demo_set_selector_selection(*selection);
            } else if (selector && key == KEY_RIGHT) {
                *selection = 1;
                demo_set_selector_selection(*selection);
            } else if (selector && (key == KEY_RET || key == KEY_SPACE)) {
                rc = *selection ? 5 : 0;
            }
        }
        if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN && rc < 0) {
            if (selector) {
                *selection = mouse_x >= (SCR_W / 2) ? 1 : 0;
                demo_set_selector_selection(*selection);
                rc = *selection ? 5 : 0;
            } else {
                rc = 0;
            }
        }
        if (cls == IDCMP_CLOSEWINDOW) rc = selector ? 10 : 5;
    }
    if (selector) {
        int fire_down = !(CIAA_PRA & P1_FIRE);
        int joy_left, joy_right;
        joy = CUSTOM_REGS[R_JOY1DAT];
        joy_left  = !!(joy & 0x0200);      /* JOY1DAT bit9 = left  */
        joy_right = !!(joy & 0x0002);      /* JOY1DAT bit1 = right */
        if (joy_left && !joy_left_was_down) {
            *selection = 0;
            demo_set_selector_selection(*selection);
        }
        if (joy_right && !joy_right_was_down) {
            *selection = 1;
            demo_set_selector_selection(*selection);
        }
        if (fire_down && !fire_was_down && rc < 0)
            rc = *selection ? 5 : 0;
        fire_was_down = fire_down;
        joy_left_was_down = joy_left;
        joy_right_was_down = joy_right;
    } else if (!(CIAA_PRA & P1_FIRE) && rc < 0) {
        rc = 0;                                      /* joystick fire */
    }
    return rc;
}

int main(int argc, char **argv)
{
    struct Screen *scr = 0;
    struct Window *win = 0;
    unsigned char *fb = 0;
    static ULONG loadrgb[1 + 256 * 3 + 1];
    ULONG modeid;
    int tick = 0, i, quit = 0;
    int selector = 0, selection = 0;

    /* "WhittyDemo GAME" = boot intro in front of a game: scroller says PRESS
     * FIRE TO START; exit rc tells the startup-sequence what the user chose
     * (fire/mouse -> rc 0 = run the game, ESC -> rc 5/WARN = quit). */
    if (argc > 1 && argv[1] && argv[1][0] == 'G')
        demo_set_text_variant(1);
    if (argc > 1 && argv[1] && argv[1][0] == 'S') {
        selector = 1;
        demo_set_text_variant(2);
        demo_set_selector_selection(selection);
    }

    modeid = BestModeID(BIDTAG_NominalWidth, SCR_W,
                        BIDTAG_NominalHeight, SCR_H,
                        BIDTAG_DesiredWidth, SCR_W,
                        BIDTAG_DesiredHeight, SCR_H,
                        BIDTAG_Depth, 8,
                        TAG_DONE);
    if (modeid != INVALID_ID)
        scr = OpenScreenTags(NULL,
                             SA_DisplayID, modeid,
                             SA_Width, SCR_W,
                             SA_Height, SCR_H,
                             SA_Depth, 8,
                             SA_Quiet, TRUE,
                             SA_Type, CUSTOMSCREEN,
                             SA_ShowTitle, FALSE,
                             TAG_DONE);
    if (!scr)
        scr = OpenScreenTags(NULL,
                             SA_Width, SCR_W,
                             SA_Height, SCR_H,
                             SA_Depth, 8,
                             SA_Quiet, TRUE,
                             SA_Type, CUSTOMSCREEN,
                             SA_ShowTitle, FALSE,
                             TAG_DONE);
    if (!scr) return 20;

    win = OpenWindowTags(NULL,
                         WA_CustomScreen, (ULONG)scr,
                         WA_Left, 0, WA_Top, 0,
                         WA_Width, SCR_W, WA_Height, SCR_H,
                         WA_Borderless, TRUE,
                         WA_Backdrop, TRUE,
                         WA_Activate, TRUE,
                         WA_RMBTrap, TRUE,
                         WA_SimpleRefresh, TRUE,
                         WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_CLOSEWINDOW,
                         TAG_DONE);
    if (!win) { CloseScreen(scr); return 20; }

    fb = (unsigned char *)AllocMem(SCR_W * SCR_H, MEMF_FAST | MEMF_CLEAR);
    if (!fb) fb = (unsigned char *)AllocMem(SCR_W * SCR_H, MEMF_ANY | MEMF_CLEAR);
    if (!fb) { CloseWindow(win); CloseScreen(scr); return 20; }

    demo_init(SCR_W, SCR_H);

    loadrgb[0] = (256UL << 16) | 0;
    for (i = 0; i < 256; i++) {
        loadrgb[1 + i * 3 + 0] = ((ULONG)demo_pal[i][0]) * 0x01010101UL;
        loadrgb[1 + i * 3 + 1] = ((ULONG)demo_pal[i][1]) * 0x01010101UL;
        loadrgb[1 + i * 3 + 2] = ((ULONG)demo_pal[i][2]) * 0x01010101UL;
    }
    loadrgb[1 + 256 * 3] = 0;
    LoadRGB32(&scr->ViewPort, loadrgb);
    ScreenToFront(scr);
    ActivateWindow(win);

    music_start();

    for (;;) {
        demo_frame(fb, tick);
        WriteChunkyPixels(win->RPort, 0, 0, SCR_W - 1, SCR_H - 1, fb, SCR_W);
        WaitTOF();
        tick++;
        if (tick > 8 && (quit = poll_input(win, selector, &selection)) >= 0)
            break;
    }

    if (selector)
        write_selector_choice(quit);

    music_stop();
    FreeMem(fb, SCR_W * SCR_H);
    CloseWindow(win);
    CloseScreen(scr);
    return quit;      /* selector: 0=Commando, 5=GunSmoke, 10=quit; game: 0=start, 5=quit */
}
