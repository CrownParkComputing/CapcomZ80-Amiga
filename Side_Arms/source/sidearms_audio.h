/* sidearms_audio.h -- Side Arms sound: audio Z80 (bd-06.1l) + 2x YM2203 ->
 * signed 8-bit PCM. Same Capcom 2xYM2203 sound board as 1943/Gun.Smoke; this is
 * c1943_audio.c adapted to Side Arms's clocks + sound map. */
#ifndef SIDEARMS_AUDIO_H
#define SIDEARMS_AUDIO_H

/* init the sound Z80 + 2x YM2203 (registers the machine's audio hooks). Pass the
 * 0x8000 sound ROM (sidearms_snd / bd-06.1l). */
void sidearms_audio_init(const unsigned char *snd);

/* render nsamples of mixed PCM, advancing the sound CPU from emitted sample time. */
void sidearms_audio_render_samples(signed char *out, int nsamples);

/* compatibility wrapper for older host tools. */
void sidearms_audio_frame(signed char *out, int nsamples);

/* free the OPN core's alloc'd state on exit (call once at teardown). */
void sidearms_audio_shutdown(void);

/* host debug: PC of the sound CPU, + optional per-YM-write trace callback
 * (NULL in the Amiga build; the host harness sets it to dump register writes). */
unsigned sidearms_audio_pc(void);
extern void (*sidearms_audio_ym_trace)(int chip, int ad, unsigned char v);

#endif
