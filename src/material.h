#pragma once

#include <vector>

#include "vec3.h"

// A stop in a color ramp: `pos` in [0,1]. Used radially across a ring
// (inner->outer) and to color a sphere's procedural surface field.
struct ColorStop {
    float pos = 0.0f;
    Vec3 color{1, 1, 1};
};

// Sample a color ramp at t in [0,1]. Robust to unsorted stops: lerps between the
// nearest stop at-or-below t and the nearest at-or-above it. `stops` must be
// non-empty.
inline Vec3 sample_color_ramp(const std::vector<ColorStop>& stops, float t) {
    const ColorStop* lo = nullptr;
    const ColorStop* hi = nullptr;
    for (const auto& s : stops) {
        if (s.pos <= t && (!lo || s.pos > lo->pos)) lo = &s;
        if (s.pos >= t && (!hi || s.pos < hi->pos)) hi = &s;
    }
    if (!lo) return hi->color;  // t before the first stop
    if (!hi) return lo->color;  // t after the last stop
    float span = hi->pos - lo->pos;
    if (span <= 1e-6f) return lo->color;
    float f = (t - lo->pos) / span;
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

// Phase 1 / 3.4: Minimal material — no PBR, just what the pixel-art shading needs.
// Phase 9: an optional procedural surface pattern that mixes `albedo` with
// `detail` across the sphere, so self-rotation becomes visible.
struct Material {
    Vec3 albedo;
    bool emissive = false;  // if true, shading ignores lighting/shadows (sun, self-lit bodies)

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
    float warp = 0.0f;               // domain-warp amount (swirly bands)
    std::vector<ColorStop> tex_ramp; // colors the field maps through

    Atmosphere atmosphere;           // Stage 6: faked limb glow
};
