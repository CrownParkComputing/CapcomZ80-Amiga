; cgunsmoke_rtg_bezeldata.s -- RGB332 Gun.Smoke Bezel Project backdrop.
        SECTION bezeldata,DATA
        XDEF    _gunsmoke_rtg_bezel
        XDEF    _gunsmoke_rtg_bezel_end

_gunsmoke_rtg_bezel:
        INCBIN  "gunsmoke_bezel_864x486.bin"
_gunsmoke_rtg_bezel_end:
        END
