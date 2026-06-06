# Space Sceneries

A from-scratch C++ raytracer and **Blender-style scene editor** for composing
simplified, pixel-art space scenes (planets, moons, rings, a sun) and exporting
them as PNGs. Spheres and disks are intersected mathematically; lighting is a
single directional light with hard shadows and an ambient fill.

`initial_ideas.md` is the original phased spec. This file is the coarse,
up-to-date map; prefer it when the two disagree.

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

## Layout

- `src/` — all first-party code.
- `third_party/` — vendored single-header / drop-in deps (nlohmann json, imgui, stb).
- `build/` — CMake output (gitignored).

### Source-file map (`src/`)

Core math / geometry (header-only unless noted):
- `vec3.h` — `Vec3` + dot/cross/normalize/length + `rotate_about` (Rodrigues).
- `ray.h` — `Ray`.
- `material.h` — `Material` (albedo + emissive flag + procedural `pattern`/`detail`).
- `hittable.h` — `Hittable` base (virtual `hit` + `clone`, plus `center` and `name`), `HitRecord`.
- `sphere.h` / `sphere.cpp`, `disk.h` / `disk.cpp` — the two primitives + ray intersection.
- `camera.h` — LookAt `Camera`: `get_ray` (rays), `project` (world→screen, the exact inverse, used for overlays), and `right`/`up`/`forward` basis accessors (used by viewport transforms).
- `motion.h` — `Orbit`, `Spin`, `Body` (a shape + its motion).
- `render.h` / `render.cpp` — shading (`ray_color`: diffuse + hard shadow + ambient), `hit_world`, and `render_view` / `render_scene` (scene → RGBA8 pixel buffer).
- `scene.h` / `scene.cpp` — `Scene`, JSON save/load, and posing (`body_world_pos`, `world_at_time`, `orbit_ring_point`).

Executables:
- `app.cpp` — the editor (ImGui + GLFW + OpenGL). **GUI-only.**
- `tests.cpp` — headless assertions; built as `space_sceneries_tests`.
- `stb_image_write_impl.cpp` — compiles the stb implementation (GUI target only).

### CMake targets

- `core` (static lib) — everything except the UI; depends only on vendored headers.
- `space_sceneries_tests` — links `core`.
- `imgui` (static lib) + `space_sceneries` — the editor; links `core`, `imgui`, GLFW, OpenGL.

## Architecture

**Scene model.** A `Scene` holds `std::vector<Body>`, the render camera (stored as
authoring params, not a built `Camera`), the directional `light_dir`, and the
output `width`/`height`. A `Body` is a `shape` (`Sphere`/`Disk`) plus an `Orbit`
and a `Spin`. There are **no separate Planet/Moon types** — those are just spheres
with a name and material.

**Motion (time-based posing).** Orbits are circular: a body orbits a parent body
(or the world origin) in the plane defined by `Orbit::normal`, with radius / period
/ phase. `body_world_pos(scene, i, t)` resolves a body's world position through its
parent chain at time `t`; `world_at_time(scene, t)` returns a posed snapshot
(clones each shape with its computed `center`) which is what the raytracer renders.
`world_at_time` also bakes each sphere's spin orientation (`spin_axis`/`spin_angle`,
from `Spin` + `t`) into the posed clone.

**Textures (Phase 9).** A sphere `Material` can carry a procedural `pattern`
(0 solid / 1 stripes / 2 mottled) mixing `albedo` toward `detail`. `Sphere::hit`
samples it in body-local space (un-spun via `spin_angle`) using spherical UVs, so a
spinning textured sphere visibly rotates. The mottled noise tiles in longitude
(seamless wrap). Emissive bodies keep using their (textured) albedo directly.

**Rendering.** `render_view` shoots one primary ray per pixel from a given camera,
finds the nearest hit, and shades: emissive surfaces return their albedo;
others use `ambient + (1-ambient)*diffuse`, with `diffuse = max(0, dot(N, -light_dir))`
zeroed when a shadow ray toward the light is blocked. Misses return a near-black
background. Output is RGBA8, one `uint32_t` per pixel, **row 0 = top**.

**Editor (`app.cpp`).** Blender-style single-viewport layout: top menu bar +
central viewport + contextual Properties panel (right) + Timeline strip (bottom).
The viewport **continuously, coarsely raytraces** a navigable editor camera
(default top-down; MMB-orbit / Shift+MMB-pan / scroll-zoom). Click selects a body
(ray-pick); the selection shows an orange outline plus its orbit path, drawn with
ImGui's draw list using `Camera::project`. **G/R/S** start Blender-style modal
transforms (grab/rotate/scale) on the selection — move the mouse, left-click/Enter
to confirm, right-click/Esc to cancel. The Timeline has Play/Pause (also **Space**),
speed, and a time scrubber; while playing, `time` advances by frame delta so orbits
and spin animate. View ▸ "Look through camera" renders from the scene's render
camera; Export PNG renders that camera at full resolution.

## Conventions & gotchas

- **Light direction:** `light_dir` is the direction light *travels* (sun → scene);
  the to-light direction is `-light_dir`. Diffuse and shadow rays both use `-light_dir`.
- **Image orientation:** pixel row 0 is the top, matching both the GL texture upload
  and stb's PNG layout — no vertical flips.
- **`Vec3` is `{float x,y,z;}` with no vtable**, so `&v.x` is a valid `float[3]`
  (relied on by ImGui `DragFloat3`/`ColorEdit3` and the projection math). Don't add
  virtuals to `Vec3`.
- **Orbiting bodies ignore their shape's `center`** — position comes from the orbit,
  so the inspector hides Position while a body is orbiting. Grab (G) is likewise
  disabled for orbiting bodies.
- **Rotate (R) acts on the object's orientation:** a disk's `normal`, or a sphere's
  `spin.axis` (visible on textured spheres as they spin). Scale (S) drives sphere
  radius / disk radii.
- **Editor preference: when a UI detail is unspecified, do it the Blender way**
  (single live viewport, contextual panels, select → outline + orbit, G/R/S transforms).
- **Phase numbering** in `initial_ideas.md`: section N corresponds to phase N−2.
