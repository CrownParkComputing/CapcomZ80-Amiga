/* src/hal/c1942_input.c -- read Amiga joystick (port 2) + keyboard in OS-takeover
 * mode and feed 1942's inputs. Pure register banging (no OS).
 *
 * Controls (gamepad on port 2 via joyport1=cd32joy, or pure keyboard):
 *   move    = joystick / cursor keys
 *   shoot   = fire button (BUTTON1)      [gamepad A,  or L-Ctrl / Space]
 *   loop    = BUTTON2 (loop-de-loop)     [gamepad B mapped to L-Alt]
 *   coin    = '5' / Back / Esc / CD32 shoulder/select-style
 *   start   = '1' / Return / CD32 Play
 *   pause   = 'P' / CD32 Yellow+Green
 *   exit    = 'Q' / hold all CD32 face buttons
 *
 * 1942 ports (active-low, 0=pressed):
 *   SYSTEM: bit0 START1, bit1 START2, bit6 COIN2, bit7 COIN1
 *   P1:     bit0 RIGHT, bit1 LEFT, bit2 DOWN, bit3 UP, bit4 BUTTON1, bit5 BUTTON2
 */
extern void c1942_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2);

#define JOY1DAT (*(volatile unsigned short *)0xdff00c)   /* port 2 joystick */
#define CIAA_PRA (*(volatile unsigned char  *)0xbfe001)  /* fire1 (port2 = bit7) */
#define CIAA_DDRA (*(volatile unsigned char *)0xbfe201)
#define POTGO    (*(volatile unsigned short *)0xdff034)   /* pot control */
#define POTINP   (*(volatile unsigned short *)0xdff016)   /* fire2/fire3 (port2) */
#define CIAA_SDR (*(volatile unsigned char  *)0xbfec01)  /* keyboard serial */
#define CIAA_ICR (*(volatile unsigned char  *)0xbfed01)
#define CIAA_CRA (*(volatile unsigned char  *)0xbfee01)

/* Amiga rawkey scancodes */
#define KEY_1     0x01
#define KEY_5     0x05
#define KEY_Q     0x10
#define KEY_P     0x19
#define KEY_SPACE 0x40
#define KEY_BACKSPACE 0x41
#define KEY_NUMENTER 0x43
#define KEY_RETURN 0x44
#define KEY_ESC   0x45
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

#define CD32_BLUE       0x80
#define CD32_RED        0x40
#define CD32_YELLOW     0x20
#define CD32_GREEN      0x10
#define CD32_RSHOULDER  0x08
#define CD32_LSHOULDER  0x04
#define CD32_PLAY       0x02
#define CD32_PORT1_FIRE 0x80
#define CD32_PORT1_DATRY 0x4000

static unsigned read_cd32_port1(void)
{
    unsigned out = 0;
    volatile unsigned char t;

    CIAA_DDRA |= CD32_PORT1_FIRE;
    CIAA_PRA &= (unsigned char)~CD32_PORT1_FIRE;
    POTGO = 0x6f00;

    for (int i = 7; i >= 0; i--) {
        t = CIAA_PRA; t = CIAA_PRA; t = CIAA_PRA;
        t = CIAA_PRA; t = CIAA_PRA; t = CIAA_PRA; (void)t;
        if (!(POTINP & CD32_PORT1_DATRY)) out |= (1u << i);
        CIAA_PRA |= CD32_PORT1_FIRE;
        CIAA_PRA &= (unsigned char)~CD32_PORT1_FIRE;
    }

    CIAA_DDRA &= (unsigned char)~CD32_PORT1_FIRE;
    POTGO = 0xff00;
    CIAA_PRA |= 0xc0;
    return out;
}

/* poll the CIA-A keyboard serial: decode one keycode if present, handshake. */
static void poll_keyboard(void)
{
    int guard = 0;
    while ((CIAA_ICR & 0x08) && ++guard < 16) {       /* drain ALL pending keycodes */
        unsigned char raw = CIAA_SDR;
        CIAA_CRA |= 0x40;                  /* SP = output -> pulls line (ack) */
        for (volatile int i = 0; i < 350; i++) { }    /* ~85us+ handshake pulse */
        CIAA_CRA &= ~0x40;
        unsigned char code = (unsigned char)~((raw >> 1) | (raw << 7));
        keydown[code & 0x7f] = (code & 0x80) ? 0 : 1;  /* bit7 = key-up */
    }
}

int c1942_input_pause_toggled(void)
{
    int v = pause_toggle;
    pause_toggle = 0;
    return v;
}

int c1942_input_exit_requested(void)
{
    return exit_request;
}

void c1942_read_inputs(void)
{
    static int potinit = 0;
    if (!potinit) { POTGO = 0xFF00; potinit = 1; }   /* pot pins -> button inputs */
    poll_keyboard();

    unsigned cd32 = read_cd32_port1();
    unsigned v = JOY1DAT;                   /* digital joystick on port 2 */
    int right = (v >> 1) & 1;
    int left  = (v >> 9) & 1;
    int down  = ((v >> 1) ^ v) & 1;
    int up    = ((v >> 9) ^ (v >> 8)) & 1;
    unsigned short pot = POTINP;
    int fire1 = !(CIAA_PRA & 0x80);         /* button 1 (red)  */
    int fire2 = !(pot & 0x4000);            /* button 2 (blue, port2 POTY) */
    int cd32_shoot = (cd32 & (CD32_BLUE | CD32_RED)) != 0;
    int cd32_loop = (cd32 & (CD32_YELLOW | CD32_GREEN)) != 0;
    int pause_btn = ((cd32 & (CD32_YELLOW | CD32_GREEN)) == (CD32_YELLOW | CD32_GREEN));
    int exit_combo = ((cd32 & (CD32_BLUE | CD32_RED | CD32_YELLOW | CD32_GREEN)) ==
                      (CD32_BLUE | CD32_RED | CD32_YELLOW | CD32_GREEN));
    int pause_req = keydown[KEY_P] || pause_btn;

    if (pause_req && !prev_pause_btn) pause_toggle = 1;
    prev_pause_btn = pause_req;
    if (keydown[KEY_Q]) exit_request = 1;
    if (exit_combo) {
        if (++exit_combo_hold >= 60) exit_request = 1;
    } else {
        exit_combo_hold = 0;
    }

    /* keyboard fallbacks so pure-keyboard play works too */
    if (keydown[KEY_RIGHT]) right = 1;
    if (keydown[KEY_LEFT])  left  = 1;
    if (keydown[KEY_DOWN])  down  = 1;
    if (keydown[KEY_UP])    up    = 1;
    int shoot = fire1 || cd32_shoot || keydown[KEY_LCTRL] || keydown[KEY_SPACE];
    int loop  = fire2 || cd32_loop || keydown[KEY_LALT];

    unsigned char p1 = 0xff;
    if (right) p1 &= ~0x01;
    if (left)  p1 &= ~0x02;
    if (down)  p1 &= ~0x04;
    if (up)    p1 &= ~0x08;
    if (shoot) p1 &= ~0x10;                 /* BUTTON1 = shoot */
    if (loop)  p1 &= ~0x20;                 /* BUTTON2 = loop-de-loop */

    int coin_key = keydown[KEY_5] || keydown[KEY_ESC] || keydown[KEY_BACKSPACE];
    int start_key = keydown[KEY_1] || keydown[KEY_RETURN] || keydown[KEY_NUMENTER];
    int coin_btn = ((cd32 & (CD32_LSHOULDER | CD32_RSHOULDER)) != 0) || coin_key;
    int start_btn = ((cd32 & CD32_PLAY) != 0) || start_key;

    if (coin_btn) {
        if (!prev_coin_btn || coin_repeat <= 0) {
            coin_pulse = 12;
            coin_repeat = 30;
        } else {
            coin_repeat--;
        }
    } else {
        coin_repeat = 0;
    }
    prev_coin_btn = coin_btn;
    if (start_btn) start_pulse = 60;

    /* Match Commando: D-pad is movement only, never coin/start. */
    unsigned char sys = 0xff;
    if (coin_pulse > 0) sys &= ~0x80;                     /* COIN1 */
    if (start_pulse > 0)      sys &= ~0x01;               /* START1 */
    if (coin_pulse) coin_pulse--;
    if (start_pulse) start_pulse--;

    c1942_set_inputs(sys, p1, 0xff);
}
