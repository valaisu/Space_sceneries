#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vec3.h"
#include "camera.h"
#include "motion.h"
#include "render.h"  // World, Background
#include "post.h"    // PostProcess

// Phase 3 / 5.1: holds everything the renderer and editor need. Camera is stored
// as authoring params (not a built Camera) so it serializes cleanly; aspect is
// derived from the output resolution. Bodies are Spheres/Disks with a name+material
// — no separate Planet/Moon types (3.3) — each wrapped in a Body carrying its
// orbit/spin (Phase 6).
// How the render camera decides where to look: a constant world Direction, a
// Target body it tracks (or the world origin), or a constant-rate Spin about vup.
enum class CamLook { Direction = 0, Target = 1, Spin = 2 };

// The render camera as a first-class scene object: it has a position (or an Orbit,
// reusing the body machinery), an orientation policy, and an FOV. It's posed at a
// time `t` by scene_camera(). Visible/movable in the editor but never raytraced.
struct SceneCamera {
    Vec3 position{0, 6, 14};   // eye position when not orbiting
    Orbit orbit;               // when active, the eye is derived from this each frame
    Vec3 vup{0, 1, 0};
    float fov = 50.0f;         // vertical field of view, degrees

    CamLook look = CamLook::Target;
    Vec3 direction{0, 0, -1};  // Direction mode: constant world facing
    int target = -1;           // Target mode: body index to track (-1 = world origin)
    float spin_period = 0.0f;  // Spin mode: seconds per revolution about vup (0 = still)
    float spin_phase = 0.0f;   // Spin mode: base yaw, radians
};

struct Scene {
    std::vector<Body> bodies;

    SceneCamera cam;  // the render camera (4.1)

    Vec3 light_dir{0, -1, -1};  // Stage 5: optional directional fill light (travel dir)
    bool fill_light = false;    // include the directional fill in Lit mode
    bool light_falloff = false; // suns use distance attenuation (off by default)

    Background background;  // Stage 2: procedural starfield
    PostProcess post;       // Stage 3: palette post-process

    // Output resolution (runtime value, exposed in the UI later — 7.1).
    int width = 400;
    int height = 200;

    float aspect_ratio() const { return static_cast<float>(width) / static_cast<float>(height); }
};

// Procedural star-system generator (the editor's "Generate" tab). Controls for
// how a random system is built; tuned so that, by default, bodies sit well apart
// — a planet's moons stay in a small bubble that never reaches a neighbouring
// planet or the sun, so a moon is always closest to the planet it orbits.
// Real systems are loose inspiration (geometric orbit spacing, inner rocky /
// outer gas giants, near-coplanar orbits) but distances are kept practical.
struct SystemGenParams {
    int   seed = 1;
    int   planet_count = 5;     // number of planets around the sun
    int   max_moons = 2;        // up to this many moons per planet (0 = none)
    float spacing = 1.6f;       // orbit growth factor between successive planets
    float sun_radius = 1.5f;
    float inclination = 0.2f;   // 0 = coplanar .. 1 = strongly tilted orbits
    float eccentricity = 0.1f;  // 0 = circular .. this is the per-orbit max
    bool  rings = true;         // allow rings on some gas giants
    bool  atmospheres = true;   // allow atmospheric glow on some planets
};

// Replace `scene.bodies` with a freshly generated system (sun at body 0, then
// planets, then their moons/rings) and reframe `scene.cam` to fit. Leaves the
// background, palette and output resolution untouched.
void generate_system(Scene& scene, const SystemGenParams& p);

// Phase 3 / 5.2: JSON round-trip. Returns/accepts a pretty-printed JSON string.
std::string scene_to_json(const Scene& scene);
Scene scene_from_json(const std::string& text);

// File helpers for the editor. Return false on I/O or parse failure.
bool save_scene(const Scene& scene, const std::string& path);
bool load_scene(Scene& out, const std::string& path);

// Palette library: a saved palette is just its list of colors (a reusable
// post-process palette, independent of any scene). Return false on I/O/parse error.
bool save_palette(const std::vector<Vec3>& palette, const std::string& path);
bool load_palette(std::vector<Vec3>& out, const std::string& path);

// Phase 6: world-space position of body `i` at time `t`, resolving orbit + parent
// chain. Stationary bodies return their authored center.
Vec3 body_world_pos(const Scene& scene, int i, float t);

// World-space eye position of the camera at time `t` (resolves its orbit chain).
Vec3 camera_eye(const Scene& scene, float t);

// Posed render camera at time `t`: resolves the camera's orbit and look mode.
// `aspect` is the image aspect to build the frustum with (output aspect for final
// renders, viewport aspect for the editor preview).
Camera scene_camera(const Scene& scene, float t, float aspect);

// Phase 6: build a posed snapshot of the scene at time `t` — clones each body's
// shape with its computed world center. This is what the raytracer renders.
World world_at_time(const Scene& scene, float t);

// Phase 7: a world point on body `i`'s orbit at absolute angle `angle` (radians),
// anchored at the parent's position at time `t`. Used to draw orbit-path overlays.
// Returns the parent position itself if the body has no active orbit.
Vec3 orbit_ring_point(const Scene& scene, int i, float t, float angle);

// Phase 7: render the scene from an explicit camera at an explicit resolution.
// Used by the editor viewport (coarse, editor camera). Row 0 is the top.
// `mode` selects the display lighting (Stage 5); the editor passes its current
// mode, the final render uses Lit.
void render_view(const Scene& scene, const Camera& cam, float t,
                 int w, int h, std::vector<uint32_t>& out,
                 ShadeMode mode = ShadeMode::Lit);

// Phase 5 / 7.1: render the scene at time `t` into an RGBA8 buffer (one uint32_t
// per pixel, byte order R,G,B,A). Row 0 is the top of the image — matches both the
// GL texture upload and stb_image_write's PNG layout. `out` is resized to w*h.
// Uses the scene's render camera and resolution.
void render_scene(const Scene& scene, float t, std::vector<uint32_t>& out);
