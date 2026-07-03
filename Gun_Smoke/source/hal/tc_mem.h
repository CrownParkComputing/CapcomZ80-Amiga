/* src/hal/tc_mem.h -- where the Terra Cresta guest workspace lives.
 *
 * Musashi build:  tc_mem is a real C array (defined in c_terracre.c); the
 *   emulated 68000 reads/writes it through Musashi callbacks.
 *
 * NATIVE build (-DTC_MEM_BASE0):  the arcade 68000 program runs DIRECTLY on
 *   the host 68020 and uses absolute addressing into 0x000000-0x02ffff (it
 *   was assembled for the arcade map). So the workspace must physically live
 *   at address 0 -- tc_mem is simply a pointer to 0. The native runtime
 *   (tc_native.c) reserves that chip-RAM region (AllocAbs) and copies the ROM
 *   there; the renderer/audio read it through this same symbol, none the wiser.
 */
#ifndef TC_MEM_H
#define TC_MEM_H
#include <stdint.h>

#ifdef TC_MEM_BASE0
#define tc_mem ((uint8_t *)0)
#else
extern uint8_t tc_mem[];
#endif

#endif
