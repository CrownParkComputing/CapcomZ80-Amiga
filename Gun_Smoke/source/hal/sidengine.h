/*
 * sidengine.h - public API for the compact SID replay engine.
 * Host prototype (adapted from TinySID, public/free-use lineage).
 */
#ifndef SIDENGINE_H
#define SIDENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the SID synth for a given output sample rate (Hz). */
void sidengine_init_synth(unsigned int mixfrq);

/* Full reset: clear 64KB RAM, reset CPU + synth, set sample rate. */
void sidengine_c64init(unsigned int mixfrq);

/* Parse a PSID/RSID image already in memory.  Returns the C64 load
 * address; fills the out-params with the replay entry points.  Song
 * indices returned are 0-based (subsong 1 -> 0). */
int sidengine_load(const unsigned char *data, int size,
                   unsigned short *init_addr, unsigned short *play_addr,
                   unsigned char *start_song, unsigned char *num_songs,
                   unsigned char *speed);

/* Run the 6510 from npc (with A=na) until it returns.  Use for both
 * init (npc=init_addr, na=subsong) and per-frame play (npc=play_addr). */
int sidengine_cpujsr(unsigned short npc, unsigned char na);

/* Render `len` mono 16-bit samples using the current SID register state. */
void sidengine_render(short *buffer, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif /* SIDENGINE_H */
