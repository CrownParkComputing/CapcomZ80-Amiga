; intro_mod.s -- embeds the WhittyDemo ProTracker module (the house tune).
        XDEF    _ai_default_mod, _ai_default_mod_end
        SECTION introdata,DATA
_ai_default_mod:
        incbin  "ptplayer/intro_default.mod"
        EVEN
_ai_default_mod_end:
