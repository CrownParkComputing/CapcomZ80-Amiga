; c1943_rtg_bezeldata.s -- RGB332 1943 Bezel Project backdrop (864x486).
        SECTION bezeldata,DATA
        XDEF    _c1943_rtg_bezel
        XDEF    _c1943_rtg_bezel_end
        CNOP 0,4
_c1943_rtg_bezel:
        INCBIN  "build/bezel/c1943_bezel_864x486.bin"
_c1943_rtg_bezel_end:
        CNOP 0,4
        END
