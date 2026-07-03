| src/hal/gunsmoke_loader_glue.s -- tiny supervisor helper for the Gun.Smoke loader.
| GNU-as / m68k-amigaos-as syntax (assembled with -m68020 alongside ptplayer),
| because movec is a 68010+ privileged instruction that the vasm -m68000 pass
| used for the rest of the asm cannot emit. Mirrors src/hal/pl_loader_glue.s but
| Gun-Smoke-namespaced (gunsmoke_get_vbr) so the Gun.Smoke link is self-contained.

	.text
	.globl	gunsmoke_get_vbr

| void *gunsmoke_get_vbr(void)
|   Returns the CPU Vector Base Register. ptplayer's mt_install_cia needs it to
|   plant the CIA-B / level-6 (EXTER) autovector. MUST be called in SUPERVISOR
|   mode (cgunsmoke_loader.c wraps it in SuperState()/UserState()); movec is privileged.
gunsmoke_get_vbr:
	movec	vbr,d0
	rts
