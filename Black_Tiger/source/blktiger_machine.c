/* blktiger_machine.c -- Black Tiger main-Z80 machine model (from MAME blktiger.cpp).
 *
 * Memory (main_prg_map):
 *   0x0000-0x7fff  fixed ROM            (in z->memory)
 *   0x8000-0xbfff  banked ROM, 16x16K   (copied into z->memory on OUT 0x01)
 *   0xc000-0xcfff  BG video RAM WINDOW  -> scrollram[scroll_bank + (a&0xfff)]  (BANKED via OUT 0x0d)
 *   0xd000-0xd7ff  text video RAM       (in z->memory)
 *   0xd800-0xdfff  palette RAM lo/hi    (in z->memory; xBRG_444)
 *   0xe000-0xfdff  work RAM             (in z->memory)
 *   0xfe00-0xffff  sprite RAM           (in z->memory; buffered at vblank)
 *
 * I/O is PORT-mapped (z80 IN/OUT -> in_impl/out_impl). See the spec in the header.
 * Built with -DZ80_MAP_BLKTIGER so z80.c traps only the 0xc000 window on reads and
 * everything <0xd000 on writes (ROM ignored, bg window redirected). */
#include <string.h>
#include "z80emu.h"
#include "blktiger_machine.h"

/* main Z80 @ 24MHz/4 = 6MHz, 384x262 @ 6MHz pixclk -> 59.637Hz */
#define MAIN_CYCLES_PER_FRAME 100609
#define FRAME_SLICES 16

static const unsigned char *region;          /* maincpu 0x50000 */
static const unsigned char *gfx_chars, *gfx_tiles, *gfx_sprites;

static unsigned char scrollram[0x4000];       /* 4 x 0x1000 BG banks */
static unsigned char spr_buffer[0x200];        /* spriteram latched at vblank */

static int cur_bank, scroll_bank;
static int scroll_x, scroll_y, screen_layout;
static int chon, bgon, objon, flip;
static int soundlatch, mcu_latch;
static unsigned char in_sys=0xff, in_p1=0xff, in_p2=0xff, in_dsw0=0xff, in_dsw1=0xff;
static MY_LITTLE_Z80 *main_z;

/* sound-CPU delegation: blktiger_audio.c (when linked) registers the sound Z80
 * context + its rd/wr handlers here; the shared z80.c calls machine_rd/wr for
 * BOTH CPUs, so we route by context pointer. The latch hook posts main-CPU
 * OUT-0x00 sound commands into the audio soundlatch FIFO. All null when audio
 * isn't linked (e.g. the bare host harness), leaving the main CPU unaffected. */
MY_LITTLE_Z80 *bt_audio_z = 0;
void          (*bt_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) = 0;
unsigned char (*bt_audio_rd_hook)(MY_LITTLE_Z80 *z, unsigned a) = 0;
void          (*bt_latch_hook)(unsigned char v) = 0;

/* ------------------------------------------------------------------ loaders */
void bt_load_maincpu(const unsigned char *p){ region = p; }
void bt_set_gfx(const unsigned char *c, const unsigned char *t, const unsigned char *s){
    gfx_chars=c; gfx_tiles=t; gfx_sprites=s;
}

static void map_bank(MY_LITTLE_Z80 *z){
    memcpy(z->memory + 0x8000, region + 0x10000 + cur_bank*0x4000, 0x4000);
}

/* ------------------------------------------------------------------ memory */
/* z80.c (Z80_MAP_BLKTIGER) only traps reads of the 0xc000 window and writes
 * below 0xd000 -- so machine_rd handles the banked BG window; machine_wr the
 * banked BG window (redirect) and ROM (ignore). */
unsigned char machine_rd(MY_LITTLE_Z80 *z, unsigned int a){
    if(z == bt_audio_z) return bt_audio_rd_hook ? bt_audio_rd_hook(z, a) : z->memory[a];
    if((a & 0xf000u)==0xc000u) return scrollram[scroll_bank + (a & 0x0fff)];
    return z->memory[a];
}
void machine_wr(MY_LITTLE_Z80 *z, unsigned int a, unsigned char v){
    if(z == bt_audio_z){ if(bt_audio_wr_hook) bt_audio_wr_hook(z, a, v); return; }
    if((a & 0xf000u)==0xc000u){ scrollram[scroll_bank + (a & 0x0fff)] = v; return; }
    if(a >= 0xd000u){ z->memory[a] = v; return; }   /* text/palette/RAM/sprite + trapped 0xe000-3 */
    /* a < 0xc000 : ROM -- ignore */
}

/* ------------------------------------------------------------------ I/O ports */
unsigned char in_impl(MY_LITTLE_Z80 *z, int port){
    (void)z;
    switch(port & 0x0f){
    case 0x00: return in_sys;     /* IN0 SYSTEM (start/coin/service) */
    case 0x01: return in_p1;      /* IN1 P1 */
    case 0x02: return in_p2;      /* IN2 P2 */
    case 0x03: return in_dsw0;    /* DSW0 (coinage/flip/test) */
    case 0x04: return in_dsw1;    /* DSW1 (lives/diff/demo/continue/cabinet) */
    case 0x05: return 0x01;       /* FREEZE off (bit0=1 normal) */
    case 0x07: return (unsigned char)mcu_latch;  /* from_mcu (i8751) -- stubbed for now */
    }
    return 0xff;
}
void out_impl(MY_LITTLE_Z80 *z, int port, unsigned char v){
    switch(port & 0x0f){
    case 0x00: soundlatch = v;                              /* -> sound Z80 soundlatch */
               if(bt_latch_hook) bt_latch_hook(v); break;
    case 0x01: cur_bank = v & 0x0f; map_bank(z); break;     /* bankswitch */
    case 0x03: break;                                       /* coin lockout */
    case 0x04: /* video_control */
        flip = (v & 0x40) ? 1 : 0;
        chon = (v & 0x80) ? 0 : 1;                          /* chars ON when bit7=0 */
        break;
    case 0x06: break;                                       /* watchdog */
    case 0x07: mcu_latch = v; break;   /* to_mcu: no-MCU bootleg set has the protection
                                        * patched out of the main code, so port 0x07 is inert. */
    case 0x08: scroll_x = (scroll_x & 0xff00) | v; break;
    case 0x09: scroll_x = (scroll_x & 0x00ff) | (v << 8); break;
    case 0x0a: scroll_y = (scroll_y & 0xff00) | v; break;
    case 0x0b: scroll_y = (scroll_y & 0x00ff) | (v << 8); break;
    case 0x0c: /* video_enable */
        bgon  = (v & 0x02) ? 0 : 1;                         /* BG ON when bit1=0 */
        objon = (v & 0x04) ? 0 : 1;                         /* sprites ON when bit2=0 */
        break;
    case 0x0d: scroll_bank = (v % 4) * 0x1000; break;       /* BG window bank */
    case 0x0e: screen_layout = v; break;                    /* wide(8x4) vs tall(4x8) tilemap */
    }
}

/* ------------------------------------------------------------------ lifecycle */
void bt_init(MY_LITTLE_Z80 *z){
    main_z = z;
    memset(z->memory, 0, sizeof z->memory);
    memcpy(z->memory, region, 0x8000);     /* fixed ROM 0x0000-0x7fff */
    z->opcodes = 0; z->opcodes_len = 0;
    cur_bank = 0; map_bank(z);
    memset(scrollram, 0, sizeof scrollram);
    memset(spr_buffer, 0, sizeof spr_buffer);
    scroll_bank = scroll_x = scroll_y = screen_layout = 0;
    chon = bgon = objon = 1; flip = 0; soundlatch = 0; mcu_latch = 0xff;
    Z80Reset(&z->state);
}

void bt_run_frame(MY_LITTLE_Z80 *z){
    const int per = MAIN_CYCLES_PER_FRAME / FRAME_SLICES;
    const int vbl = 224 * FRAME_SLICES / 262;   /* vblank starts ~scanline 224 */
    int irq = 0;
    for(int s=0; s<FRAME_SLICES; s++){
        if(s == vbl){
            memcpy(spr_buffer, z->memory + 0xfe00, 0x200);  /* sprite RAM latched at vblank */
            irq = 1;                                         /* irq0_line_hold */
        }
        if(irq && Z80Interrupt(&z->state, 0xff, z) != 0) irq = 0;  /* hold until accepted */
        Z80Emulate(&z->state, per, z);
    }
}

void bt_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2,
                   unsigned char dsw0, unsigned char dsw1){
    in_sys=sys; in_p1=p1; in_p2=p2; in_dsw0=dsw0; in_dsw1=dsw1;
}

/* ------------------------------------------------------------------ accessors */
const unsigned char *bt_scrollram(void){ return scrollram; }
const unsigned char *bt_txram(MY_LITTLE_Z80 *z){ return z->memory + 0xd000; }
const unsigned char *bt_palette(MY_LITTLE_Z80 *z){ return z->memory + 0xd800; }
const unsigned char *bt_spritebuf(void){ return spr_buffer; }
const unsigned char *bt_chars(void){ return gfx_chars; }
const unsigned char *bt_tiles(void){ return gfx_tiles; }
const unsigned char *bt_sprites(void){ return gfx_sprites; }
int bt_scrollx(void){ return scroll_x; }
int bt_scrolly(void){ return scroll_y; }
int bt_screen_layout(void){ return screen_layout; }
int bt_chon(void){ return chon; }
int bt_bgon(void){ return bgon; }
int bt_objon(void){ return objon; }
int bt_flip(void){ return flip; }
unsigned bt_pc(MY_LITTLE_Z80 *z){ return z->state.pc; }
int bt_cur_bank(void){ return cur_bank; }
unsigned char bt_soundlatch(void){ return (unsigned char)soundlatch; }   /* last OUT-0x00 byte */

/* Rust static-transcode bridge hooks. These mirror the names expected by
 * recompile_blktiger's prelude and are inert until a native runner is linked. */
unsigned char cblktiger_scrollram_read(unsigned short off)
{
    return scrollram[scroll_bank + (off & 0x0fff)];
}

void cblktiger_scrollram_write(unsigned short off, unsigned char v)
{
    scrollram[scroll_bank + (off & 0x0fff)] = v;
}

unsigned char cblktiger_port_in(unsigned char port)
{
    return in_impl(main_z, port);
}

void cblktiger_port_out(unsigned char *mem, unsigned char port, unsigned char v)
{
    (void)mem;
    if (main_z) out_impl(main_z, port, v);
}

unsigned char cblktiger_active_bank(void)
{
    return (unsigned char)(cur_bank & 0x0f);
}
