#include <algorithm>
#include <cmath>

#include "sphere.h"

namespace {
constexpr float PI = 3.14159265358979323846f;

// Deterministic 3D hash -> [0,1] at integer lattice points.
float hash31(float x, float y, float z) {
    float h = std::sin(x * 127.1f + y * 311.7f + z * 74.7f) * 43758.5453f;
    return h - std::floor(h);
}

// Smooth 3D value noise in [0,1] (trilinear interpolation of lattice hashes).
// Sampled in object space, so it has no seam and no pole pinching on a sphere.
float value_noise3(Vec3 p) {
    float xi = std::floor(p.x), yi = std::floor(p.y), zi = std::floor(p.z);
    float xf = p.x - xi, yf = p.y - yi, zf = p.z - zi;
    float sx = xf * xf * (3.0f - 2.0f * xf);
    float sy = yf * yf * (3.0f - 2.0f * yf);
    float sz = zf * zf * (3.0f - 2.0f * zf);
    auto corner = [&](float dx, float dy, float dz) {
        return hash31(xi + dx, yi + dy, zi + dz);
    };
    float x00 = corner(0, 0, 0) + (corner(1, 0, 0) - corner(0, 0, 0)) * sx;
    float x10 = corner(0, 1, 0) + (corner(1, 1, 0) - corner(0, 1, 0)) * sx;
    float x01 = corner(0, 0, 1) + (corner(1, 0, 1) - corner(0, 0, 1)) * sx;
    float x11 = corner(0, 1, 1) + (corner(1, 1, 1) - corner(0, 1, 1)) * sx;
    float y0 = x00 + (x10 - x00) * sy;
    float y1 = x01 + (x11 - x01) * sy;
    return y0 + (y1 - y0) * sz;
}

// Layered fbm in [0,1].
float fbm3(Vec3 p, int octaves) {
    float sum = 0.0f, amp = 0.5f, tot = 0.0f;
    for (int i = 0; i < std::max(1, octaves); ++i) {
        sum += amp * value_noise3(p);
        tot += amp;
        p = p * 2.0f;
        amp *= 0.5f;
    }
    return (tot > 0.0f) ? sum / tot : 0.0f;
}
}  // namespace

float surface_field(const Material& m, Vec3 p) {
    float n = fbm3(p * m.noise_scale, m.noise_octaves);  // [0,1]
    if (m.band_strength <= 0.0f) return n;
    // Latitude bands (gas giants): sine of latitude, optionally warped by noise
    // for swirly bands. p is a unit vector, so p.y is the sine of latitude.
    float lat = p.y + (n - 0.5f) * m.warp;
    float phase = lat * m.band_freq * PI;
    // band_var bunches/spreads the stripes with latitude so widths vary (a low-
    // frequency phase wobble) instead of a perfectly even comb.
    phase += m.band_var * std::sin(lat * 2.0f * PI);
    float bands = 0.5f + 0.5f * std::sin(phase);
    return n * (1.0f - m.band_strength) + bands * m.band_strength;
}

Vec3 terrain_color(const Material& m, Vec3 p, float season_swing) {
    const Terrain& t = m.terrain;
    float e = surface_field(m, p);  // elevation [0,1] (fbm; band_strength stays 0)
    Vec3 base;
    if (e < t.sea_level) {
        // Ocean: darken toward 0 elevation so deep water reads deeper than coast.
        float depth = (t.sea_level > 1e-4f) ? e / t.sea_level : 1.0f;  // 0 deep .. 1 shore
        base = t.ocean * (0.5f + 0.5f * depth);
    } else {
        // Land height remapped to [0,1] and colored by the ramp (low->high).
        float land = (e - t.sea_level) / std::max(1e-4f, 1.0f - t.sea_level);
        base = surface_color(m, land);
    }
    // Polar ice caps: |p.y| is the sine of latitude. Perturb the test by elevation
    // noise so the cap rim is ragged, not a clean parallel; season_swing moves it.
    float edge = t.cap + season_swing;
    if (edge < 1.0f) {
        float ragged = std::fabs(p.y) + (e - 0.5f) * 0.15f;
        float ice = std::clamp((ragged - edge) / 0.08f, 0.0f, 1.0f);
        base = base * (1.0f - ice) + t.cap_color * ice;
    }
    return base;
}

Vec3 surface_color(const Material& m, float field) {
    field = std::clamp(field, 0.0f, 1.0f);
    if (!m.tex_ramp.empty()) return sample_color_ramp(m.tex_ramp, field);
    return m.albedo * (1.0f - field) + m.detail * field;
}

// Phase 2 / 4.2: ray-sphere intersection. Solve at^2 + bt + c = 0 and return the
// nearest root in (t_min, t_max). Uses the half-b form to save a couple of mults.
bool Sphere::hit(const Ray& r, float t_min, float t_max, HitRecord& rec) const {
    Vec3 oc = r.origin - center;
    float a = dot(r.direction, r.direction);
    float half_b = dot(oc, r.direction);
    float c = dot(oc, oc) - radius * radius;

    float discriminant = half_b * half_b - a * c;
    if (discriminant < 0.0f) return false;
    float sqrtd = std::sqrt(discriminant);

    // Nearest root in range; try the far root if the near one is out of bounds.
    float root = (-half_b - sqrtd) / a;
    if (root <= t_min || root >= t_max) {
        root = (-half_b + sqrtd) / a;
        if (root <= t_min || root >= t_max) return false;
    }

    rec.t = root;
    rec.p = r.point_at_parameter(root);
    rec.normal = normalize(rec.p - center);  // outward normal
    rec.material = material;

    // Phase 9 / Stage 4: procedural surface texture. Sample 3D object-space noise
    // at the un-spun body-local point (no pole pinch), so it rotates with spin.
    if (material.pattern != 0) {
        Vec3 ln = rotate_about(rec.normal, spin_axis, -spin_angle);
        rec.material.albedo = material.terrain.enabled
            ? terrain_color(material, ln, season_swing)
            : surface_color(material, surface_field(material, ln));
    }
    // Cloud layer: a second translucent fbm texture composited over the base.
    if (material.clouds.enabled) {
        const Clouds& cl = material.clouds;
        // Clouds drift relative to the surface, so use their own baked angle.
        Vec3 ln = rotate_about(rec.normal, spin_axis, -cloud_angle);
        float f = std::clamp(fbm3(ln * cl.scale, cl.octaves) + (cl.coverage - 0.5f),
                             0.0f, 1.0f);
        float a;
        Vec3 cc;
        if (cl.ramp.empty()) { cc = Vec3(1, 1, 1); a = f; }
        else cc = sample_color_ramp(cl.ramp, f, &a);
        a = std::clamp(a * cl.opacity, 0.0f, 1.0f);
        rec.material.albedo = rec.material.albedo * (1.0f - a) + cc * a;
    }
    return true;
}
