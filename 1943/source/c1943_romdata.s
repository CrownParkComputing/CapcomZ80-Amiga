* c1943_romdata.s -- embedded 1943 (1943b bootleg) ROM images (incbin), raw MAME
* dumps from ../roms. Each region is laid out CONTIGUOUSLY so the C glue can pass a
* single base pointer straight to the renderer (only maincpu needs reassembly for
* the bank gap). Region order/layout matches MAME capcom/1943.cpp ROM_START(1943b).
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
_c1943_snd:  INCBIN "../roms/bm05.4k"      ; audio Z80 program 0x0000-0x7fff (0x8000)
        CNOP 0,4
_c1943_p0:   INCBIN "../roms/1.12d"        ; maincpu fixed   0x0000-0x7fff (0x8000)
        CNOP 0,4
_c1943_p1:   INCBIN "../roms/bm02.13d"     ; maincpu banks 0-3 -> region 0x10000 (0x10000)
        CNOP 0,4
_c1943_p2:   INCBIN "../roms/bm03.14d"     ; maincpu banks 4-7 -> region 0x20000 (0x10000)

        CNOP 0,4
_c1943_gfx1: INCBIN "../roms/4.5h"         ; chars 8x8 2bpp (0x8000)

        CNOP 0,4
_c1943_gfx2: INCBIN "../roms/15.12f"       ; bg1 tiles 32x32 4bpp, 0x40000 total
             INCBIN "../roms/16.14f"       ;   (low plane pair = 15,16)
             INCBIN "../roms/17.12j"       ;   (high plane pair = 17,18)
             INCBIN "../roms/18.14j"

        CNOP 0,4
_c1943_gfx3: INCBIN "../roms/bm24.14k"     ; bg2 tiles 32x32 4bpp, 0x10000 total
             INCBIN "../roms/bm25.14l"

        CNOP 0,4
_c1943_gfx4: INCBIN "../roms/bm06.10a"     ; sprites 16x16 4bpp, 0x40000 total
             INCBIN "../roms/bm07.11a"     ;   (low plane pair = bm06..09)
             INCBIN "../roms/bm08.12a"     ;   (high plane pair = bm10..13)
             INCBIN "../roms/bm09.14a"
             INCBIN "../roms/bm10.10c"
             INCBIN "../roms/bm11.11c"
             INCBIN "../roms/bm12.12c"
             INCBIN "../roms/bm13.14c"

        CNOP 0,4
_c1943_tilerom: INCBIN "../roms/bm14.5f"   ; bg1 map @0x0000 (0x8000)
                INCBIN "../roms/bm23.8k"   ; bg2 map @0x8000 (0x8000)

        CNOP 0,4
_c1943_proms: INCBIN "../roms/bm1.12a"     ; 0x000 red
              INCBIN "../roms/bm2.13a"     ; 0x100 green
              INCBIN "../roms/bm3.14a"     ; 0x200 blue
              INCBIN "../roms/bm5.7f"      ; 0x300 char lut
              INCBIN "../roms/bm10.7l"     ; 0x400 bg1 lut
              INCBIN "../roms/bm9.6l"      ; 0x500 bg1 bank
              INCBIN "../roms/bm12.12m"    ; 0x600 bg2 lut
              INCBIN "../roms/bm11.12l"    ; 0x700 bg2 bank
              INCBIN "../roms/bm8.8c"      ; 0x800 sprite lut
              INCBIN "../roms/bm7.7c"      ; 0x900 sprite bank + priority
              INCBIN "../roms/bm4.12c"     ; 0xa00 (unused)
              INCBIN "../roms/bm6.4b"      ; 0xb00 (unused)
        CNOP 0,4
        END
