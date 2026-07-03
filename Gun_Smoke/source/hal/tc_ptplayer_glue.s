| tc_ptplayer_glue.s -- C-callable wrappers around Frank Wille's ptplayer.
| GNU-as / m68k-amigaos-as syntax (NOT vasm). Assembled by build_terracre_native.sh
| with m68k-amigaos-as, same as ptplayer.68k / vbr.68k.
|
| ptplayer's interrupt handler references the absolute symbol `_custom`
| (=0xdff000); we define it here so the link resolves. The mt_* routines use a
| register-arg ABI (a6=_custom, a0/a1/d0...); these thin wrappers marshal the
| C stack-arg ABI into it. gcc (Bebbo) passes args on the stack and requires
| d2-d7/a2-a6 preserved across calls, so we save/restore them.

	.text

	.globl	_custom
	.set	_custom, 0x00dff000

	.globl	_tc_pt_install
	.globl	_tc_pt_init
	.globl	_tc_pt_start
	.globl	_tc_pt_end
	.globl	_tc_pt_playfx
	.globl	_tc_pt_loopfx
	.globl	_tc_pt_stopfx
	.globl	_tc_pt_musicmask
	.globl	_tc_pt_remove
	.globl	_tc_pt_mastervol

	.extern	_mt_install_cia
	.extern	_mt_init
	.extern	_mt_start
	.extern	_mt_end
	.extern	_mt_playfx
	.extern	_mt_loopfx
	.extern	_mt_stopfx
	.extern	_mt_musicmask
	.extern	_mt_remove_cia
	.extern	_mt_mastervol

| after movem.l d2-d7/a2-a6 (11 longs = 44 bytes): retaddr@44, arg1@48, arg2@52, arg3@56

| void _tc_pt_install(void *vbr, long palflag)
_tc_pt_install:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a0		| vbr
	move.l	52(sp),d0		| palflag
	lea	_custom,a6
	jsr	_mt_install_cia
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_init(void *mod, void *samples, long songpos)
_tc_pt_init:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a0		| module
	move.l	52(sp),a1		| samples (NULL = inside module)
	move.l	56(sp),d0		| song position
	lea	_custom,a6
	jsr	_mt_init
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_start(void)
_tc_pt_start:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	_custom,a6
	jsr	_mt_start
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_end(void)
_tc_pt_end:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	_custom,a6
	jsr	_mt_end
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_playfx(void *sfx)
_tc_pt_playfx:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a0		| sfx struct
	lea	_custom,a6
	jsr	_mt_playfx
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_loopfx(void *sfx)   -- looped sfx on a fixed channel (BGM)
_tc_pt_loopfx:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a0		| sfx struct
	lea	_custom,a6
	jsr	_mt_loopfx
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_stopfx(long channel)
_tc_pt_stopfx:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),d0		| channel 0..3
	lea	_custom,a6
	jsr	_mt_stopfx
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_musicmask(long mask)   -- reserve channels (bitmask) for music only
_tc_pt_musicmask:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),d0		| channel mask
	lea	_custom,a6
	jsr	_mt_musicmask
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_remove(void)
_tc_pt_remove:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	_custom,a6
	jsr	_mt_remove_cia
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void _tc_pt_mastervol(long vol)
_tc_pt_mastervol:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),d0		| volume 0..64
	lea	_custom,a6
	jsr	_mt_mastervol
	movem.l	(sp)+,d2-d7/a2-a6
	rts
