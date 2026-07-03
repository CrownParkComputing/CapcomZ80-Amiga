; src/hal/gunsmoke_loaderimg.s -- embed the Gun.Smoke loader intro image.
; Produced by tools/make_gs_loader_img.py into
; build/gunsmoke_loader/gunsmoke_loaderimg.bin (pass -I build/gunsmoke_loader to
; vasm). Parsed at runtime by src/hal/cgunsmoke_loader.c.
;   header: u16 w, u16 h, u16 nplanes, u16 ncolours
;   then   ncolours*u16 palette (12-bit 0x0RGB), then planar bitplane data.
;   pen 0 = BLACK canvas; the LAST palette entry (ncol-1) is the flashing prompt pen.
        XDEF    _gunsmoke_loaderimg

        SECTION data,DATA
        CNOP 0,4
_gunsmoke_loaderimg:
        incbin  "gunsmoke_loaderimg.bin"
        CNOP 0,4
        END
