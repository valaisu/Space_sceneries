#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vec3.h"
#include "camera.h"
#include "motion.h"
#include "render.h"  // World

// Phase 3 / 5.1: holds everything the renderer and editor need. Camera is stored
// as authoring params (not a built Camera) so it serializes cleanly; aspect is
// derived from the output resolution. Bodies are Spheres/Disks with a name+material
// — no separate Planet/Moon types (3.3) — each wrapped in a Body carrying its
// orbit/spin (Phase 6).
struct Scene {
    std::vector<Body> bodies;

    // Camera authoring params (4.1).
    Vec3 cam_lookfrom{0, 0, 0};
    Vec3 cam_lookat{0, 0, -1};
    Vec3 cam_vup{0, 1, 0};
    float cam_fov = 90.0f;  // vertical FOV, degrees

    Vec3 light_dir{0, -1, -1};  // travel direction (4.3)

    // Output resolution (runtime value, exposed in the UI later — 7.1).
    int width = 400;
    int height = 200;

    float aspect_ratio() const { return static_cast<float>(width) / static_cast<float>(height); }
    Camera camera() const { return Camera(cam_lookfrom, cam_lookat, cam_vup, cam_fov, aspect_ratio()); }
};

// Phase 3 / 5.2: JSON round-trip. Returns/accepts a pretty-printed JSON string.
std::string scene_to_json(const Scene& scene);
Scene scene_from_json(const std::string& text);

// File helpers for the editor. Return false on I/O or parse failure.
bool save_scene(const Scene& scene, const std::string& path);
bool load_scene(Scene& out, const std::string& path);

// Phase 6: world-space position of body `i` at time `t`, resolving orbit + parent
// chain. Stationary bodies return their authored center.
Vec3 body_world_pos(const Scene& scene, int i, float t);

// Phase 6: build a posed snapshot of the scene at time `t` — clones each body's
// shape with its computed world center. This is what the raytracer renders.
World world_at_time(const Scene& scene, float t);

// Phase 7: a world point on body `i`'s orbit at absolute angle `angle` (radians),
// anchored at the parent's position at time `t`. Used to draw orbit-path overlays.
// Returns the parent position itself if the body has no active orbit.
Vec3 orbit_ring_point(const Scene& scene, int i, float t, float angle);

// Phase 7: render the scene from an explicit camera at an explicit resolution.
// Used by the editor viewport (coarse, editor camera). Row 0 is the top.
void render_view(const Scene& scene, const Camera& cam, float t,
                 int w, int h, std::vector<uint32_t>& out);

// Phase 5 / 7.1: render the scene at time `t` into an RGBA8 buffer (one uint32_t
// per pixel, byte order R,G,B,A). Row 0 is the top of the image — matches both the
// GL texture upload and stb_image_write's PNG layout. `out` is resized to w*h.
// Uses the scene's render camera and resolution.
void render_scene(const Scene& scene, float t, std::vector<uint32_t>& out);
