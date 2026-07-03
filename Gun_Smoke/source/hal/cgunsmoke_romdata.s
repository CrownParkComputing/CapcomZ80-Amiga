; src/hal/cgunsmoke_romdata.s -- embed Gun.Smoke ROM/gfx/PROM blobs into the native
; Amiga executable (build/rgunsmoke/*.bin from tools/make_gunsmoke.py; vasm invoked
; with -I build/rgunsmoke so the incbin files resolve). Mirrors c1943_romdata.s.
        XDEF    _gunsmoke_rom_main, _gunsmoke_rom_chars, _gunsmoke_rom_tiles
        XDEF    _gunsmoke_rom_sprites, _gunsmoke_rom_bgmap, _gunsmoke_rom_proms
        XDEF    _gunsmoke_rom_snd

        SECTION data,DATA
        CNOP 0,4
_gunsmoke_rom_main:    incbin "main.bin"     ; maincpu: fixed 0..7fff + 4 banks at 0x8000 (0x18000)
        CNOP 0,4
_gunsmoke_rom_chars:   incbin "chars.bin"    ; gs01: 8x8 2bpp fg chars (0x4000)
        CNOP 0,4
_gunsmoke_rom_tiles:   incbin "tiles.bin"    ; gs06-13: 32x32 4bpp bg tiles (0x40000)
        CNOP 0,4
_gunsmoke_rom_sprites: incbin "sprites.bin"  ; gs15-22: 16x16 4bpp sprites (0x40000)
        CNOP 0,4
_gunsmoke_rom_bgmap:   incbin "bgmap.bin"    ; gs14: bg tilemap layout (0x8000)
        CNOP 0,4
_gunsmoke_rom_proms:   incbin "proms.bin"    ; RGB + LUT PROMs, shot layout (0xa00)
        CNOP 0,4
_gunsmoke_rom_snd:     incbin "snd.bin"      ; audio Z80 gs02.14h (0x8000)
        END
