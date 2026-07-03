# 02 - render_router migration (Capcom dual-Z80 family profile)

Goal: bring 1942 onto the shared `src/hal/render_router.{c,h}` routing layer with the
Capcom-Z80 family profile, WITHOUT regressing the proven render.

## Key fact that shapes the migration
1942's renderer (`src/hal/c1942_render.c`) is a **software ROT90 compositor**:
`composite()` writes the 8-bit indirect pen of every visible pixel straight into a
chunky buffer (bg fill -> sprites -> fg on top), then `chunky_to_planes()` C2Ps to
the 8 AGA bitplanes. It does **not** use the shared `hwscroll.c` primitives, has
**no** Amiga hardware-sprite / bob / poke chipset path, and does **not**
double-buffer hardware sprites.

Consequence: the `hwscroll-shared-engine-gotcha` (the Commando aspr flicker from the
shared double-buffered hwscroll engine) **cannot affect 1942** - there is no shared
hwscroll usage and no hardware sprites to flicker. No private engine copy was needed.

## Family profile (the rule, applied to 1942)
| object class            | method     | 1942 realisation |
|-------------------------|------------|------------------|
| bg tiles                | PLAYFIELD  | composited bg fill (hardware-scrolled by scroll[0..1]) |
| fg chars / HUD / score  | PLAYFIELD  | fg char layer, already drawn WITHOUT scrollx = a STATIC field |
| player plane            | HW_SPRITE  | (config wired; see classifier note) |
| bullets                 | POKED      | (config wired) |
| enemies/bosses/explosions | BOB      | sprites, bounded by bob_cap |

**HUD-in-scroll-area failure mode does NOT arise here.** The user's standing rule is:
if the score/HUD is rendered inside the scrolling area, move it to a static field and
shrink the play area one tile-row top+bottom. 1942's fg char layer is *already* drawn
with no scrollx (a static overlay on top of the scrolled bg), so the HUD is already a
static field. No play-area shrink was needed. (Documented so a future reviewer does
not "fix" a non-problem.)

## Integration (src/hal/c1942_render.c)
- Added `#include "render_router.h"`.
- Built `c1942_rr_cfg` (the class->method table above) with `bob_cap=64`,
  `hwspr_cap=8`, `big_w/h=32`.
- The sprite loop in `composite()` now: `rr_begin()`, then for each of the 32
  spriteram entries builds an `rr_object_t` and calls `rr_route()`. Reverse order
  (s=31..0) preserved -> 1942 priority unchanged.
- Four callbacks (`c1942_rr_hwsprite/poked/bob`) all funnel onto ONE pixel
  primitive `draw_sprite_obj()` = the exact old inner sprite loop. bg + fg layers are
  composited directly as PLAYFIELD (config `draw_playfield=0`, handled inline).

Because every routing method realises onto the same compositor primitive, the routed
render is **bit-for-bit identical** to the pre-router renderer (proven in step 04),
while the router does real work: family-profile routing decision + the hard per-frame
bob cap (32 objects < 64 cap -> never drops; a busy scene can never starve the bg
fill). This is the "working > architecture" safety the Commando lesson demands.

## Classifier note (deferred refinement, not a regression)
There is no level-scan classifier table for 1942 yet, so every live sprite is tagged
`RR_CLS_ENEMY` (-> BOB). The class->method TABLE already encodes PLAYER->HW_SPRITE and
BULLET->POKED; refining the per-sprite-code tags (so the player plane goes to a
hardware-sprite channel and bullets get poked) is a follow-up that plugs into the
existing `levelscan-classification-tool` pipeline. With the single shared pixel path,
mis-tagging is visually inert today, so this is safe to defer.
