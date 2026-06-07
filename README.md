# Space Sceneries

![A rendered space scene: pixel-art planets and moons over a starfield](docs/preview.png)

A from-scratch C++ raytracer with a **Blender-style scene editor** for composing
simplified, pixel-art space scenes and exporting them as PNGs or animated GIFs. You
arrange bodies in a live viewport, light them with emissive suns, and a palette
post-process crushes the final image toward deliberate pixel-art color.

## What you can make

- **Planets and moons** — spheres with procedural 3D textures (fbm noise, warped
  latitude bands for gas giants that slowly **drift**, **impact craters** on airless
  rocky worlds), translucent **cloud** layers, and Fresnel-limb **atmospheres**.
- **Rings** — translucent annular disks with radial color ramps, lit from either face.
- **Suns** — emissive bodies that act as the scene's lights (with hard shadows and
  optional inverse-square falloff).
- **Starfields** — seeded backgrounds with denser and sparser (nebula) regions, an
  optional Milky-Way-style **galactic band**, and animated **shooting stars**.
- **Motion** — **Kepler elliptic orbits** (and circles) plus spin, scrubbable on a
  timeline so scenes animate.
- **Random star systems** — a **Generate** tab builds a whole system (sun, planets,
  moons, rings) with real-system-inspired spacing, then orbits the camera around a
  planet; tweak the seed, planet/moon counts, spacing, and tilt.
- **A pixel-art look** — generated HSV palettes (harmonic schemes — monochromatic,
  complementary, triadic, analogous, or hand-anchored — saveable to a reusable library)
  with dithering, applied identically to the viewport and the exported PNG.
- **An infinite eclipse loop** — an "Infinite mode" that opens on a planet eclipsing
  its sun, reveals the system as it plays, then silently regenerates a whole new system
  at each eclipse (with a morphing palette), so the scene renews itself forever.
- **Export** — still **PNG** frames or an **animated GIF** (by duration, or by a number
  of eclipse-loop cycles), rendered with a progress bar you can cancel. The output
  resolution is editable in the Timeline, and **`F`** shows the scene fullscreen
  (black-letterboxed to the output aspect).

## Quick start

After building and launching, the editor opens with a default scene in the live viewport.

### Layout

- **Central viewport** — raytraced preview; right-click-drag to orbit, scroll or `+`/`-` to zoom.
- **Properties panel (right)** — tabbed: **Object** (selected body), **Generate** (random system), **World** (starfield/lighting), **Stylize** (palette).
- **Timeline (bottom)** — playback controls, resolution, and export.

### Viewport controls

| Action | Input |
|---|---|
| Select object | Left-click (or click its orbit path) |
| Move selected object | Left-drag |
| Orbit view | Right-drag |
| Zoom | Scroll wheel or `+` / `-` |
| Align view to axis | `X` / `Y` / `Z` |
| Recenter pivot on origin | `C` |
| Look through render camera | `0` |
| Rotate selected (modal) | `R`, then move mouse → confirm Left-click / cancel Right-click |
| Scale selected (modal) | `S`, then move mouse → confirm Left-click / cancel Right-click |
| Delete selected | `Delete` |

### General hotkeys

| Hotkey | Action |
|---|---|
| `Space` | Play / Pause |
| `F` | Toggle fullscreen (hides panels, letterboxed to output aspect) |
| `Esc` | Exit fullscreen |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / Redo |
| `Ctrl+S` | Save scene |
| `Ctrl+Shift+S` | Save As |

### Infinite eclipse loop

Infinite mode auto-generates a fresh system at each eclipse and morphs the palette — the scene renews itself forever.

1. Open the **Generate** tab and adjust seed/planet count/spacing to taste (or leave defaults).
2. Tick **Infinite** on the Timeline strip (or use the *Eclipse loop* section in the Generate tab).
3. Press **Space** to play. The scene opens on an eclipse, reveals the system, then silently regenerates at the next eclipse.
4. Press **`N`** at any time to skip immediately to the next cycle.
5. The **Scene seconds** slider (Generate → Eclipse loop) controls how long each cycle lasts; **Camera loops** (1–3) sets how many times the camera orbits per cycle.

Use **Export GIF… → By loop cycles** in the Timeline to bake N eclipse cycles to a `.gif`.

## Build & run

GLFW and OpenGL come from the system; `nlohmann/json`, Dear ImGui, `stb_image_write`,
and `gif.h` are vendored in `third_party/`.

```bash
cmake -S . -B build        # first time / after CMakeLists or file-list changes
cmake --build build -j     # after editing code
./build/space_sceneries          # the editor (needs a display)
./build/space_sceneries_tests    # headless sanity checks
```

Saved scenes go in `scenes/`, saved palettes in `palettes/`, and exported PNGs/GIFs in
`renders/` (all created on first run).
