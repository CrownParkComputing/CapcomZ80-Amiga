; src/hal/c1942_romdata.s -- embed 1942 ROM/gfx/PROM blobs (build/r1942/*.bin,
; from tools/make_1942rom.py; pass -I build/r1942 to vasm). C-visible symbols.
        XDEF    _c1942_rom_main, _c1942_rom_g1, _c1942_rom_g2, _c1942_rom_g3
        XDEF    _c1942_rom_pr, _c1942_rom_pg, _c1942_rom_pb
        XDEF    _c1942_rom_lchr, _c1942_rom_ltile, _c1942_rom_lspr
        XDEF    _c1942_rom_snd

        SECTION data,DATA
        CNOP 0,4
_c1942_rom_main:  incbin "main.bin"
        CNOP 0,4
_c1942_rom_g1:    incbin "g1.bin"
        CNOP 0,4
_c1942_rom_g2:    incbin "g2.bin"
        CNOP 0,4
_c1942_rom_g3:    incbin "g3.bin"
        CNOP 0,4
_c1942_rom_pr:    incbin "pr.bin"
        CNOP 0,4
_c1942_rom_pg:    incbin "pg.bin"
        CNOP 0,4
_c1942_rom_pb:    incbin "pb.bin"
        CNOP 0,4
_c1942_rom_lchr:  incbin "lchr.bin"
        CNOP 0,4
_c1942_rom_ltile: incbin "ltile.bin"
        CNOP 0,4
_c1942_rom_lspr:  incbin "lspr.bin"
        CNOP 0,4
_c1942_rom_snd:   incbin "snd.bin"
        END
