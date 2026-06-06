# Plan — artistic upgrade pass

Distilled from `clarifications.md` plus the follow-up discussion. This is the
working plan for the next batch of features. `initial_ideas.md` remains the
original phased spec; `CLAUDE.md` is the architecture map. Where this file and
those disagree about *intent for the new work*, this file wins.

The unifying goal: move the look from "early-90s 3D" toward deliberate pixel-art,
driven by a **constrained color palette + post-process**, richer surfaces, and
sun-based lighting.

## Decisions locked

- **Spheres stay analytic.** No icospheres / meshes. The "icosphere" wish was
  really about pole-pinching in the current UV texturing; we fix that with 3D
  object-space noise instead.
- **Ring color ramp is radial** (inner→outer radius), Saturn-style bands.
- **Atmosphere is faked** (limb glow from view+sun angle, sunset tint). No
  renderer transparency rework.
- **Background is a procedural, seeded starfield** with editable params (no HDR
  image for now).
- **Lighting: suns are the light sources.** Emissive bodies become lights;
  ambient fill stays. A directional "fill" light is kept as an option. Suns have
  **no distance falloff by default** (toggleable).
- **Palette post-process** runs on the final RGBA buffer and is **on by default**
  in both viewport and export. Palettes are **generated from base colors** and
  hand-overridable.
- **Dither is ordered (Bayer) by default**, with none/random selectable live.

## Open risk — temporal dither stability

Quantizing to a small palette causes banding; dithering hides it by speckling the
two nearest palette colors so the eye blends them. The hard part is *motion*:

- A **screen-locked** dither pattern is stable only when the screen is. With a
  moving camera the whole image slides under the fixed pattern → global low-level
  crawl. Moving bodies add a second, independent crawl.
- An **object/world-locked** pattern would move with bodies but not the camera,
  and is genuinely hard (every surface type needs a stable anchor coordinate).
- Blue-noise improves spatial quality, not the temporal problem.

**We do not solve this up front.** Stage 3 ships the live none/ordered/random
toggle so we can judge it *in motion*, then decide how far to chase stability.
The likely outcome is "a coherent amount of shimmer is on-style, stop there."

---

## Stages

Ordered for low coupling and early visible payoff. Each stage stands alone.

### Stage 1 — Ring color ramp
Radial gradient bands on disks/rings.

- `material.h`: add a radial ramp — a small `std::vector<{float pos; Vec3 color}>`
  of stops (pos in [0,1] across inner→outer). Empty = current solid behavior.
- `disk.cpp` (`Disk::hit`): compute radial param `t = (r - inner)/(outer - inner)`
  at the hit point, sample the ramp (lerp between bracketing stops), write albedo.
- `scene.cpp`: serialize the stops.
- `app.cpp`: ring properties panel — add/move/recolor stops.
- **Verify:** a ring with stops at 0/0.5/1 shows three distinct bands blending;
  JSON round-trips; solid rings (no stops) unchanged.

### Stage 2 — Procedural starfield background
Replace the constant near-black with generated stars.

- `render.cpp` (`background`): sample a seeded star function by ray direction.
  Params: density, brightness, size, color tint/variation, seed.
- `scene.h`: a `Background` struct holding those params (serialized).
- `app.cpp`: background panel with live params.
- **Verify:** stars appear, stable per seed, tunable; empty space between stays
  near-black. (Note: stars are composited *before* the palette pass — Stage 3.)

### Stage 3 — Palette post-process pipeline
The stylization core. A pass over the RGBA8 buffer, used by viewport + export.

Pipeline (per the discussion):
1. render (+ stars already composited)
2. blur (smoothing filter, adjustable radius)
3. quantize to nearest palette color, with dither (none / ordered-Bayer / random)
4. optional repeat (N iterations)

- New `post.h`/`post.cpp` in `src/`: palette type, blur, quantize+dither, the
  driver. Lives in `core` (no UI deps) so tests can cover it.
- Palette generation: from 1–2 base colors produce a harmonic ramp (hand-editable
  swatch list overrides/extends it).
- `scene.h`: a `PostProcess` struct (palette, blur radius, dither mode, repeat,
  enabled) — serialized. Enabled by default.
- `app.cpp`: post-process / palette panel — edit/generate swatches, toggle blur,
  dither mode dropdown, repeat count, **live in the viewport**.
- `render.cpp`: apply post after `render_view` (gated by `enabled`).
- **Verify:** a gradient sky quantizes to palette without hard banding under
  ordered dither; toggling dither modes live visibly differs; export matches
  viewport; disabling restores raw render. **Judge dither in motion here.**

### Stage 4 — Richer surface textures (fix pole pinching)
Replace UV sampling with 3D object-space noise; support multiple variables.

- `sphere.cpp` (`Sphere::hit`): sample noise at the **un-spun body-local hit
  point** (3D), not `atan2`/`asin` UVs — no poles, even detail, spin still
  animates it.
- `material.h`: extend pattern params — layered fbm (octaves), latitude banding
  (e.g. gas-giant bands), and a small set of color stops the noise mixes between.
  Keep it data-driven, not a pile of magic ints.
- `scene.cpp`: serialize the new fields.
- `app.cpp`: texture panel exposing the variables.
- **Verify:** a textured sphere shows even detail at the poles (no pinch); a
  banded preset reads as a gas giant; spin animates the surface; JSON round-trips.

### Stage 5 — Sun-based lighting + editor display modes
Emissive bodies drive lighting; viewport gets preview modes.

- `render.cpp` / `render.h`: gather lights from emissive bodies (positional,
  no falloff by default; falloff toggle). Keep an optional directional fill light.
  Loop lights for diffuse + shadow rays; ambient floor stays.
- `scene.h`: `light_dir` becomes the optional directional-fill light; add the
  sun-derived light list at pose time. Falloff flag.
- Editor **display modes** (viewport only; final render always = Lit):
  - **Direction** — current cheap single directional light + ambient.
  - **In-between** — one directional light auto-aimed from the main sun + ambient,
    **no shadows** (cheap sun-direction preview).
  - **Lit** — real lighting from emissive bodies with shadows (matches export).
- `app.cpp`: View menu toggle for the three modes.
- **Verify:** moving a sun changes shading direction in Lit mode; multiple suns
  combine; Direction mode matches today's look; In-between shows sun direction
  without shadow cost; export uses Lit.

### Stage 6 — Faked atmosphere
Per-planet limb glow. Depends on Stage 5 (needs sun direction).

- Per-body atmosphere params: color, thickness/extent, sunset-tint color,
  intensity. Stored on the body / material; serialized.
- `render.cpp`: additive limb term from view-grazing angle (rim brightens toward
  the silhouette) modulated by sun angle; lerp color toward the sunset tint where
  the sun grazes. No second ray / no transparency.
- `app.cpp`: atmosphere panel on a selected planet.
- **Verify:** a planet shows a soft lit rim that brightens on the sun side and
  tints toward sunset at the terminator; turning it off restores the bare planet.

---

## Notes / cross-cutting

- **Compositing order matters:** stars added before blur+quantize so they survive
  (a 1px star pre-blur becomes a soft dot, not erased; tune star size accordingly).
- **Palette crushes everything downstream** — ring gradients, atmosphere tints,
  lighting ramps all get quantized at output. That's intended: author in full
  color, quantize last. Don't over-invest in gradient precision.
- **Tests** (`tests.cpp`): cover ramp sampling, palette quantize/dither
  determinism, and noise continuity. UI stages are verified in the editor.
