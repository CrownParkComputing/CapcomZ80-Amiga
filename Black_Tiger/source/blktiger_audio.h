/* blktiger_audio.h -- Black Tiger sound: audio Z80 (bd-06.1l) + 2x YM2203 ->
 * signed 8-bit PCM. Same Capcom 2xYM2203 sound board as 1943/Gun.Smoke; this is
 * c1943_audio.c adapted to Black Tiger's clocks + sound map. */
#ifndef BLKTIGER_AUDIO_H
#define BLKTIGER_AUDIO_H

/* init the sound Z80 + 2x YM2203 (registers the machine's audio hooks). Pass the
 * 0x8000 sound ROM (blktiger_snd / bd-06.1l). */
void bt_audio_init(const unsigned char *snd);

/* render nsamples of mixed PCM, advancing the sound CPU from emitted sample time. */
void bt_audio_render_samples(signed char *out, int nsamples);

/* compatibility wrapper for older host tools. */
void bt_audio_frame(signed char *out, int nsamples);

/* free the OPN core's alloc'd state on exit (call once at teardown). */
void bt_audio_shutdown(void);

/* host debug: PC of the sound CPU, + optional per-YM-write trace callback
 * (NULL in the Amiga build; the host harness sets it to dump register writes). */
unsigned bt_audio_pc(void);
extern void (*bt_audio_ym_trace)(int chip, int ad, unsigned char v);

#endif
