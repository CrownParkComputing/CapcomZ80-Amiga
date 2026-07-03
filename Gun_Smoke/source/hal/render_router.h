/* src/hal/render_router.h -- generic, game-agnostic Amiga AGA render-router.
 *
 * Implements the JOTD-proven render-routing rule (see memory
 * amiga-aga-render-routing-rule): every on-screen game object is routed, by a
 * STATIC per-game class->method table, to one of four Amiga rendering methods, and
 * the per-frame BOB budget is hard-capped so a busy scene can never starve the
 * background fill:
 *
 *   class               default method
 *   ------------------  --------------------------------------------------------
 *   background / tiles   PLAYFIELD  (hardware-scrolled bitplanes -- never a bob)
 *   text / HUD / score   PLAYFIELD  (the single biggest CPU win; bobbing the fg
 *                                    layer is what starves the bg fill)
 *   player + N critical  HW_SPRITE  (the scarce 4-8 sprite channels)
 *   enemy bullets        POKED      (bset straight into a bitplane)
 *   everything else      BOB        (bounded blitter bobs; big => WIDE bob)
 *
 * The router itself owns NO chipset code -- it only DECIDES + dispatches to the
 * game's draw callbacks, which are built on the shared hwscroll primitives. A new
 * game plugs in by supplying ONE rr_config_t (the class->method table, budgets, and
 * its four hwscroll-based draw callbacks) and tagging each live object with a class
 * (from the static per-code table the level-scan classifier emits). So 1943,
 * Commando, Pac-Land, Terra Cresta all share this module with only a small config.
 *
 * Minimal but real: fixed-length loops, no allocation, no per-frame heuristics --
 * the routing decision is a table lookup + budget check, exactly like JOTD's
 * asset-time-baked per-sprite-code routing. */
#ifndef RENDER_ROUTER_H
#define RENDER_ROUTER_H

/* the four Amiga rendering methods (+ DROP for the over-budget overflow). */
typedef enum {
    RR_PLAYFIELD = 0,   /* background / text / HUD / tiles -> bitplanes           */
    RR_HW_SPRITE,       /* player + designated critical movers -> hardware sprite */
    RR_POKED,           /* enemy bullets -> bset pixels into a bitplane           */
    RR_BOB,             /* everything else -> bounded blitter bob (wide if big)   */
    RR_DROP,            /* over the bob budget this frame -> not drawn            */
    RR_NMETHOD
} rr_method_t;

/* coarse SEMANTIC classes a game tags each object with (static, asset-time -- the
 * level-scan classifier emits a per-sprite-code table of these). The class -> method
 * mapping lives in the per-game config, so a game can, e.g., route its bullets to
 * bobs instead of poked pixels just by changing its table -- without touching this
 * module. */
typedef enum {
    RR_CLS_BACKGROUND = 0,  /* scroll-locked scenery / tiles                      */
    RR_CLS_TEXT,            /* HUD / score / in-game text (the fg/char layer)     */
    RR_CLS_PLAYER,          /* the player ship + 1-2 designated critical movers   */
    RR_CLS_BULLET,          /* small enemy shots / grenades                       */
    RR_CLS_ENEMY,           /* enemies, big objects, explosions, score popups     */
    RR_NCLASS
} rr_class_t;

/* one live on-screen object the game hands to the router. Positions are screen
 * pixels; `code` is the sprite/tile code (the classification key + the draw key);
 * (w,h) drive big-object (wide-bob) detection; `cls` is the object's class;
 * `palid`/`flip`/`cf`/`user` are opaque pass-throughs the draw callbacks need. */
typedef struct {
    int x, y;           /* screen position (top-left)                             */
    int code;           /* sprite/tile code                                       */
    int w, h;           /* pixel size (for big-object / wide-bob detection)       */
    int cls;            /* rr_class_t                                             */
    int palid;          /* palette id (hw-sprite palette-conflict + bob colour)   */
    int cf, flip;       /* colour-full / flip attribute pass-through              */
    void *user;         /* opaque caller pointer                                  */
} rr_object_t;

/* per-frame routing tally (diagnostics + the bob-cap proof). */
typedef struct {
    int n[RR_NMETHOD];  /* count routed to each method (n[RR_DROP] = overflow)    */
    int routed;         /* total objects seen                                     */
} rr_stats_t;

/* the game's four draw callbacks, built on the shared hwscroll primitives. `ctx` is
 * the game's opaque renderer state (passed straight through). draw_hwsprite returns
 * 1 if it actually placed the object in a hardware-sprite channel, 0 if it could not
 * (budget/overlap/palette conflict) -- the router then FALLS BACK to a bob (matches
 * JOTD: hw-sprite overflow silently degrades to a bob, never dropped if budget
 * remains). draw_bob's `wide` flag is set for big objects. Any callback may be NULL;
 * a NULL playfield/poked callback routes that object to a bob instead. */
typedef struct {
    /* class -> method routing table (indexed by rr_class_t). */
    rr_method_t method_for_class[RR_NCLASS];

    int bob_cap;        /* HARD cap on bobs per frame (e.g. 64)                    */
    int hwspr_cap;      /* hardware-sprite placements per frame (e.g. 8)           */
    int big_w, big_h;   /* w>=big_w || h>=big_h  => "big object" -> wide bob       */

    void (*draw_playfield)(void *ctx, const rr_object_t *o);
    int  (*draw_hwsprite )(void *ctx, const rr_object_t *o); /* 1=placed, 0=fallback */
    void (*draw_poked    )(void *ctx, const rr_object_t *o);
    void (*draw_bob      )(void *ctx, const rr_object_t *o, int wide);
} rr_config_t;

/* reset per-frame budgets/stats. Call once before routing this frame's objects. */
void rr_begin(const rr_config_t *cfg, rr_stats_t *st);

/* route + DRAW one object: look up its method, enforce the budgets, dispatch to the
 * matching callback (hw-sprite overflow -> bob; bob over cap -> DROP). Returns the
 * method actually used. */
rr_method_t rr_route(const rr_config_t *cfg, void *ctx,
                     const rr_object_t *o, rr_stats_t *st);

#endif /* RENDER_ROUTER_H */
