        SECTION romdata,DATA

        XDEF    _sidearms_maincpu
        CNOP 0,4
_sidearms_maincpu:
        INCBIN  "../roms/sa03.bin"
        INCBIN  "../roms/a_14e.rom"
        INCBIN  "../roms/a_12e.rom"
        DCB.B   $10000,0

        XDEF    _sidearms_chars
        CNOP 0,4
_sidearms_chars:
        INCBIN  "../roms/a_10j.rom"

        XDEF    _sidearms_tiles
        CNOP 0,4
_sidearms_tiles:
        INCBIN  "../roms/b_13d.rom"
        INCBIN  "../roms/b_13e.rom"
        INCBIN  "../roms/b_13f.rom"
        INCBIN  "../roms/b_13g.rom"
        INCBIN  "../roms/b_14d.rom"
        INCBIN  "../roms/b_14e.rom"
        INCBIN  "../roms/b_14f.rom"
        INCBIN  "../roms/b_14g.rom"

        XDEF    _sidearms_sprites
        CNOP 0,4
_sidearms_sprites:
        INCBIN  "../roms/b_11b.rom"
        INCBIN  "../roms/b_13b.rom"
        INCBIN  "../roms/b_11a.rom"
        INCBIN  "../roms/b_13a.rom"
        INCBIN  "../roms/b_12b.rom"
        INCBIN  "../roms/b_14b.rom"
        INCBIN  "../roms/b_12a.rom"
        INCBIN  "../roms/b_14a.rom"

        XDEF    _sidearms_tilemap
        CNOP 0,4
_sidearms_tilemap:
        INCBIN  "../roms/b_03d.rom"

        XDEF    _sidearms_snd
        CNOP 0,4
_sidearms_snd:
        INCBIN  "../roms/a_04k.rom"
