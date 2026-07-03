/* src/hal/render_router.c -- generic render-router (see render_router.h).
 *
 * Pure decision + dispatch: a table lookup and two integer budget checks per object,
 * no allocation, no chipset code. The four rendering methods are realised by the
 * game's callbacks (built on hwscroll); this file only enforces the rule + caps. */
#include "render_router.h"

void rr_begin(const rr_config_t *cfg, rr_stats_t *st)
{
    (void)cfg;
    if (!st) return;
    for (int i = 0; i < RR_NMETHOD; i++) st->n[i] = 0;
    st->routed = 0;
}

rr_method_t rr_route(const rr_config_t *cfg, void *ctx,
                     const rr_object_t *o, rr_stats_t *st)
{
    if (!cfg || !o) return RR_DROP;
    if (st) st->routed++;

    /* 1) class -> method (out-of-range class defaults to a bob -- the safe default). */
    rr_method_t m = (o->cls >= 0 && o->cls < RR_NCLASS)
                  ? cfg->method_for_class[o->cls] : RR_BOB;

    /* 2) PLAYFIELD / POKED: no per-frame budget (they cost ~nothing vs a bob). A
     *    missing callback degrades to a bob so a partial config still draws. */
    if (m == RR_PLAYFIELD) {
        if (cfg->draw_playfield) { cfg->draw_playfield(ctx, o); goto done; }
        m = RR_BOB;
    } else if (m == RR_POKED) {
        if (cfg->draw_poked) { cfg->draw_poked(ctx, o); goto done; }
        m = RR_BOB;
    }

    /* 3) HW_SPRITE: try a hardware channel within the budget; on no-budget or a
     *    placement refusal (overlap / palette conflict / channel full), FALL BACK to
     *    a bob -- never silently dropped while bob budget remains (JOTD behaviour). */
    if (m == RR_HW_SPRITE) {
        int placed = 0;
        if (cfg->draw_hwsprite && st && st->n[RR_HW_SPRITE] < cfg->hwspr_cap)
            placed = cfg->draw_hwsprite(ctx, o);
        else if (cfg->draw_hwsprite && !st)
            placed = cfg->draw_hwsprite(ctx, o);
        if (placed) { m = RR_HW_SPRITE; goto done; }
        m = RR_BOB;
    }

    /* 4) BOB (default + every fallback): enforce the HARD bob cap. Big objects draw
     *    as a single WIDE bob (one wide blit, not several -- keeps the cap meaningful
     *    and matches the rule's "big = wide/32px-double bob, NOT attached sprite"). */
    {
        int used = st ? st->n[RR_BOB] : 0;
        if (cfg->draw_bob && used < cfg->bob_cap) {
            int wide = (o->w >= cfg->big_w) || (o->h >= cfg->big_h);
            cfg->draw_bob(ctx, o, wide);
            m = RR_BOB; goto done;
        }
        m = RR_DROP;          /* over the cap (or no bob callback) -> not drawn */
    }

done:
    if (st && m >= 0 && m < RR_NMETHOD) st->n[m]++;
    return m;
}
