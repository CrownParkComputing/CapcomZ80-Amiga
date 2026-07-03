/* src/hal/pl_machine.h
 * Portable Pac-Land machine model: the MC6809E main CPU + memory map, wired to
 * the mc6809 core. The HD63701 MCU is *stubbed* for now (Phase 2) -- inputs and
 * the MCU<->main handshake live in the shared CUS30 RAM and read back as 0,
 * which is fine for a boot trace / attract. No Amiga dependencies (host + AGA).
 */
#ifndef PL_MACHINE_H
#define PL_MACHINE_H
#include <stdint.h>

/* Load the assembled MAME "maincpu" region (0x20000: fixed ROM at 0x8000, banks
 * at 0x10000) and the MCU ROMs (internal 0x1000 + sub 0x2000). Before pl_init(). */
void pl_load_maincpu(const uint8_t *region20000);
void pl_load_mcu(const uint8_t *intern1000, const uint8_t *sub2000);

void pl_init(void);          /* reset both CPUs */
void pl_run_frame(void);     /* run ~one 60Hz frame (both CPUs interleaved) + vblank IRQ */
void pl_vblank(void);        /* assert the vblank IRQ to both CPUs (if enabled) */
int  pl_step(void);          /* step the main 6809 once; returns mc6809 fault code */

unsigned pl_mcu_pc(void);    /* MCU introspection */
int      pl_mcu_trap(void);
unsigned pl_dp(void);              /* main CPU direct-page register */
int      pl_main_irq_mask(void);   /* has the main CPU enabled its vblank IRQ? */
uint8_t  pl_peek(unsigned addr);   /* read main address space */
uint8_t  pl_cus30(unsigned i);     /* read shared CUS30 byte i (0..0x3ff) */
int           pl_main_ccI(void);       /* main CPU IRQ-mask flag (cc.i) */
unsigned long pl_dbg_main_irq(void);   /* times main serviced its vblank IRQ */
unsigned long pl_dbg_mcu_cus30(void);  /* MCU writes to shared CUS30 RAM */
unsigned long pl_dbg_w2000(void);      /* nonzero writes to [0x2000] */
unsigned long pl_dbg_irq_store(void);  /* times IRQ reached STA <$00 (0xde77) */
unsigned      pl_dbg_irq_dp(void);     /* DP at that STA */
unsigned      pl_dbg_irq_6a49(void);   /* [6a49] at that STA */

/* Video state, for a renderer to read. */
uint8_t *pl_videoram(int plane); /* plane 0 = fg (0x1000), 1 = bg (0x1000) */
uint8_t *pl_spriteram(void);     /* 0x1800 bytes */
int      pl_scroll(int which);   /* 9-bit scroll X for fg(0)/bg(1) */
int      pl_palette_bank(void);
void     pl_set_inputs(uint8_t in0, uint8_t in2);  /* IN0 coin/start, IN2 jump/L/R */

/* Introspection for the boot trace. */
unsigned      pl_pc(void);
unsigned long pl_cycles(void);
int           pl_fault(void);    /* 0 = none, else mc6809 fault code */
int           pl_cur_bank(void);

#endif
