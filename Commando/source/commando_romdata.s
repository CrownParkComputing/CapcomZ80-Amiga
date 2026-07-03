        XDEF    _commando_rom_main
        XDEF    _commando_rom_g1
        XDEF    _commando_rom_g2
        XDEF    _commando_rom_g3
        XDEF    _commando_rom_proms
        XDEF    _commando_rom_snd

        SECTION data,DATA
_commando_rom_main:     incbin  "main.bin"
_commando_rom_g1:       incbin  "g1.bin"
_commando_rom_g2:       incbin  "g2.bin"
_commando_rom_g3:       incbin  "g3.bin"
_commando_rom_proms:    incbin  "proms.bin"
_commando_rom_snd:      incbin  "snd.bin"
