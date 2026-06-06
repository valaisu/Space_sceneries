#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vec3.h"
#include "ray.h"
#include "hittable.h"

// Phase 2: shading core. A "world" is just a list of hittables for now; the full
// Scene class (camera + light + serialization) arrives in Phase 3.
using World = std::vector<std::shared_ptr<Hittable>>;

// Stage 2: procedural starfield for rays that hit nothing. A seeded star function
// sampled by ray direction over `sky`. Stable per seed; all params live-tunable.
struct Background {
    Vec3 sky{0.01f, 0.01f, 0.02f};  // base near-black space color
    float density = 0.08f;          // probability a grid cell holds a star [0,1]
    float brightness = 1.0f;        // star intensity scale
    float size = 0.2f;              // star radius in grid-cell units (0..0.5)
    Vec3 tint{1.0f, 1.0f, 1.0f};    // base star color
    float color_variation = 0.25f;  // warm/cool spread per star
    int seed = 1337;

    // Large-scale density regions: a low-frequency field carves darker and denser
    // areas across the sky (no true nebulae). region_glow adds a faint haze in the
    // densest regions.
    float region_scale = 2.0f;      // region feature size (lower = broader patches)
    float region_strength = 0.5f;   // how strongly regions modulate local star density
    float region_glow = 0.0f;       // additive haze in dense regions (0 = off)
};

// Stage 5: a single light. Directional lights model the old sun -> scene fill;
// positional lights are derived from emissive bodies (suns).
struct Light {
    bool directional = true;  // true: `vec` is travel direction; false: `vec` is world position
    Vec3 vec{0, -1, -1};      // travel direction (directional) or world position (positional)
    Vec3 color{1, 1, 1};      // light color / intensity
    bool falloff = false;     // positional only: apply distance attenuation
};

// Stage 5: editor display modes (final render always uses Lit).
//  - Direction:  one fixed directional light + shadows + ambient (the old look).
//  - InBetween:  one directional light auto-aimed from the main sun, no shadows.
//  - Lit:        positional lights from emissive bodies + shadows (matches export).
enum class ShadeMode { Direction = 0, InBetween = 1, Lit = 2 };

// Nearest hit across the world within (t_min, t_max).
bool hit_world(const World& world, const Ray& r, float t_min, float t_max, HitRecord& rec);

// Stage 6: additive atmosphere limb glow at a surface hit. `normal` is the
// outward surface normal, `view_dir` points from the surface toward the camera,
// `to_light` toward the main sun. Brightest at the sun-lit silhouette, tinted
// toward `sunset` near the terminator, zero on the night side. Exposed for tests.
Vec3 atmosphere_glow(const Atmosphere& a, Vec3 normal, Vec3 view_dir, Vec3 to_light);

// Phase 2 / 4.4 + Stage 2: color for a ray that hits nothing — the starfield.
Vec3 background(const Ray& r, const Background& bg = Background{});

// Phase 2 / 4.3 + Stage 5: shade a primary ray against a set of lights. Emissive
// surfaces return their albedo; others get ambient floor + summed diffuse, each
// light optionally shadow-tested. Misses return the starfield described by `bg`.
Vec3 ray_color(const Ray& r, const World& world, const std::vector<Light>& lights,
               float ambient, bool shadows, const Background& bg = Background{});

// Small standalone previews for the editor panels — neutral lighting, no shadows,
// no post-process, so the surface texture / sky read true. RGBA8, one uint32_t per
// pixel (byte order R,G,B,A); row 0 = top. `out` is resized to w*h.
void render_material_preview(const Material& m, int w, int h, std::vector<uint32_t>& out);
void render_background_preview(const Background& bg, int w, int h, std::vector<uint32_t>& out);
