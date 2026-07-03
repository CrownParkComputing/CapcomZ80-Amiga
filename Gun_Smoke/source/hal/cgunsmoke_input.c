/* src/hal/cgunsmoke_input.c -- Amiga joystick (port 2) + keyboard for Gun.Smoke,
 * OS-takeover register banging (mirrors commando_input.c / c1943_input.c).
 *
 * Gun.Smoke inputs (active-low, from MAME capcom/gunsmoke.cpp -- VERIFIED bits):
 *   SYSTEM 0xc000: START1=0x01 START2=0x02 VBLANK=0x08 SERVICE1=0x10 COIN1=0x40 COIN2=0x80
 *   P1     0xc001: RIGHT=0x01 LEFT=0x02 DOWN=0x04 UP=0x08 BUTTON1=0x10 BUTTON2=0x20 BUTTON3=0x40
 *
 * Controls: stick/cursors = move, any fire/CD32 face button shoots in the facing direction,
 * '5'/'6'/Back/CD32 shoulders = coin 1/2, '1'/Return/CD32 Play = start P1,
 * '2' = start P2, 'P'/CD32 Yellow+Green = pause, Esc/Q/hold all CD32 face buttons = exit.
 *
 * AXIS REMAP: NONE (first-light). The renderer's ROT270 transform reproduces the
 * cabinet view (same transform as the MAME-verified tools/cgunsmoke_shot.c), so the
 * game's own direction bits should line up with the screen. If on-device the stick
 * feels rotated, swap the g_* mapping below (this is the known first-light tuning knob).
 */
#include "z80emu.h"
extern void gunsmoke_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2);

/* CD32 pad 3-direction fire (src/hal/cgunsmoke_firedir.c). The pot-pin (POTINP bit12/14)
 * read for the 2nd/3rd buttons failed repeatedly, so the three fire directions now come
 * from a CD32 control pad read as a serial shift register on joystick port 1. */
extern unsigned gunsmoke_read_cd32_port1(void);
extern unsigned char gunsmoke_firedir_from_cd32(unsigned cd32);

#define JOY1DAT  (*(volatile unsigned short *)0xdff00c)
#define CIAA_PRA (*(volatile unsigned char  *)0xbfe001)
#define POTGO    (*(volatile unsigned short *)0xdff034)
#define POTINP   (*(volatile unsigned short *)0xdff016)
#define CIAA_SDR (*(volatile unsigned char  *)0xbfec01)
#define CIAA_ICR (*(volatile unsigned char  *)0xbfed01)
#define CIAA_CRA (*(volatile unsigned char  *)0xbfee01)

#define KEY_1 0x01
#define KEY_2 0x02
#define KEY_5 0x05
#define KEY_6 0x06
#define KEY_Q 0x10
#define KEY_P 0x19
#define KEY_X     0x32
#define KEY_SPACE 0x40
#define KEY_BACKSPACE 0x41
#define KEY_NUMENTER 0x43
#define KEY_RETURN 0x44
#define KEY_ESC 0x45
#define KEY_LCTRL 0x63
#define KEY_LALT  0x64
#define KEY_UP    0x4C
#define KEY_DOWN  0x4D
#define KEY_RIGHT 0x4E
#define KEY_LEFT  0x4F

static unsigned char keydown[128];
static int prev_coin_btn = 0, coin_pulse = 0, coin_repeat = 0, start_pulse = 0;
static int prev_pause_btn = 0, exit_combo_hold = 0;
static volatile int pause_toggle = 0, exit_request = 0;
static int facing = 0;       /* -1 left, 0 straight, +1 right; latched until horizontal move */
static int suppress_until_release = 0;

#define CD32_BLUE       0x80
#define CD32_RED        0x40
#define CD32_YELLOW     0x20
#define CD32_GREEN      0x10
#define CD32_RSHOULDER  0x08
#define CD32_LSHOULDER  0x04
#define CD32_PLAY       0x02

static void poll_keyboard(void)
{
    int guard = 0;
    while ((CIAA_ICR & 0x08) && ++guard < 16) {
        unsigned char raw = CIAA_SDR;
        CIAA_CRA |= 0x40;
        for (volatile int i = 0; i < 350; i++) { }
        CIAA_CRA &= ~0x40;
        unsigned char code = (unsigned char)~((raw >> 1) | (raw << 7));
        keydown[code & 0x7f] = (code & 0x80) ? 0 : 1;
    }
}

int gunsmoke_input_pause_toggled(void)
{
    int v = pause_toggle;
    pause_toggle = 0;
    return v;
}

int gunsmoke_input_exit_requested(void)
{
    return exit_request;
}

void gunsmoke_input_suppress_until_release(void)
{
    suppress_until_release = 1;
    coin_pulse = 0;
    start_pulse = 0;
}

void gunsmoke_read_inputs(MY_LITTLE_Z80 *z)
{
    (void)z;
#ifdef GUNSMOKE_AUTOPLAY
    /* DIAGNOSTIC: auto coin+start so a headless Amiberry boot drives itself into
     * gameplay (for screenshot capture). Revert by not defining GUNSMOKE_AUTOPLAY. */
    static int af = 0; af++;
    unsigned char asys = 0xff, ap1 = 0xff;
    if (af >= 60 && af < 90) asys &= ~0x40;             /* COIN1  */
    if (af >= 200 && (af % 120) < 14) asys &= ~0x01;    /* START1 */
    if (af >= 700) ap1 &= ~0x10;                        /* hold fire */
    gunsmoke_set_inputs(asys, ap1, 0xff);
    return;
#endif
    poll_keyboard();
    POTGO = 0xFF01;       /* HRM port-1 pot pins: drive DATRX/DATRY high before reading buttons */

    unsigned v = JOY1DAT;
    int d_right = (v >> 1) & 1;
    int d_left  = (v >> 9) & 1;
    int d_down  = ((v >> 1) ^ v) & 1;
    int d_up    = ((v >> 9) ^ (v >> 8)) & 1;
    if (keydown[KEY_RIGHT]) d_right = 1;
    if (keydown[KEY_LEFT])  d_left  = 1;
    if (keydown[KEY_DOWN])  d_down  = 1;
    if (keydown[KEY_UP])    d_up    = 1;

    int raw_fire1 = !(CIAA_PRA & 0x80);
    unsigned short pot = POTINP;
    int raw_fire2 = !(pot & 0x1000);     /* pin 9 / DATRX -> BUTTON2 */
    int raw_fire3 = !(pot & 0x4000);     /* pin 5 / DATRY -> BUTTON3 */

    /* One-fire-button mode: any physical fire/face button shoots. Horizontal movement
     * updates the facing latch; firing while neutral keeps the last chosen direction. */
    unsigned cd32 = gunsmoke_read_cd32_port1();
    unsigned char fb = gunsmoke_firedir_from_cd32(cd32);
    int pause_btn = ((cd32 & (CD32_YELLOW | CD32_GREEN)) == (CD32_YELLOW | CD32_GREEN));
    int exit_combo = ((cd32 & (CD32_BLUE | CD32_RED | CD32_YELLOW | CD32_GREEN)) ==
                      (CD32_BLUE | CD32_RED | CD32_YELLOW | CD32_GREEN));
    int pause_req = keydown[KEY_P] || pause_btn;
    int any_fire = raw_fire1 || raw_fire2 || raw_fire3 || fb ||
                   keydown[KEY_LCTRL] || keydown[KEY_SPACE] || keydown[KEY_LALT] || keydown[KEY_X];
    if (pause_req && !prev_pause_btn) pause_toggle = 1;
    prev_pause_btn = pause_req;
    if (keydown[KEY_Q] || keydown[KEY_ESC]) exit_request = 1;
    if (exit_combo) {
        if (++exit_combo_hold >= 60) exit_request = 1;
    } else {
        exit_combo_hold = 0;
    }
    if (d_right && !d_left) facing = 1;
    else if (d_left && !d_right) facing = -1;
    int fireL = any_fire && (facing < 0);     /* BUTTON1 = left shot     */
    int fireC = any_fire && (facing == 0);    /* BUTTON2 = straight shot */
    int fireR = any_fire && (facing > 0);     /* BUTTON3 = right shot    */

    /* stick 1:1 to game directions (first-light; tune here if rotated on device) */
    int g_up = d_up, g_down = d_down, g_left = d_left, g_right = d_right;

    unsigned char p1 = 0xff;
    if (g_right) p1 &= ~0x01;      /* RIGHT */
    if (g_left)  p1 &= ~0x02;      /* LEFT  */
    if (g_down)  p1 &= ~0x04;      /* DOWN  */
    if (g_up)    p1 &= ~0x08;      /* UP    */
    if (fireL)   p1 &= ~0x10;      /* BUTTON1 = LEFT shot     */
    if (fireC)   p1 &= ~0x20;      /* BUTTON2 = STRAIGHT shot */
    if (fireR)   p1 &= ~0x40;      /* BUTTON3 = RIGHT shot    */

    unsigned char sys = 0xff;
    int coin_key = keydown[KEY_5] || keydown[KEY_BACKSPACE];
    int coin2_key = keydown[KEY_6];
    int start_key = keydown[KEY_1] || keydown[KEY_RETURN] || keydown[KEY_NUMENTER];
    int start2_key = keydown[KEY_2];
    int cd32_coin = (cd32 & (CD32_LSHOULDER | CD32_RSHOULDER)) != 0;
    int cd32_start = (cd32 & CD32_PLAY) != 0;

    if (suppress_until_release) {
        int held = d_right || d_left || d_down || d_up || any_fire ||
                   coin_key || coin2_key || start_key || start2_key || cd32_coin || cd32_start;
        if (!held) suppress_until_release = 0;
        gunsmoke_set_inputs(0xff, 0xff, 0xff);
        return;
    }

    if (coin_key)  sys &= ~0x40;        /* COIN1  */
    if (coin2_key) sys &= ~0x80;        /* COIN2  */
    if (start_key) sys &= ~0x01;        /* START1 */
    if (start2_key) sys &= ~0x02;       /* START2 */

    if (cd32_coin) {
        if (!prev_coin_btn || coin_repeat <= 0) {
            coin_pulse = 10;
            coin_repeat = 30;
        } else {
            coin_repeat--;
        }
    } else {
        coin_repeat = 0;
    }
    if (cd32_start) start_pulse = 60;
    prev_coin_btn = cd32_coin;
    if (coin_pulse  > 0) { sys &= ~0x40; coin_pulse--;  }
    if (start_pulse > 0) { sys &= ~0x01; start_pulse--; }

    gunsmoke_set_inputs(sys, p1, 0xff);
}
