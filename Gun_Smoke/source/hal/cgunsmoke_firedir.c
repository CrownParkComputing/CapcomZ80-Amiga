/* src/hal/cgunsmoke_firedir.c -- Gun.Smoke 3-direction fire via a CD32 control pad.
 *
 * Gun.Smoke wants THREE independent fire buttons (left / straight / right shot ->
 * c001 BUTTON1=0x10, BUTTON2=0x20, BUTTON3=0x40). The earlier port tried to read the
 * 2nd/3rd buttons off the joystick POT pins (POTINP bit12/bit14) -- that path failed
 * repeatedly (the centre/main shot kept dropping). The user chose to read a CD32 pad
 * instead: amiberry presents joyport1=cd32joy and the USB gamepad's face buttons map to
 * the CD32 blue/red/yellow/green buttons, which we read here as a serial SHIFT REGISTER.
 *
 * CD32 SHIFT READ (canonical asman/wepl/JOTD routine, see refs/galaxian500/src/amiga/
 * ReadJoypad.68k and refs/scramble500/src/ReadJoypad.s -- this is the C transcription of
 * the port-1 path):
 *   - The fire line (CIAA PRA bit7 = port-1 fire) is briefly driven as an OUTPUT and
 *     pulled LOW to latch the pad's shift register.
 *   - Writing POTGO=0x6f00 (the port-1 pot-pin pattern) starts the clock.
 *   - We then clock 8 times by toggling the fire line hi/lo; before each clock we sample
 *     the serial data on POTINP bit14 (port-1 DATRY). The pad shifts out, MSB first:
 *       blue, red, yellow, green, R-shoulder, L-shoulder, Play/Pause, (id)
 *   - The lines are active-low: a CLEAR POTINP bit14 means that button is PRESSED, so we
 *     set the corresponding bit in the returned mask (mask bit set = pressed).
 *   - Afterwards we float the fire line back to input and restore POTGO (0xff00) so a
 *     plain 1-button joystick still reads normal fire on bit7 outside this routine.
 *
 * A plain (non-CD32) joystick wired to port 1 will ghost ALL bits as "pressed" during
 * this read; with amiberry in cd32joy mode that does not happen, and the keyboard
 * fallback in cgunsmoke_input.c covers host testing without a pad.
 */

#define CIAA_PRA   (*(volatile unsigned char  *)0xbfe001)  /* port-1 fire = bit7 */
#define CIAA_DDRA  (*(volatile unsigned char  *)0xbfe201)  /* data-direction for PRA  */
#define POTGO      (*(volatile unsigned short *)0xdff034)   /* write: pot-pin control  */
#define POTINP     (*(volatile unsigned short *)0xdff016)   /* read:  pot-pin data     */

#define PORT1_FIRE   0x80      /* CIAA PRA bit7 = port-1 fire / CD32 serial+clock line */
#define PORT1_DATRY  0x4000    /* POTINP bit14  = port-1 DATRY = CD32 serial data line  */
#define POTGO_PORT1  0x6f00    /* port-1 pot-pin pattern that starts the CD32 shift     */
#define POTGO_RESET  0xff00    /* restore (robinsonb5@eab value)                        */

/* Returned mask bits (1 = pressed), in CD32 shift order: */
#define CD32_BLUE       0x80   /* read 1st */
#define CD32_RED        0x40   /* read 2nd */
#define CD32_YELLOW     0x20   /* read 3rd */
#define CD32_GREEN      0x10   /* read 4th */
#define CD32_RSHOULDER  0x08   /* read 5th */
#define CD32_LSHOULDER  0x04   /* read 6th */
#define CD32_PLAY       0x02   /* read 7th */
#define CD32_ID         0x01   /* read 8th (pad-id marker)                              */

/* Read the CD32 pad on Amiga joystick port 1 as a shift register.
 * Returns a bitmask of pressed buttons (see CD32_* above; bit set = pressed). */
unsigned gunsmoke_read_cd32_port1(void)
{
    unsigned out = 0;
    int i;
    volatile unsigned char t;

    /* Drive the fire line as an output and pull it LOW -> latch the pad register. */
    CIAA_DDRA |= PORT1_FIRE;
    CIAA_PRA  &= (unsigned char)~PORT1_FIRE;

    /* Start the shift clock on the port-1 pot pins. */
    POTGO = POTGO_PORT1;

    /* Clock 8 bits out, MSB first (blue..id). */
    for (i = 7; i >= 0; i--) {
        /* wepl timing fix: settle the line with a few dummy CIA reads before sampling. */
        t = CIAA_PRA; t = CIAA_PRA; t = CIAA_PRA;
        t = CIAA_PRA; t = CIAA_PRA; t = CIAA_PRA; (void)t;

        if (!(POTINP & PORT1_DATRY))   /* active-low: clear bit14 == pressed */
            out |= (1u << i);

        /* Clock: drive fire hi then lo to shift in the next button. */
        CIAA_PRA |= PORT1_FIRE;
        CIAA_PRA &= (unsigned char)~PORT1_FIRE;
    }

    /* Float the fire line back to input and restore the pot pins. */
    CIAA_DDRA &= (unsigned char)~PORT1_FIRE;
    POTGO = POTGO_RESET;
    CIAA_PRA |= 0xC0;            /* reset port direction bits (both fire lines to input) */

    return out;
}

/* Map the CD32 face buttons -> Gun.Smoke c001 fire-direction bits (active-HIGH here; the
 * caller inverts into the active-low c001 byte). Layout (per the prompt's natural choice):
 *
 *   BLUE   (left face button)  -> BUTTON1 (0x10) = LEFT  shot
 *   RED    (bottom/centre)     -> BUTTON2 (0x20) = STRAIGHT shot
 *   GREEN  (right face button) -> BUTTON3 (0x40) = RIGHT shot
 *   YELLOW (top face button)   -> BUTTON3 (0x40) = RIGHT shot (shares with green)
 *
 * BUTTON1/2/3 = left/straight/right is a Gun.Smoke cabinet convention; capcom/gunsmoke.cpp
 * only labels them IPT_BUTTON1/2/3 (no PORT_NAME), so this mapping is the design choice. */
unsigned char gunsmoke_firedir_from_cd32(unsigned cd32)
{
    unsigned char b = 0;
    if (cd32 & CD32_BLUE)                     b |= 0x10;  /* BUTTON1 = LEFT     */
    if (cd32 & CD32_RED)                      b |= 0x20;  /* BUTTON2 = STRAIGHT */
    if (cd32 & (CD32_GREEN | CD32_YELLOW))    b |= 0x40;  /* BUTTON3 = RIGHT    */
    return b;
}
