# Space Raytracer & Scene Editor: Project Specification

## 1. Project Overview & Goals
This project is a standalone, from-scratch 3D rendering engine and scene editor designed to create simplified, pixel-art style space scenes. 

**Core Functionality:**
* **Raytraced Rendering:** Accurately renders spheres (planets, moons) and disks (planetary rings) using mathematical ray-intersection instead of rasterization.
* **Lighting & Shadows:** Calculates directional lighting (from a sun) and casts secondary rays to generate hard shadows (e.g., a moon casting a shadow on a planet).
* **Interactive Scene Editor:** A graphical user interface (GUI) allowing users to add, remove, and manipulate celestial bodies in real-time.
* **Low-Resolution Aesthetic:** The renderer operates at a fixed low resolution (e.g., 400x200) and upscales the output without interpolation to achieve a retro, pixelated aesthetic.
* **Export:** Allows the user to export the rendered scene as an image file.

## 2. Project Architecture
* **Language:** C++17 or C++20
* **Build System:** CMake
* **Dependencies:**
    * **Windowing/Input:** GLFW (+ OpenGL context)
    * **GUI:** Dear ImGui
    * **Serialization:** `nlohmann/json` (for saving/loading scene files)
    * **Image Output:** `stb_image_write.h` (for exporting standard image formats)

## 3. Phase 1: Core Mathematics & Primitives
Implement the fundamental types required for raytracing without relying on external math libraries.

### 3.1 Vector Mathematics
* Implement a `Vec3` struct (`float x, y, z;`).
* **Operator Overloads:** `+`, `-`, `*` (scalar), `/` (scalar).
* **Core Math Functions:** * `float dot(Vec3 a, Vec3 b)`
    * `Vec3 cross(Vec3 a, Vec3 b)`
    * `Vec3 normalize(Vec3 v)`
    * `float length(Vec3 v)`

### 3.2 Ray Definition
* Struct `Ray` containing `Vec3 origin` and `Vec3 direction`.
* Function `Vec3 point_at_parameter(float t)`: returns `origin + t * direction`.

### 3.3 Geometry Definitions
The geometry model is a single polymorphic `Hittable` base with two concrete primitives. "Planet" and "Moon" are **not** separate types — they are both spheres distinguished only by their material/label.
* `Sphere`: Needs `Vec3 center`, `float radius`, and a `Material`.
* `Disk` (for rings): Needs `Vec3 center`, `Vec3 normal`, `float inner_radius`, `float outer_radius`, and a `Material`.
* **Intersection Interface:** Create a base `Hittable` class with a virtual `hit` function returning a boolean and filling a `HitRecord` struct. `HitRecord` contains $t$, intersection point $\mathbf{P}$, surface normal $\mathbf{N}$, and the hit object's `Material` (so the shading loop knows what color was hit).

### 3.4 Materials
Keep materials minimal for the pixel-art aesthetic — no PBR.
* `struct Material`:
    * `Vec3 albedo` — base surface color, used by diffuse shading.
    * `bool emissive = false` — if true, the surface ignores lighting/shadows and renders at full `albedo` (for the sun and any self-lit bodies).
* `HitRecord` carries a copy (or pointer) to the hit object's `Material`; the shading loop in 4.3 branches on `emissive` before doing diffuse + shadow work.

## 4. Phase 2: Raytracing Engine Core
Build the logic that shoots rays and calculates colors.

### 4.1 Camera System
* Implement a LookAt camera.
* **Parameters:** `lookfrom`, `lookat`, `vup` (up vector), `fov` (field of view), `aspect_ratio`.
* **Axes Calculation:** Calculate local `u`, `v`, `w` axes using cross products to orient the camera frame and generate primary rays.

### 4.2 Intersection Mathematics
* **Ray-Sphere:** Solve the quadratic equation $at^2 + bt + c = 0$. Return the smallest positive root $t$.
* **Ray-Disk:** Intersect the ray with an infinite plane (`dot(normal, direction)`), then verify if the hit point's distance from the center is between `inner_radius` and `outer_radius`. Note: this disk is infinitely thin, so at grazing camera angles the ring can shimmer or disappear. For the 400×200 pixel-art target this is likely acceptable; revisit only if it looks bad.

### 4.3 Lighting & Shadows
* **Directional Light:** Define a global `Vec3 light_dir`. **Convention:** `light_dir` is the direction the light **travels** (sun → scene). The direction from a surface *toward* the light is therefore `-light_dir`.
* **Diffuse Shading:** Calculate color intensity using `max(0.0f, dot(normal, -light_dir))` (i.e. dot with the to-light direction).
* **Shadow Rays:** From the intersection point, cast a secondary ray towards `-light_dir`. Offset the ray origin along the normal (`P + N * epsilon`, e.g. `1e-3`) to avoid self-shadowing ("shadow acne"). Because the light is **directional** (no position), any hit at `t > epsilon` puts the point in shadow — there is no max distance to clamp against. A shadowed point receives no direct light.
* **Ambient Fill:** Add a small ambient floor (e.g. `0.15`) so shadowed surfaces and faces pointing away from the sun aren't pure black. Final intensity = `ambient + (1 - ambient) * direct`, where `direct` is the diffuse term (or `0` if in shadow). This keeps intensity in `[ambient, 1]` and replaces the older "multiply by 0.1" shadow hack.

### 4.4 Background / Sky
Rays that hit nothing need a color. For space this is near-black (e.g. `Vec3(0.01, 0.01, 0.02)`). A starfield is **out of scope for now**; if added later, the cheapest route is a hash-based per-pixel sprinkle in the background shade function rather than real emissive geometry. Decide before relying on it, since geometry-based stars would change scene serialization (5.2).

## 5. Phase 3: Scene Management & Serialization
Manage the state of the simulation so the editor can modify it and save it to disk.

### 5.1 Data Structures
* `class Scene`: Holds the geometric entities (e.g. `std::vector<std::shared_ptr<Hittable>>`), camera settings, and light direction. Per the geometry model in 3.3, there are no separate Planet/Moon classes — bodies are `Sphere`s with a name/label and material.

### 5.2 Serialization (JSON)
* Implement functions to convert `Scene` to JSON and parse JSON back to a `Scene` object.
* **Syntax example using nlohmann/json:**
    ```cpp
    json j;
    j["planets"].push_back({{"radius", p.radius}, {"center", {p.center.x, p.center.y, p.center.z}}});
    ```

## 6. Phase 4: Scene Editor (GUI)
Build the interactive layer using Dear ImGui to manipulate the scene without recompiling.

### 6.1 Window Setup
* Initialize the GLFW window and an OpenGL context.
* Initialize the ImGui context and attach backend implementations.

### 6.2 UI Layout
* **Hierarchy Panel:** List all objects in the `Scene`. Use `ImGui::Selectable` to pick an active object.
* **Inspector Panel:** Display and edit properties of the selected object. 
    * **Useful widgets:** `ImGui::DragFloat3` for coordinate manipulation, `ImGui::SliderFloat` for radius adjustments.
* **Global Settings:** Controls for camera position, sun direction, and output resolution.

## 7. Phase 5: Integration & Rendering Loop
Connect the raytracer to the UI for previewing and image exporting.

### 7.1 Real-Time Preview
* Allocate a memory buffer for pixel data sized to the current resolution (e.g., `std::vector<uint32_t> pixels(width * height)`). Resolution is a runtime value, not a compile-time constant, since 6.3 exposes it as a UI control.
* Run the raytracer over the pixel array to generate the frame.
* **Render trigger:** At 400×200 (~80k primary + ~80k shadow rays) a single-threaded brute-force render is cheap, but do **not** re-render every UI frame. Re-render only when the scene/camera/light/resolution changes (a dirty flag set by the editor), and keep displaying the last texture otherwise. If a single render ever feels sluggish, parallelize the per-row loop before reaching for anything fancier.
* Upload the pixel array to an OpenGL Texture.
* Display the texture in an ImGui window using `ImGui::Image`. 
* *Crucial Setup:* Configure the texture sampler to use **Nearest Neighbor filtering** (e.g., `GL_NEAREST` in OpenGL) to retain the sharp, pixelated aesthetic when the 400x200 image is scaled up on standard monitors.

### 7.2 Offline Export
* Provide an "Export Image" button in the GUI.
* When triggered, dump the current `pixels` array to disk.
* **Useful function:** `stbi_write_png("render.png", width, height, 4, pixels, width * 4);`