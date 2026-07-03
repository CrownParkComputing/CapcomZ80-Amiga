; src/hal/gunsmoke_loadermod.s -- embed the Gun.Smoke loader's ProTracker .mod tune
; (the "sanxion" module, same one Terra Cresta / Pac-Land use). Staged by
; src/tools/build_gunsmoke_hw.sh into build/gunsmoke_loader/gunsmoke_loader.mod from
; assets/gunsmoke_loader.mod (pass -I build/gunsmoke_loader to vasm).
; cgunsmoke_loader.c copies it into CHIP RAM and hands it to ptplayer (mt_init).
        XDEF    _gunsmoke_loader_mod, _gunsmoke_loader_mod_end

        SECTION data,DATA
        CNOP 0,4
_gunsmoke_loader_mod:
        incbin  "gunsmoke_loader.mod"
        CNOP 0,4
_gunsmoke_loader_mod_end:
        END
