* c1943_romdata.s -- embedded 1943 Kai: Midway Kaisen (1943kai) ROM images (incbin),
* raw MAME dumps from ../roms. Same Capcom hardware + regions as 1943; layout matches
* MAME capcom/1943.cpp ROM_START(1943kai) using board-position filenames. Symbols
* are kept identical to the 1943 port
* (_c1943_*) so the machine/renderer/glue are reused UNCHANGED -- only the ROM data
* differs. The i8751 MCU (bm.7k) is NOT emulated: 0xc007 reads 0x00, which Kai
* tolerates (confirmed by the old Kai transcode and by MAME 1943b on the same board).
        SECTION romdata,DATA

        XDEF _c1943_p0
        XDEF _c1943_p1
        XDEF _c1943_p2
        XDEF _c1943_gfx1
        XDEF _c1943_gfx2
        XDEF _c1943_gfx3
        XDEF _c1943_gfx4
        XDEF _c1943_tilerom
        XDEF _c1943_proms
        XDEF _c1943_snd

        CNOP 0,4
_c1943_snd:  INCBIN "../roms/bmk05.4k"      ; audio Z80 program 0x0000-0x7fff (0x8000)
        CNOP 0,4
_c1943_p0:   INCBIN "../roms/bmk01.12d"     ; maincpu fixed   0x0000-0x7fff (0x8000)
        CNOP 0,4
_c1943_p1:   INCBIN "../roms/bmk02.13d"     ; maincpu banks 0-3 -> region 0x10000 (0x10000)
        CNOP 0,4
_c1943_p2:   INCBIN "../roms/bmk03.14d"     ; maincpu banks 4-7 -> region 0x20000 (0x10000)

        CNOP 0,4
_c1943_gfx1: INCBIN "../roms/bmk04.5h"      ; chars 8x8 2bpp (0x8000)

        CNOP 0,4
_c1943_gfx2: INCBIN "../roms/bm15.10f"      ; bg tiles 32x32 4bpp, 0x40000 total (8 x 0x8000)
             INCBIN "../roms/bmk16.11f"
             INCBIN "../roms/bmk17.12f"
             INCBIN "../roms/bmk18.14f"
             INCBIN "../roms/bm19.10j"
             INCBIN "../roms/bmk20.11j"
             INCBIN "../roms/bmk21.12j"
             INCBIN "../roms/bmk22.14j"

        CNOP 0,4
_c1943_gfx3: INCBIN "../roms/bmk24.14k"     ; fg tiles 32x32 4bpp, 0x10000 total
             INCBIN "../roms/bmk25.14l"

        CNOP 0,4
_c1943_gfx4: INCBIN "../roms/bmk06.10a"     ; sprites 16x16 4bpp, 0x40000 total
             INCBIN "../roms/bmk07.11a"
             INCBIN "../roms/bmk08.12a"
             INCBIN "../roms/bmk09.14a"
             INCBIN "../roms/bmk10.10c"
             INCBIN "../roms/bmk11.11c"
             INCBIN "../roms/bmk12.12c"
             INCBIN "../roms/bmk13.14c"

        CNOP 0,4
_c1943_tilerom: INCBIN "../roms/bmk14.5f"   ; bg1 map @0x0000 (0x8000)
                INCBIN "../roms/bmk23.8k"   ; bg2 map @0x8000 (0x8000)

        CNOP 0,4
_c1943_proms: INCBIN "../roms/bmk1.12a"     ; 0x000 red
              INCBIN "../roms/bmk2.13a"     ; 0x100 green
              INCBIN "../roms/bmk3.14a"     ; 0x200 blue
              INCBIN "../roms/bmk5.7f"      ; 0x300 char lut
              INCBIN "../roms/bmk10.7l"     ; 0x400 fg(bg1) lut
              INCBIN "../roms/bmk9.6l"      ; 0x500 fg(bg1) bank
              INCBIN "../roms/bmk12.12m"    ; 0x600 bg(bg2) lut
              INCBIN "../roms/bmk11.12l"    ; 0x700 bg(bg2) bank
              INCBIN "../roms/bmk8.8c"      ; 0x800 sprite lut
              INCBIN "../roms/bmk7.7c"      ; 0x900 sprite bank + priority
              INCBIN "../roms/bm4.12c"      ; 0xa00 (unused)
              INCBIN "../roms/bm6.4b"       ; 0xb00 (unused)
        CNOP 0,4
        END
