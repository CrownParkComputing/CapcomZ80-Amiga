; sidearms_cgx.s -- minimal cybergraphics.library call stubs for the Side Arms RTG
; presenter.  This toolchain ships NO cybergraphics SDK header, so we hand-roll the
; two calls the truecolor presenter needs.  C-stack ABI (bebbo m68k-amigaos): all
; args are 4-byte stack slots (the C prototypes declare every arg `unsigned long`),
; pushed right-to-left; d0-d1/a0-a1 are scratch, d2-d7/a2-a6 callee-saved.
; The library base lives in the C global _CyberGfxBase (a6 = base for the jsr).
;
; LVOs (from the cybergraphics_lib pragma offsets):
;   BestCModeIDTagList(a0)                                   -> -60  (0x3c)
;   WritePixelArray(a0,d0,d1,d2,a1,d3,d4,d5,d6,d7)           -> -126 (0x7e)

        XDEF    _BestCModeIDTagList
        XDEF    _WritePixelArray
        XREF    _CyberGfxBase

        SECTION code,CODE

; ULONG BestCModeIDTagList(struct TagItem *taglist)
_BestCModeIDTagList:
        move.l  a6,-(sp)
        move.l  _CyberGfxBase,a6
        move.l  8(sp),a0              ; taglist  (4 saved + 4 ret = 8)
        jsr     -60(a6)
        move.l  (sp)+,a6
        rts

; ULONG WritePixelArray(APTR src, UWORD sx, UWORD sy, UWORD srcmod,
;                       struct RastPort *rp, UWORD dx, UWORD dy,
;                       UWORD w, UWORD h, UBYTE fmt)
_WritePixelArray:
        movem.l d2-d7/a6,-(sp)        ; 7 longs = 28 bytes pushed
        move.l  _CyberGfxBase,a6
        move.l  32(sp),a0             ; src    (28 saved + 4 ret = 32)
        move.l  36(sp),d0             ; sx
        move.l  40(sp),d1             ; sy
        move.l  44(sp),d2             ; srcmod
        move.l  48(sp),a1             ; rastport
        move.l  52(sp),d3             ; dx
        move.l  56(sp),d4             ; dy
        move.l  60(sp),d5             ; w
        move.l  64(sp),d6             ; h
        move.l  68(sp),d7             ; fmt
        jsr     -126(a6)
        movem.l (sp)+,d2-d7/a6
        rts

        END
