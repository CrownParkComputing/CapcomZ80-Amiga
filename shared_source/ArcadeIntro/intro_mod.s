; intro_mod.s -- embeds the default ArcadeIntro ProTracker module.
; A game may instead embed its own .mod and pass it to ai_config; this just
; provides a ready-to-use default (ai_default_mod / ai_default_mod_end).
        XDEF    _ai_default_mod, _ai_default_mod_end
        SECTION introdata,DATA
_ai_default_mod:
        incbin  "intro_default.mod"
        EVEN
_ai_default_mod_end:
