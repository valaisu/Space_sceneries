#pragma once

#include <vector>

#include "vec3.h"

// A stop in a color ramp: `pos` in [0,1]. Used radially across a ring
// (inner->outer) and to color a sphere's procedural surface field.
struct ColorStop {
    float pos = 0.0f;
    Vec3 color{1, 1, 1};
    float alpha = 1.0f;  // per-stop opacity (used by clouds / translucent ramps)
};

// Sample a color ramp at t in [0,1]. Robust to unsorted stops: lerps between the
// nearest stop at-or-below t and the nearest at-or-above it. `stops` must be
// non-empty. If `out_alpha` is given, the interpolated alpha is written to it.
inline Vec3 sample_color_ramp(const std::vector<ColorStop>& stops, float t,
                              float* out_alpha = nullptr) {
    const ColorStop* lo = nullptr;
    const ColorStop* hi = nullptr;
    for (const auto& s : stops) {
        if (s.pos <= t && (!lo || s.pos > lo->pos)) lo = &s;
        if (s.pos >= t && (!hi || s.pos < hi->pos)) hi = &s;
    }
    if (!lo) { if (out_alpha) *out_alpha = hi->alpha; return hi->color; }  // before first
    if (!hi) { if (out_alpha) *out_alpha = lo->alpha; return lo->color; }  // after last
    float span = hi->pos - lo->pos;
    if (span <= 1e-6f) { if (out_alpha) *out_alpha = lo->alpha; return lo->color; }
    float f = (t - lo->pos) / span;
    if (out_alpha) *out_alpha = lo->alpha * (1.0f - f) + hi->alpha * f;
    return lo->color * (1.0f - f) + hi->color * f;
}

// Stage 6: faked atmosphere — an additive limb glow keyed off the view-grazing
// angle and the sun direction. No transparency / no second ray.
struct Atmosphere {
    bool enabled = false;
    Vec3 color{0.4f, 0.6f, 1.0f};   // day-side rim color (sky)
    Vec3 sunset{1.0f, 0.5f, 0.2f};  // tint near the terminator (sun grazing)
    float thickness = 0.4f;         // rim width: 0 = thin sliver .. 1 = broad glow
    float intensity = 1.0f;         // overall strength
};

// A second, translucent texture layer for spheres — fbm cloud cover composited
// over the base surface at the same hit point (no extra ray). The fbm field maps
// through `ramp` (color + per-stop alpha); an empty ramp = white, alpha = field.
struct Clouds {
    bool  enabled = false;
    float scale = 4.0f;          // fbm frequency (independent of the base texture)
    int   octaves = 4;
    float coverage = 0.5f;       // biases the field: <0.5 sparse, >0.5 more cloud
    float opacity = 1.0f;        // overall alpha multiplier
    float drift = 0.0f;          // extra revolutions/time relative to surface spin (0 = locked)
    std::vector<ColorStop> ramp; // field -> (color, alpha)
};

// Rocky-planet surface model: an elevation fbm thresholded into ocean vs. land,
// with seasonal polar ice caps. When enabled it replaces the scalar field->ramp
// path for the base albedo (gas giants keep using latitude bands instead). Land
// color comes from the material's `tex_ramp`, sampled by land height (sea_level..1
// remapped to 0..1) — so deserts/forests/mountains come from the ramp choice.
struct Terrain {
    bool  enabled = false;
    float sea_level = 0.5f;            // elevation threshold: below = ocean
    Vec3  ocean{0.10f, 0.22f, 0.45f};  // shoreline water (deepens toward 0 elevation)
    int   levels = 0;                  // 0 = smooth gradient; >1 = posterize land into
                                       // that many flat bands with crisp boundaries
    float cap = 0.85f;                 // |latitude| above this -> ice (>=1 = no cap)
    Vec3  cap_color{0.92f, 0.96f, 1.0f};
    float cap_season = 0.0f;           // seasonal cap-edge swing amplitude (0 = static)
    float season_period = 0.0f;        // time for one season cycle (0 = static)
};

// Phase 1 / 3.4: Minimal material — no PBR, just what the pixel-art shading needs.
// Phase 9: an optional procedural surface pattern that mixes `albedo` with
// `detail` across the sphere, so self-rotation becomes visible.
struct Material {
    Vec3 albedo;
    bool emissive = false;  // if true, shading ignores lighting/shadows (sun, self-lit bodies)
    bool two_sided = false; // if true, lit when the sun hits either face (rings read as translucent)

    int pattern = 0;        // 0 = solid (texture off); nonzero = procedural texture on
    Vec3 detail{0, 0, 0};   // fallback mix color when tex_ramp is empty

    // Radial color ramp for disks/rings (Saturn-style bands), sampled
    // inner->outer. Empty = solid `albedo`.
    std::vector<ColorStop> ring_ramp;

    // Stage 4: 3D object-space procedural surface texture (spheres). The field is
    // layered fbm noise, optionally blended toward latitude bands (gas giants);
    // it maps through `tex_ramp` (or albedo<->detail when the ramp is empty).
    float noise_scale = 3.0f;        // base fbm frequency
    int   noise_octaves = 4;         // number of fbm layers
    float band_strength = 0.0f;      // 0 = isotropic mottle .. 1 = pure latitude bands
    float band_freq = 6.0f;          // number of latitude bands
    float band_var = 0.0f;           // unevenness of band widths (0 = uniform stripes)
    float warp = 0.0f;               // domain-warp amount (swirly bands)
    int   band_levels = 0;           // 0/1 = smooth bands; >1 = posterize into flat belts
    float turbulence = 0.0f;         // intra-belt zonal filament texture (0 = none)
    std::vector<ColorStop> tex_ramp; // colors the field maps through

    // Gas-giant storms: a few oval vortices (a guaranteed "great red spot" plus
    // hash-placed smaller ovals), elongated east-west, blended toward storm_color.
    float storm = 0.0f;              // overall storm strength/coverage (0 = none)
    int   storm_seed = 0;            // placement seed
    Vec3  storm_color{0.75f, 0.3f, 0.2f};

    Terrain terrain;                 // rocky planets: ocean/land + polar caps
    Atmosphere atmosphere;           // Stage 6: faked limb glow
    Clouds clouds;                   // second translucent texture layer (spheres)
};
