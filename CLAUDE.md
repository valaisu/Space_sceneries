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

`scene.json` and `render.png` are written to the working directory (both
gitignored). The editor needs a display; the tests do not.

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
- `material.h` — `Material` (albedo + emissive + 3D procedural texture fields + ring/tex `ColorStop` ramps + `Atmosphere`), `ColorStop` + `sample_color_ramp`, `Atmosphere`.
- `hittable.h` — `Hittable` base (virtual `hit` + `clone`, plus `center` and `name`), `HitRecord`.
- `sphere.h` / `sphere.cpp`, `disk.h` / `disk.cpp` — the two primitives + ray intersection. `sphere.cpp` also has `surface_field`/`surface_color` (3D object-space texture); `disk.cpp` samples the radial ring ramp.
- `camera.h` — LookAt `Camera`: `get_ray` (rays), `project` (world→screen, the exact inverse, used for overlays), and `right`/`up`/`forward` basis accessors (used by viewport transforms).
- `motion.h` — `Orbit`, `Spin`, `Body` (a shape + its motion).
- `render.h` / `render.cpp` — `Light`, `ShadeMode`, `Background` (starfield + density regions); shading (`ray_color`: ambient + summed per-light diffuse + hard shadows; `atmosphere_glow`; `background`); `hit_world`; `render_view` / `render_scene` (scene → RGBA8, then `apply_post`); `render_material_preview` / `render_background_preview` (neutral-light editor thumbnails, no post).
- `post.h` / `post.cpp` — `PostProcess`, `DitherMode`; `generate_palette` (HSV harmonic ramp; 0/1/2 anchor colors + seeded `randomness`) + `apply_post` (blur → quantize/dither over the RGBA8 buffer). In `core`, no UI deps.
- `scene.h` / `scene.cpp` — `Scene`, `SceneCamera` (+ `CamLook`), JSON save/load, and posing (`body_world_pos`, `world_at_time`, `orbit_ring_point`, `camera_eye`, `scene_camera`).

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

**Motion (time-based posing).** Orbits are circular: a body orbits a parent body
(or the world origin) in the plane defined by `Orbit::normal`, with radius / period
/ phase. `body_world_pos(scene, i, t)` resolves a body's world position through its
parent chain at time `t`; `world_at_time(scene, t)` returns a posed snapshot
(clones each shape with its computed `center`) which is what the raytracer renders.
`world_at_time` also bakes each sphere's spin orientation (`spin_axis`/`spin_angle`,
from `Spin` + `t`) into the posed clone.

**Textures (Phase 9 → Stage 4).** A sphere `Material` with `pattern != 0` carries a
**3D object-space** procedural texture: layered fbm noise (`noise_scale`,
`noise_octaves`) optionally blended toward warped latitude bands (`band_strength`,
`band_freq`, `warp`) for gas giants, mapped through `tex_ramp` (or `albedo`↔`detail`
when empty). `Sphere::hit` samples it at the **un-spun body-local point** (no pole
pinch, no seam), so a spinning textured sphere visibly rotates. `surface_field`
(scalar) / `surface_color` (albedo) are split out and unit-tested.

**Lighting (Stage 5).** Lights are gathered at pose time. In the final/`Lit` render,
each **emissive sphere is a positional light** (color = its albedo, optional
inverse-square `light_falloff`), plus an optional directional fill (`light_dir`,
gated by `fill_light`; also the fallback when there are no suns). `ray_color` sums an
ambient floor + per-light diffuse, shadow-testing each (emissive bodies are
**transparent to shadow rays**, so suns don't shadow themselves). Editor-only
`ShadeMode` previews: `Direction` (fixed directional + shadows = the old look),
`InBetween` (one directional auto-aimed from the main sun, no shadows), `Lit`
(matches export). `render_scene` always uses `Lit`.

**Background (Stage 2).** Misses return a seeded **starfield**: `background()` hashes
a 3D grid sampled by ray direction (no pole pinch), one star per cell with size /
brightness / tint-variation / density / seed from `Scene::background`. Stars are
sampled over the **3×3×3 neighborhood** with full-cell jitter (round points, no
visible grid rows), and a low-frequency `fbm` field carves **density regions**
(`region_scale` / `region_strength`, plus an additive `region_glow` haze) so the sky
has darker and denser areas.

**Atmosphere (Stage 6).** A sphere `Material` may carry an `Atmosphere` (day `color`,
`sunset` tint, `thickness`, `intensity`). `atmosphere_glow` adds a Fresnel-like limb
term (bright at the silhouette), gated to the sun-lit side and tinted toward `sunset`
near the terminator. Purely additive — **no transparency, no second ray.**

**Rendering & post (Stage 3).** `render_view` shoots one primary ray per pixel, shades
the nearest hit, composites stars on misses, then runs `apply_post`. Its per-pixel
loop is **OpenMP-parallel over rows** (`#pragma omp parallel for`; read-only over the
posed `world`/`lights`, a no-op if OpenMP is absent). `apply_post`: blur →
quantize each pixel to the nearest `PostProcess::palette` color with dithering
(`None` / ordered Bayer / random), optionally repeated. Used by **both** viewport and
export so they match; gated by `PostProcess::enabled`. Palettes are generated by
`generate_palette` (HSV ramp) and hand-editable; generation is anchored on **0, 1, or
2** base colors (`anchor_count`) with a seeded `randomness` jitter and a `palette_seed`
("Randomize" bumps the seed). Output is RGBA8, one
`uint32_t` per pixel, **row 0 = top**. (Open issue: the screen-locked dither shimmers
under camera/body motion — deliberately not solved yet; a coherent amount of shimmer
is considered on-style.)

**Editor (`app.cpp`).** Blender-style single-viewport layout: top menu bar +
central viewport + **tabbed** Properties panel (right: **Object / World / Stylize**) +
Timeline strip (bottom).
The viewport **coarsely raytraces** a navigable *edit* camera — a
distinct concept from the scene's *render* camera (the `OrbitCam` is a yaw/pitch/
distance orbit rig with no orbit/look-mode of its own). Its controls are **not** the
Blender defaults (intentionally): **left-drag moves the selected object** (Grab has
no separate `G` key), **right-drag orbits** the view, wheel or **`+`/`-`** zoom,
**`X`/`Y`/`Z`** align the view down a world axis, **`C`** recenters the pivot on the
scene origin, **`0`** toggles look-through-camera, and **`Delete`** removes the
selection. There's no panning. Selecting
an object (left-click ray-pick, or click the render camera's frustum gizmo →
`selected == SEL_CAMERA == -2`) recenters the edit-camera pivot on it. Overlays
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
resolution (always Lit). The **World** tab holds scene-global lighting + the
background starfield (with a live sky preview); **Stylize** holds the palette
post-process; **Object** shows the selected body — a sphere's material/texture (with a
neutral-light material preview) and atmosphere, or a ring's color ramp. The
`render_material_preview` / `render_background_preview` thumbnails live in `core`
(neutral lighting, no post-process) so the texture/sky read true.

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
- **Color ramps are shared:** `sample_color_ramp` (in `material.h`) serves both the
  radial ring ramp and the sphere texture ramp; the editor's `ramp_editor` edits both.
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
  with a **radius-0 orbit** parented to the planet so it tracks the planet. A radius-0
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
