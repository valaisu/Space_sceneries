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
    float gas_ratio = 0.5f;     // ~fraction of planets that are gas giants (0 = all rocky)
    int   hero_kind = 0;        // reframe target: 0 = prefer gas giant, 1 = rocky, 2 = gas

    // --- Appearance / probability knobs. Defaults reproduce the previous hardcoded
    // behaviour. "chance" values are probabilities in [0,1]; the rest are centers
    // that the generator jitters per-body so variety is preserved. These apply to
    // both Generate and every eclipse-loop cycle (the loop calls generate_system).
    // Gas giants:
    float gas_storm_chance = 0.34f;    // fraction with an oval storm
    float gas_storm_strength = 0.78f;  // storm opacity (center)
    float gas_belt_count = 7.0f;       // ~number of latitude belts (band_freq center)
    float gas_belt_var = 0.8f;         // belt-width unevenness (center)
    float gas_turbulence = 0.6f;       // intra-belt filament amount (center)
    float gas_swirl = 0.15f;           // belt warp/swirl (center)
    float gas_drift = 0.03f;           // band-layer drift speed, revs/sec (0 = static)
    // Spin & axial tilt (all planets):
    float spin_speed = 1.0f;           // rotation-speed multiplier (1 = default 3..10s)
    float axial_tilt = 22.0f;          // default max axial tilt, degrees
    float extreme_tilt_chance = 0.08f; // chance of a Uranus-like ~90 deg tilt
    // Terrestrials:
    float water_chance = 0.55f;        // fraction that are water worlds (else dry/desert)
    float exotic_chance = 0.30f;       // fraction with an alien (non-Earth) hue family
    float biome_chance = 0.80f;        // fraction with posterized crisp biome bands
    float cap_chance = 0.80f;          // fraction with visible polar ice caps
    float frozen_chance = 0.15f;       // subset that are fully frozen worlds
    float season_chance = 0.60f;       // fraction of capped worlds with seasonal swing
    float cloud_chance = 0.60f;        // cloud chance (water worlds; dry = half)
    float crater_chance = 0.70f;       // airless (no-atmosphere) rocky worlds/moons with craters
    // Feature chances:
    float ring_chance = 0.45f;         // gas giants with a ring (needs `rings`)
    float ring_colors = 4.0f;          // ~number of radial color bands on a ring (center)
    float atmosphere_chance = 0.45f;   // planets with atmospheric glow (needs `atmospheres`)
    float moon_cap_chance = 0.40f;     // moons with polar caps
    float sun_texture_chance = 0.40f;  // chance the sun shows faint granulation/banding
};

// Editor-only authoring controls that aren't part of the rendered Scene: the
// Generate-tab sliders, the eclipse-loop knobs, and a few view preferences.
// Persisted in the scene JSON's optional "editor" section so "Set as default"
// and Save restore the full editing state; absent in older files (defaults used).
struct EditorSettings {
    SystemGenParams gen;        // the Generate tab
    // Eclipse loop:
    float scene_seconds = 30.0f;
    int   cam_loops = 2;
    int   pal_transition = 0;   // 0 = Morph, 1 = Snap
    bool  infinite_mode = false;
    // View preferences:
    float play_speed = 1.0f;
    ShadeMode shade_mode = ShadeMode::Lit;
    bool  show_orbits = true;
};

// Replace `scene.bodies` with a freshly generated system (sun at body 0, then
// planets, then their moons/rings) and reframe `scene.cam` to fit. Leaves the
// background, palette and output resolution untouched.
void generate_system(Scene& scene, const SystemGenParams& p);

// "Infinite eclipse loop" variant. Builds a normal system via generate_system
// (with the params biased toward a more spread-out, tilted system so fewer bodies
// read as fully dark), shrinks the inner planets, then fits the foreground planet
// + render camera into a fixed canonical frame so that at t = 0 (and once per
// `scene_seconds` thereafter) the camera looks +Z at the planet with the sun
// directly behind it — an eclipse. One cycle lasts exactly `scene_seconds`
// regardless of orbit sizes, so the apparent motion speed is constant across
// scenes; the camera makes `cam_loops` revolutions per cycle (the planet does
// `cam_loops - 1` orbits, keeping exactly one eclipse per cycle; cam_loops == 1
// parks the planet). The framing (distance/FOV) is identical every cycle and the
// sun radius is clamped to stay hidden, so the editor can regenerate the whole
// system at the dark moment with a seamless cut. Cycle length = scene_seconds.
// The close-up hero alternates type each cycle (rocky world vs gas giant, via
// `hero_kind` keyed off the per-cycle seed) so the loop doesn't repeat one look.
void generate_eclipse_system(Scene& scene, const SystemGenParams& p,
                             float scene_seconds, int cam_loops);

// Phase 3 / 5.2: JSON round-trip. Returns/accepts a pretty-printed JSON string.
// The two-arg form embeds the editor settings under an "editor" key.
std::string scene_to_json(const Scene& scene);
std::string scene_to_json(const Scene& scene, const EditorSettings& settings);
Scene scene_from_json(const std::string& text);

// File helpers for the editor. Return false on I/O or parse failure. The
// EditorSettings overloads also persist/restore the "editor" section; the
// scene-only overloads omit it on save (and ignore it on load).
bool save_scene(const Scene& scene, const std::string& path);
bool load_scene(Scene& out, const std::string& path);
bool save_scene(const Scene& scene, const EditorSettings& settings, const std::string& path);
bool load_scene(Scene& out, EditorSettings& settings, const std::string& path);

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
