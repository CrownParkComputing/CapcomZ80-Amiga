; c1943_native_aliases.s -- symbol-name bridge between Rust m68k ELF objects
; and Amiga C hunk objects. Rust emits/imports plain C names, while the Amiga
; C toolchain uses leading underscores.

        XDEF    _run
        XDEF    c1943_bank_select
        XDEF    c1943_soundlatch_write
        XDEF    c1943_sprite_dma
        XDEF    abort

        XREF    run
        XREF    _c1943_bank_select
        XREF    _c1943_soundlatch_write
        XREF    _c1943_sprite_dma
        XREF    _abort

        SECTION code,CODE

_run:
        jmp     run

c1943_bank_select:
        jmp     _c1943_bank_select

c1943_soundlatch_write:
        jmp     _c1943_soundlatch_write

c1943_sprite_dma:
        jmp     _c1943_sprite_dma

abort:
        jmp     _abort

        END
