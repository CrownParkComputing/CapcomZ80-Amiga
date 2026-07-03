* blktiger_romdata.s -- embedded Black Tiger ROMs (no-MCU bootleg set blktigerb1:
* protection patched out of the main code; all gfx/sound byte-identical to the parent).
* vasm INCBIN, paths relative to the build cwd (rtg_interp/). Region layout matches
* bt_load_maincpu()/bt_set_gfx(): maincpu 0x50000 (fixed@0, banks@0x10000), chars 0x8000,
* tiles 0x40000, sprites 0x40000, sound 0x8000.

        SECTION romdata,DATA

        XDEF    _blktiger_maincpu
        CNOP 0,4
_blktiger_maincpu:
        INCBIN  "../roms/btiger1.f6"     ; 0x00000 fixed code 0x0000-0x7fff (0x8000)
        DCB.B   $8000,0                  ; 0x08000 gap (unused) -- $ hex: vasm/phxass ignores 0x
        INCBIN  "../roms/bdu-02a.6e"     ; 0x10000 banks 0+1
        INCBIN  "../roms/btiger3.j6"     ; 0x20000 banks 2+3
        INCBIN  "../roms/bd-04.9e"       ; 0x30000 banks 4+5
        INCBIN  "../roms/bd-05.10e"      ; 0x40000 banks 6+7

        XDEF    _blktiger_chars
        CNOP 0,4
_blktiger_chars:
        INCBIN  "../roms/bd-15.2n"       ; 8x8 2bpp text, 0x8000

        XDEF    _blktiger_tiles
        CNOP 0,4
_blktiger_tiles:
        INCBIN  "../roms/bd-12.5b"       ; 16x16 4bpp BG tiles, 0x40000
        INCBIN  "../roms/bd-11.4b"
        INCBIN  "../roms/bd-14.9b"
        INCBIN  "../roms/bd-13.8b"

        XDEF    _blktiger_sprites
        CNOP 0,4
_blktiger_sprites:
        INCBIN  "../roms/bd-08.5a"       ; 16x16 4bpp sprites, 0x40000
        INCBIN  "../roms/bd-07.4a"
        INCBIN  "../roms/bd-10.9a"
        INCBIN  "../roms/bd-09.8a"

        XDEF    _blktiger_snd
        CNOP 0,4
_blktiger_snd:
        INCBIN  "../roms/bd-06.1l"       ; sound Z80, 0x8000 (audio wiring later)
