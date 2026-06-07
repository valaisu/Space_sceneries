# Space Sceneries

A from-scratch C++ raytracer and **Blender-style scene editor** for composing
simplified, pixel-art space scenes (planets, moons, rings, a sun) and exporting
them as PNGs. Spheres and disks are intersected mathematically; **emissive bodies
(suns) are the lights** (positional, ambient fill, optional directional fill) with
hard shadows, and a **palette post-process** crushes the final image toward
deliberate pixel-art color.

This file is the coarse, up-to-date map of the project.

## Build & run

GLFW and OpenGL come from the system; `nlohmann/json`, Dear ImGui, and
`stb_image_write` are vendored in `third_party/`.

```bash
cmake -S . -B build        # first time / after CMakeLists or file-list changes
cmake --build build -j     # after editing code
./build/space_sceneries          # the editor (needs a display)
./build/space_sceneries_tests    # headless sanity checks
```

The editor keeps files in project subfolders (created on startup): saved scenes in
`scenes/*.json`, saved palettes in `palettes/*.json`, exported PNGs in `renders/*.png`.
Only `scenes/default.json` (the base scene loaded on launch) is tracked; the rest is
gitignored. The editor needs a display; the tests do not.

The build defaults to **`Release` (`-O3`)** when no `CMAKE_BUILD_TYPE` is set — this
is a CPU raytracer and `-O0` is ~10× slower. CMake also `find_package`s **OpenMP** and
links it to `core` if present, parallelizing the render loop across cores. For a debug
build, configure a separate tree explicitly: `-DCMAKE_BUILD_TYPE=Debug` (slower).

## Layout

- `src/` — all first-party code.
- `third_party/` — vendored single-header / drop-in deps (nlohmann json, imgui, stb).
- `build/` — CMake output (gitignored).

### Source-file map (`src/`)

Core math / geometry (header-only unless noted):
- `vec3.h` — `Vec3` + dot/cross/normalize/length + `rotate_about` (Rodrigues).
- `ray.h` — `Ray`.
- `material.h` — `Material` (albedo + emissive + `two_sided` + 3D procedural texture fields incl. `band_var` + `band_drift` + ring/tex `ColorStop` ramps + `Terrain` + `Craters` + `Atmosphere` + `Clouds`), `ColorStop` (carries per-stop `alpha`) + `sample_color_ramp` (optional alpha out-param), `Terrain` (rocky ocean/land + seasonal polar caps), `Craters` (cellular impact bowls for airless bodies), `Atmosphere`, `Clouds` (incl. `drift`).
- `hittable.h` — `Hittable` base (virtual `hit` + `clone`, plus `center` and `name`), `HitRecord`.
- `sphere.h` / `sphere.cpp`, `disk.h` / `disk.cpp` — the two primitives + ray intersection. `sphere.cpp` also has `surface_field`/`surface_color` (3D object-space texture), `terrain_color` (rocky ocean/land + caps), `crater_shade` (cellular impact bowls), and composites the translucent **cloud layer** over the base surface; `disk.cpp` samples the radial ring ramp.
- `camera.h` — LookAt `Camera`: `get_ray` (rays), `project` (world→screen, the exact inverse, used for overlays), and `right`/`up`/`forward` basis accessors (used by viewport transforms).
- `motion.h` — `Orbit`, `Spin`, `Body` (a shape + its motion).
- `render.h` / `render.cpp` — `Light`, `ShadeMode`, `Background` (starfield + density regions); shading (`ray_color`: ambient + summed per-light diffuse + hard shadows; `atmosphere_glow`; `background`); `hit_world`; `render_view` / `render_scene` (scene → RGBA8, then `apply_post`); `render_material_preview` / `render_background_preview` (neutral-light editor thumbnails, no post).
- `post.h` / `post.cpp` — `PostProcess`, `DitherMode`; `generate_palette` (HSV harmonic palettes; a `scheme` selector — `Anchors` line (0/1/2/3 anchor colors + `spread` hue-fan) or rule-based `Monochromatic`/`Complementary`/`Triadic`/`Analogous` ramped dark→light — plus a `base_hue` rotation + seeded `randomness`) + `apply_post` (blur → quantize/dither over the RGBA8 buffer). In `core`, no UI deps.
- `scene.h` / `scene.cpp` — `Scene`, `SceneCamera` (+ `CamLook`), JSON save/load, posing (`body_world_pos`, `world_at_time`, `orbit_ring_point`, `camera_eye`, `scene_camera`), and the **random system generator** (`SystemGenParams` + `generate_system`, plus `generate_eclipse_system` — the infinite-loop variant that fits the foreground planet + camera into a fixed eclipse frame).

Executables:
- `app.cpp` — the editor (ImGui + GLFW + OpenGL). **GUI-only.**
- `tests.cpp` — headless assertions; built as `space_sceneries_tests`.
- `stb_image_write_impl.cpp` — compiles the stb implementation (GUI target only).

### CMake targets

- `core` (static lib) — everything except the UI; depends only on vendored headers.
- `space_sceneries_tests` — links `core`.
- `imgui` (static lib) + `space_sceneries` — the editor; links `core`, `imgui`, GLFW, OpenGL.

## Architecture

**Scene model.** A `Scene` holds `std::vector<Body>`, a `SceneCamera`, the
directional fill `light_dir` (+ `fill_light`/`light_falloff` flags), a `Background`
(starfield), a `PostProcess` (palette), and the output `width`/`height`. A `Body` is
a `shape` (`Sphere`/`Disk`) plus an `Orbit` and a `Spin`. There are **no separate
Planet/Moon types** — those are just spheres with a name and material. The `Disk`
primitive doubles as a **ring** (annulus via `inner_radius`); the editor's Add menu
exposes it as "Ring".

**Camera as an object.** The render camera is a `SceneCamera`: a movable, orbitable
scene object (not just authoring params). It has a `position` *or* an `Orbit`
(reusing the body machinery, so it can circle a parent body), an FOV, a `vup`, and a
`CamLook` policy deciding where it aims — `Direction` (a constant world facing),
`Target` (track a body, or the world origin), or `Spin` (rotate at a constant rate
about `vup`). `scene_camera(scene, t, aspect)` poses it at time `t` (resolving orbit
+ look mode) into a built `Camera`; `camera_eye(scene, t)` gives just the eye. It is
drawn as a frustum gizmo in the editor but **never raytraced**.

**Motion (time-based posing).** Orbits are **Kepler ellipses**: a body orbits a parent
body (or the world origin) in the plane defined by `Orbit::normal`, with semi-major axis
(`radius`) / period / phase (mean-anomaly offset) / `eccentricity`. The parent sits at a
focus; `orbit_offset` advances the mean anomaly uniformly and Newton-solves the eccentric
anomaly, so the body moves faster at periapsis (equal-area). `eccentricity == 0` collapses
to the old uniform circle, so existing scenes are unchanged. `orbit_offset_at(o, E)` maps an
eccentric anomaly to an in-plane offset and is reused to draw the elliptical orbit path.
`body_world_pos(scene, i, t)` resolves a body's world position through its
parent chain at time `t`; `world_at_time(scene, t)` returns a posed snapshot
(clones each shape with its computed `center`) which is what the raytracer renders.
`world_at_time` also bakes each sphere's spin orientation (`spin_axis`/`spin_angle`,
from `Spin` + `t`) into the posed clone, plus the time-driven texture state
(`cloud_angle` for cloud drift, `season_swing` for seasonal polar caps).

**Textures (Phase 9 → Stage 4).** A sphere `Material` with `pattern != 0` carries a
**3D object-space** procedural texture: layered fbm noise (`noise_scale`,
`noise_octaves`) optionally blended toward warped latitude bands (`band_strength`,
`band_freq`, `warp`) for gas giants, mapped through `tex_ramp` (or `albedo`↔`detail`
when empty). `band_var` wobbles the band phase with latitude so gas-giant stripe
**widths vary** instead of forming an even comb. `Sphere::hit` samples it at the
**un-spun body-local point** (no pole pinch, no seam), so a spinning textured sphere
visibly rotates. `surface_field` (scalar) / `surface_color` (albedo) are split out and
unit-tested.

**Gas-giant belts & storms.** To avoid wavy "mush" giants, the band path builds a
discrete-belt structure instead of a symmetric sine (which alternated two ramp ends):
it makes a **belt coordinate** from latitude, where `band_var` distorts the
latitude→belt spacing so belt **widths vary** (steeper local slope = narrower belt);
`floor` of that coordinate is the **belt index**, and each belt samples its **own
position along `tex_ramp`** via a per-belt `hash11`, so consecutive belts are distinct
colors drawn from the whole ramp ("the line") rather than an A/B/A/B alternation.
`band_levels` (>1) snaps belts to that many distinct ramp positions (0 = each belt
anywhere on the line). `turbulence` adds high-freq fbm **stretched east–west** (latitude
compressed ×3) to the belt coordinate, wiggling the **belt boundaries** into filaments;
a small `n` noise term survives at high `band_strength` for interior texture. `storm`
(>0) overlays a few **oval vortices** via `storm_weight` (declared in sphere.h,
unit-tested): a guaranteed primary "great red spot" plus hash-placed (`storm_seed`)
smaller ovals that appear as `storm`→1, each elongated east–west, blended toward
`storm_color` in `Sphere::hit` after the band albedo. The generator gives gas giants
high `band_strength`, varied `band_var` widths, `turbulence`, calmer `warp`, usually a
3-stop ramp (the line), free belt colors (`band_levels` mostly 0), and storms on ~1/3
(the primary "great red spot" is enlarged and mid-latitude so it reads clearly).

**Terrain (rocky planets).** A sphere `Material` may carry a `Terrain` (gated by
`pattern != 0`, replacing the scalar field→ramp path for the base albedo). It reuses
the fbm field as **elevation**: below `sea_level` is **ocean** (the `ocean` color,
darkened toward 0 elevation for depth), above is **land** colored by `tex_ramp`
sampled by land height (low→high → lowland→mountain). `levels` **posterizes** the land
(and ocean depth) into that many flat steps, so colored regions get **crisp boundaries**
instead of a smooth gradient (0 = smooth); the generator pairs `levels=3` with a 3-color
biome ramp so each band is a distinct region — the fix for "uniform mush" planets.
**Polar ice caps** blend toward
`cap_color` where `|latitude|` exceeds `cap`, with a noise-raggedized rim. The cap edge
**swings with the seasons**: `cap_season` × sin(time/`season_period`) is baked into the
sphere's `season_swing` at pose time, so caps slowly grow/shrink while the timeline
plays. `terrain_color` is split out and unit-tested. Gas giants keep using bands (no
terrain); the generator gives rocky planets/moons terrain, water worlds vs. dry/desert.

**Clouds.** A sphere `Material` may carry a `Clouds` layer — a *second* translucent
texture composited over the base surface **at the same hit point** (no extra ray).
`Sphere::hit` samples an independent fbm field (`scale`/`octaves`, biased by
`coverage`) at the same un-spun body-local point, maps it through the cloud
`ColorStop` ramp to a color + alpha (empty ramp = white, alpha = field), scales the
alpha by `opacity`, and alpha-blends it onto the computed albedo. So clouds rotate
with the planet and get lit like the surface. `drift` lets clouds rotate at a slightly
different rate than the surface (baked as the sphere's `cloud_angle = spin_angle +
2π·t·drift` at pose time), so the cloud deck slowly slides over the planet while the
timeline plays. Per-stop `alpha` on `ColorStop` is the shared translucency primitive
(also intended for see-through ring gaps).

**Craters.** A sphere `Material` may carry a `Craters` layer (airless rocky bodies). In
`Sphere::hit`, `crater_shade` evaluates **cellular (Worley) noise** at the same un-spun
body-local point: over the 3×3×3 cell neighbourhood it finds the nearest jittered feature
point that holds a crater (`density` sets cell frequency, ~45% of cells are empty), and
inside that crater's radius shades a **bowl** — a darkened floor with a thin bright raised
**rim** — returning a multiplier (~0.2–1.4) applied to the base albedo. Unit-tested
(`crater_shade`: 1 when disabled, floors <1 when enabled). The generator gives airless
rocky planets + every moon craters (gated by `crater_chance`); worlds with an atmosphere
keep a smooth surface.

**Band drift.** A gas giant's `band_drift` (revs/time) bakes a `band_angle = spin_angle +
2π·t·band_drift` at pose time (mirroring cloud drift). `Sphere::hit` samples the band path
(`surface_field` + `storm_weight`) at that drifting angle instead of the plain spin angle,
so belts, zonal filaments and storms slowly **slide** while the surface still spins — cheap
(one extra `rotate_about` per gas-giant pixel, skipped when `band_drift == 0`). Terrain
(rocky) bodies are unaffected.

**Lighting (Stage 5).** Lights are gathered at pose time. In the final/`Lit` render,
each **emissive sphere is a positional light** (color = its albedo, optional
inverse-square `light_falloff`), plus an optional directional fill (`light_dir`,
gated by `fill_light`; also the fallback when there are no suns). `ray_color` sums an
ambient floor + per-light diffuse, shadow-testing each (emissive bodies are
**transparent to shadow rays**, so suns don't shadow themselves). A `two_sided`
material (set on rings) is lit when the sun hits **either** face (`fabs` of the
diffuse term, shadow offset flipped to the lit side), so a back-lit ring reads as
translucent instead of going black. Editor-only
`ShadeMode` previews: `Direction` (fixed directional + shadows = the old look),
`InBetween` (one directional auto-aimed from the main sun, no shadows), `Lit`
(matches export). `render_scene` always uses `Lit`.

**Background (Stage 2).** Misses return a seeded **starfield**: `background(r, bg, time)`
hashes a 3D grid sampled by ray direction (no pole pinch), one star per cell with size /
brightness / tint-variation / density / seed from `Scene::background`. Stars are
sampled over the **3×3×3 neighborhood** with full-cell jitter (round points, no
visible grid rows), and a low-frequency `fbm` field carves **density regions**
(`region_scale` / `region_strength`, plus an additive `region_glow` haze) so the sky
has darker and denser areas; `region_star_boost` makes those regions also hold **more,
brighter stars** (the "nebula" look). A **galactic band** (`band_*`) is a non-noise
pattern — the great-circle equator of `band_normal` — that concentrates stars + a tinted
haze along a Milky-Way-like band. **Shooting stars** (`shoot_*`) are time-driven: a few
deterministic streaks sweep great-circle arcs with fading tails, so they only move while
the timeline plays (this is why `background`/`ray_color` take a `time` arg; previews pass 0).

**Atmosphere (Stage 6).** A sphere `Material` may carry an `Atmosphere` (day `color`,
`sunset` tint, `thickness`, `intensity`). `atmosphere_glow` adds a Fresnel-like limb
term (bright at the silhouette), gated to the sun-lit side and tinted toward `sunset`
near the terminator. Purely additive — **no transparency, no second ray.**

**System generation.** `generate_system(scene, SystemGenParams)` (in `core`/scene.cpp)
replaces `scene.bodies` with a random system and reframes `scene.cam` — it sets the
render camera to **orbit a planet** and `Target`-track it (which planet is picked by
`hero_kind`: 0 prefers a gas giant — the default — 1 a rocky/terrain world, 2 forces a
gas giant; falls back to any planet), so a
generated system opens on a moving close-up rather than a static wide shot — leaving
background/palette/resolution alone. Real systems are loose inspiration: a central
emissive **sun** (body 0), then planets on **geometrically growing orbits** (each ≈
`spacing` × the previous radius, +jitter; Kepler-ish `period ∝ R^1.5`), inner ones small
& **rocky** (ocean/land **terrain** — water worlds with blue seas + green land, or
dry/desert worlds; a **3-color biome ramp posterized into flat bands** so regions read
crisply; ice caps vary from none through small to a frozen world, sometimes seasonal,
with **varied cap tints** (white-blue / warm dust / pale cyan / alien); **airless** rocky
worlds (no atmosphere) get **impact craters**; some get **drifting clouds**, not always
white; ~30% are **exotic**, an HSV hue family
shifted off Earth-like greens/blues), outer ones likelier **gas giants**
(discrete belts of varied widths, each a distinct color sampled along the ramp, with
zonal filaments + slowly **drifting bands** (`band_drift`) + ~1/3 an oval storm, sometimes
a ring — now a **multi-band radial color ramp** (`ring_colors`) — + atmosphere). The sun
itself sometimes gets **faint granulation/banding** (`sun_texture_chance`). The overall
rocky↔gas split is set by `gas_ratio` (a mild outward tilt keeps inner planets rockier).
Moons are mottled + cratered, some with polar caps. Every planet **spins** (period 3–10s) about a
**randomly tilted axis** (small, ~up to 22°), with a rare **~8% Uranus-like extreme tilt
(~90°, spins on its side)** — and an extreme-tilt world **drops its polar caps** (the poles
no longer face away from the sun). Orbits are near-coplanar (small `inclination` tilt) with optional
`eccentricity`. **Moons stay bubbled:** a planet's moon orbit is capped at `0.4 ×` the
*worst-case* clearance to either neighbouring orbit (computed from their perihelion/
aphelion edges) — strictly less than half the gap — so a moon is always closer to its
parent than to any other planet or the sun. That invariant is unit-tested (`system_gen`
in tests.cpp). Moons also start outside the planet's radius and any ring. The gen
params (`SystemGenParams`) are editor authoring controls, not part of `Scene`, but they
**are** persisted: save/"Set as default" writes them (plus the eclipse-loop knobs and a few
view prefs) under the scene JSON's optional **`editor`** section via the `EditorSettings`
overloads of `scene_to_json`/`save_scene`/`load_scene` (older files lacking the section fall
back to defaults). The editor packs/unpacks `EditorSettings` around save/load (`app.cpp`).

**Infinite eclipse loop.** `generate_eclipse_system(scene, SystemGenParams, scene_seconds,
cam_loops)` (scene.cpp) builds a normal system via `generate_system` — with the params
**biased toward more spacing + tilt** and the **inner planets shrunk** (0.6×→1.0× by orbit
index) so fewer bodies crowd the view and read as fully dark — then **fits the foreground
planet + render camera into a fixed canonical eclipse frame**: the planet is forced
circular/coplanar (`e=0`, `normal=+Y`, `phase=π`) so at `t=0` it sits at `(0,0,-R)`; the
camera orbits it in the same plane (`phase=π`, `radius=6·r_planet`, `fov=50`), so at `t=0`
it looks **+Z** at the planet with the sun directly behind it (an eclipse), and the sun
radius is clamped to stay hidden. **Each cycle lasts exactly `scene_seconds`** (so the
apparent motion speed is constant across scenes regardless of orbit sizes): the camera
makes `cam_loops` revolutions per cycle (`period = T/cam_loops`) and the planet does
`cam_loops-1` orbits (`period = T/(cam_loops-1)`), so camera & anti-sun realign **exactly
once per cycle** (one eclipse), with a full reveal at mid-cycle. `cam_loops == 1` parks the
planet (`(0,0,-R)`, orbit off) for the calmest, pure-camera sweep. The **close-up hero is a
random type each cycle** (`hero_kind` chosen 50/50: rocky/ocean world vs gas giant), so the
loop showcases terrain + caps + a backlit-atmosphere crescent on some cycles and a banded
giant on others, with no predictable A-B-A-B pattern. Moons aren't eligible heroes — the
eclipse frame needs a **sun-orbiting** hero (`R` = its solar orbit). **Every *other*
sun-orbiting planet is hidden at the cut:** it's forced coplanar (`normal=+Y`, `phase=0`)
and re-timed to an **integer number of orbits per cycle**, so at `t=0` (and again at `t=T`)
it sits on the `+Z` axis directly **behind the hero** — out of frame at the eclipse — then
swings briefly into view off to the side mid-cycle; each is also shrunk `0.7×` so it never
reads as large as the sun (the "stray planet next to the sun" complaint). Two more loop-only
tweaks keep the vibe calm: the spacing floor is raised (neighbours sit farther apart in
frame) and **every moon is re-timed to ≥ one cycle per orbit** (`generate_system`'s short
Keplerian moon periods otherwise whir around many times per scene). Because the camera
orientation + FOV are identical every eclipse and the starfield is hashed by ray
*direction* only, the cut is **seamless** (no star/framing jump). The editor's "Infinite
mode" (app.cpp) drives this: while playing it wraps `time` at `scene_seconds`, calls
`generate_eclipse_system`, and **morphs the palette** between cycles (lerp old→new across
the cycle, or Snap at the eclipse) so the colors — incl. the starfield — drift as a
"something changed" cue. `scene_seconds`/`cam_loops`/transition are editor controls.

**Rendering & post (Stage 3).** `render_view` shoots one primary ray per pixel, shades
the nearest hit, composites stars on misses, then runs `apply_post`. Its per-pixel
loop is **OpenMP-parallel over rows** (`#pragma omp parallel for`; read-only over the
posed `world`/`lights`, a no-op if OpenMP is absent). `apply_post`: blur →
quantize each pixel to the nearest `PostProcess::palette` color with dithering
(`None` / ordered Bayer / random), optionally repeated. Used by **both** viewport and
export so they match; gated by `PostProcess::enabled`. Palettes are generated by
`generate_palette` (HSV ramp) and hand-editable. A **`scheme`** selector picks the
generation strategy. **`Anchors` (line, scheme 0)** is the classic mode: interpolate a
path through **0, 1, 2, or 3** base colors (`anchor_count`) — the **3-anchor** mode bends
the ramp through `base_a → base_b → base_c`, so it can span three distinct hues
(blue+green+orange) instead of a single line, and a `spread` slider fans the
**two-anchor** ramp across a wider hue arc (0 = straight line; higher = sweeps more of
the wheel; ignored at 3 anchors so hand-picked hues stay exact). The **rule-based
schemes** — **`Monochromatic`** (one hue), **`Complementary`** (two opposite),
**`Triadic`** (three evenly spaced), **`Analogous`** (three neighbors) — take their
hue(s) from **`base_a`'s hue** by a color-wheel rule and give **each hue its own
dark→light ramp** (shadows slightly more saturated, highlights desaturated), so the
palette always spans brightness — guaranteeing a dark swatch for the starfield
background regardless of how bright the base color is (this is why bright anchors used
to yield an ugly washed-out background). All schemes are then rotated bodily by a
`base_hue` slider (recenters on any hue), with a seeded `randomness` jitter and a
`palette_seed`. "Randomize" bumps the seed **and** walks `base_hue` by the golden
ratio, so it produces a genuinely different palette in every anchor mode (a 2-anchor
ramp with `randomness == 0` is otherwise fully determined and wouldn't change). Output is RGBA8, one
`uint32_t` per pixel, **row 0 = top**. (Open issue: the screen-locked dither shimmers
under camera/body motion — deliberately not solved yet; a coherent amount of shimmer
is considered on-style.)

**Editor (`app.cpp`).** Blender-style single-viewport layout: top menu bar +
central viewport + **tabbed** Properties panel (right: **Object / Generate / World / Stylize**) +
Timeline strip (bottom).
The viewport **coarsely raytraces** a navigable *edit* camera — a
distinct concept from the scene's *render* camera (the `OrbitCam` is a yaw/pitch/
distance orbit rig with no orbit/look-mode of its own). Its controls are **not** the
Blender defaults (intentionally): **left-drag moves the selected object** (Grab has
no separate `G` key), **right-drag orbits** the view, wheel or **`+`/`-`** zoom,
**`X`/`Y`/`Z`** align the view down a world axis, **`C`** recenters the pivot on the
scene origin, **`0`** toggles look-through-camera, and **`Delete`** removes the
selection. There's no panning. Selecting
an object (left-click ray-pick, **or click its drawn orbit path** when the ray misses —
`pick_orbit` hit-tests the cursor against the projected orbit polylines — or click the
render camera's frustum gizmo → `selected == SEL_CAMERA == -2`) recenters the edit-camera
pivot on it. Overlays
(ImGui draw list via `Camera::project`): an orange outline + orbit path on the
selection, the render-camera frustum gizmo, a scene-origin marker ("O") and a
view-pivot crosshair, and a corner **X/Y/Z axis gizmo**. Orbit paths are an edit-view
aid: hidden through the render camera and toggleable via View ▸ "Show orbits".
**R/S** are still modal Blender transforms (rotate/scale) — move the mouse,
left-click/Enter to confirm, right-click/Esc to cancel. The Timeline has Play/Pause
(also **Space**), speed, and a time scrubber; while playing, `time` advances by frame
delta so orbits and spin animate. View ▸ "Look through camera" (also **`0`**) renders
from the scene's render camera; View ▸ "Display mode" picks the viewport `ShadeMode`
(Direction / In-between / Lit); Export PNG renders the render camera at full
resolution to `renders/` (always Lit). The **File** menu drives modal **Save As…** /
**Load…** dialogs over `scenes/*.json` (also `Ctrl+S` / `Ctrl+Shift+S`) and **Set as
default** (writes `scenes/default.json`); every save/export raises a confirmation popup
showing the absolute path with a strip of the current palette. On startup the editor loads
`scenes/default.json` if present, else the built-in `make_default_scene()`. The **Edit**
menu has **Undo** / **Redo** (`Ctrl+Z` / `Ctrl+Shift+Z`): a snapshot history committed once
an edit settles (mouse up, no active widget), so a continuous drag is one entry; loads reset
it. The **World** tab holds scene-global lighting + the background starfield — incl. the
nebula **Stars-in-nebula** boost, the **galactic band**, and **shooting stars** (with a live
sky preview); **Stylize** holds the palette post-process (incl. the `base_hue` slider and a
**palette library** that saves/loads `palettes/*.json`); **Object** shows the selected body — a
sphere's material/texture, **clouds** layer, and atmosphere (with a neutral-light
material preview), or a ring's color ramp. Color-ramp rows expose a per-stop **alpha**
slider. The **Generate** tab drives the random system generator (see below):
seed/planet-count/moons/spacing/inclination/eccentricity knobs + a **gas-giant ratio**
slider (`gas_ratio`: fraction of planets that are gas giants, 0 = all rocky) +
rings/atmosphere toggles, with **Generate** / **Randomize** buttons that replace all bodies (Undo
recovers the prior scene). An **Appearance (advanced)** block of collapsing headers
(Gas giants / Spin & tilt / Terrestrials / Feature chances) exposes the per-body
appearance + probability params on `SystemGenParams` (storm chance/strength, belt
count/width/turbulence/swirl/**band drift**; spin speed, axial tilt, sideways-tilt chance; water/exotic/
crisp-biome/ice-cap/frozen/seasonal/cloud/**crater** chances; ring/**ring colors**/atmosphere/moon-cap/**sun-texture** chances) —
"chance" sliders are probabilities, the rest bias a jittered random center; defaults
reproduce the old hardcoded look. **These flow through to the eclipse loop too** (it
calls `generate_system`), so they retune every cycle, not just one-shot Generate. Its **Eclipse loop** section (mirrored by an **Infinite**
checkbox on the Timeline) toggles the infinite self-renewing mode, with a
**Scene seconds** slider (fixed per-scene duration), a **Camera loops** knob
(1–3), a **Morph/Snap** palette-transition combo and a **Regenerate now** button (also
**`N`** while in infinite mode — a dev shortcut to jump straight to the next cycle). The
`render_material_preview` / `render_background_preview`
thumbnails live in `core` (neutral lighting, no post-process) so the texture/sky read true.

## Conventions & gotchas

- **Light direction:** `light_dir` is the direction light *travels* (sun → scene);
  the to-light direction is `-light_dir`. It is now the **optional directional fill**
  (off in `Lit` unless `fill_light`); the primary lights are the emissive suns.
- **Suns don't shadow themselves:** shadow rays ignore emissive hits (a sun sphere
  sits at its own light position, so it would otherwise occlude every shadow ray).
- **Post-process runs inside `render_view`**, so the editor viewport and the PNG
  export are identically stylized; overlays are drawn on top via ImGui and stay crisp.
- **The viewport raytrace is gated, not every-frame:** `draw_viewport` re-renders only
  when something can have changed (playing, a widget/mouse drag active, cursor over the
  viewport where keys act, or a resize), plus a few trailing frames so edits settle;
  otherwise it reuses the last texture so an idle editor sits at ~0% CPU. If a change
  doesn't update the view, it's likely missing from that activity check (`Editor`'s
  `redraw_frames`/`vp_hovered`). ImGui itself still redraws each frame at vsync.
- **Color ramps are shared:** `sample_color_ramp` (in `material.h`) serves the radial
  ring ramp, the sphere texture ramp, and the cloud ramp; the editor's `ramp_editor`
  edits all three. Each `ColorStop` carries a per-stop `alpha` (used by clouds; ignored
  by the opaque base/ring sampling, which calls `sample_color_ramp` without the alpha
  out-param).
- **Image orientation:** pixel row 0 is the top, matching both the GL texture upload
  and stb's PNG layout — no vertical flips.
- **`Vec3` is `{float x,y,z;}` with no vtable**, so `&v.x` is a valid `float[3]`
  (relied on by ImGui `DragFloat3`/`ColorEdit3` and the projection math). Don't add
  virtuals to `Vec3`.
- **Orbiting bodies ignore their shape's `center`** — position comes from the orbit,
  so the inspector hides Position while a body is orbiting. Drag-move is likewise
  disabled for orbiting bodies (and for an orbiting render camera).
- **Adding a Ring** around a selected sphere sizes it to 1.2×/1.4× the planet radius,
  lays it in the planet's equatorial plane (`normal = spin_axis`), and attaches it
  with a **radius-0 orbit** parented to the planet so it tracks the planet. Its
  material is created `two_sided` (lit from either face). A radius-0
  active orbit is treated as a pure **attachment** in the inspector (parent picker
  only, no Radius/Period/Phase); `orbit_controls` shows the full orbit fields once
  radius is nonzero.
- **Rotate (R) acts on the object's orientation:** a disk's `normal`, or a sphere's
  `spin.axis` (visible on textured spheres as they spin). It's a **trackball** —
  horizontal drag rotates about the view's up axis, vertical about its right axis — so
  a flat ring viewed top-down still tilts. Scale (S) drives sphere radius / disk radii.
- **Editor preference: when a UI detail is unspecified, do it the Blender way** —
  *except* the edit-camera navigation, which the user deliberately customized
  (left-drag move, right-drag orbit, `+`/`-`/`X`/`Y`/`Z`/`C`, no pan).
- **Files live in subfolders, not the working dir:** `scenes/`, `palettes/`, `renders/`
  (created on startup in `main`). `scene_path`/`palette_path`/`list_json` (app.cpp) build
  paths and list entries; palette I/O (`save_palette`/`load_palette`) lives in `scene.cpp`
  with the other JSON so `core/post` stays JSON-free. Menu actions that change the scene
  (load/add/delete/undo) must call `force_redraw` — the viewport is gated and won't refresh
  on its own (this is what made "Load" look broken).
- **Magnitude fields use `drag_scale`** (app.cpp): a logarithmic `DragFloat` (fine control
  below 1.0) with Ctrl+click to type an exact value — used for radii and noise/cloud scales.
