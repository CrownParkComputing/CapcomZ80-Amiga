| arcade_intro_glue.s -- supervisor helper for the ArcadeIntro ptplayer.

	.text
	.globl	ai_get_vbr

| void *ai_get_vbr(void)
| Returns the CPU Vector Base Register. Must be called in supervisor mode
| (the C side wraps this in SuperState()/UserState()).
ai_get_vbr:
	movec	vbr,d0
	rts
