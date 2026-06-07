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

// Deterministic 1D hash -> [0,1] (storm placement).
float hash11(float x) {
    float h = std::sin(x * 91.37f + 13.1f) * 43758.5453f;
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
    // Gas-giant belts. p is a unit vector, so p.y is the sine of latitude. Build a
    // belt COORDINATE from latitude (warped by noise for swirl); band_var distorts
    // the latitude->belt spacing so belt WIDTHS vary (steeper local slope = narrower
    // belt) instead of an even comb.
    float lat = p.y + (n - 0.5f) * m.warp;
    float coord = lat * m.band_freq +
                  m.band_var * std::sin(lat * m.band_freq * 0.7f + 1.7f);
    // Zonal filaments: high-freq fbm stretched east-west (latitude compressed) wiggles
    // the belt boundaries and adds texture aligned with the belts.
    if (m.turbulence > 0.0f) {
        Vec3 q(p.x, p.y * 3.0f, p.z);
        coord += (fbm3(q * m.noise_scale * 2.5f, 3) - 0.5f) * m.turbulence;
    }
    // Each belt samples its OWN position along the ramp (the "line") via a per-belt
    // hash, so consecutive belts are distinct colors drawn from the whole ramp rather
    // than alternating two anchors. band_levels (>1) snaps belts to a limited set of
    // ramp positions; 0 = each belt anywhere on the line.
    float belt = std::floor(coord);
    float c = hash11(belt * 1.37f + 0.5f);
    if (m.band_levels > 1) {
        int b = std::min(m.band_levels - 1, static_cast<int>(c * m.band_levels));
        c = static_cast<float>(b) / (m.band_levels - 1);
    }
    return n * (1.0f - m.band_strength) + c * m.band_strength;
}

float storm_weight(const Material& m, Vec3 p) {
    if (m.storm <= 0.0f) return 0.0f;
    float best = 0.0f;
    float lon_p = std::atan2(p.z, p.x);
    // A small fixed set of candidate ovals; the first is always present (the "great
    // red spot"), the rest appear as storm -> 1, so seeds give 1..3 storms.
    for (int k = 0; k < 3; ++k) {
        float s = static_cast<float>(m.storm_seed) + k * 17.0f;
        if (k > 0 && hash11(s + 7.1f) > m.storm) continue;
        // Primary storm (k=0, the "great red spot") sits at a mid-latitude and is
        // bigger; secondary ovals are smaller and may sit anywhere.
        float latspread = (k == 0) ? 0.9f : 1.4f;
        float sz = (k == 0) ? 1.7f : 1.0f;
        float cy = (hash11(s) - 0.5f) * latspread;     // latitude center
        float clon = hash11(s + 3.3f) * 2.0f * PI;     // longitude center
        float dlat = p.y - cy;
        float dlon = lon_p - clon;
        dlon = std::atan2(std::sin(dlon), std::cos(dlon));  // wrap to [-PI, PI]
        float rlat = (0.08f + 0.05f * hash11(s + 1.7f)) * sz;  // half-height in latitude
        float rlon = (0.28f + 0.30f * hash11(s + 5.5f)) * sz;  // half-width, wider (oval)
        float d = std::sqrt((dlat / rlat) * (dlat / rlat) +
                            (dlon / rlon) * (dlon / rlon));
        float w = std::clamp(1.0f - d, 0.0f, 1.0f);
        w = w * w * (3.0f - 2.0f * w);  // smoothstep falloff
        best = std::max(best, w);
    }
    return best * std::clamp(m.storm, 0.0f, 1.0f);
}

float crater_shade(const Craters& c, Vec3 p) {
    if (!c.enabled) return 1.0f;
    // Cellular (Worley) layout: scale the point into a grid, then over the 3x3x3
    // neighbourhood find the nearest jittered feature point that actually holds a
    // crater. p is a unit vector, so this tiles the sphere with no seam/pole pinch.
    Vec3 q = p * c.density + Vec3(static_cast<float>(c.seed) * 0.123f, 0.0f, 0.0f);
    float xi = std::floor(q.x), yi = std::floor(q.y), zi = std::floor(q.z);
    float xf = q.x - xi, yf = q.y - yi, zf = q.z - zi;
    float best_d = 1e9f, best_r = 0.0f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                float cx = xi + dx, cy = yi + dy, cz = zi + dz;
                if (hash31(cx + 3.7f, cy + 9.1f, cz + 13.3f) < 0.45f) continue;  // empty cell
                float jx = hash31(cx, cy, cz);
                float jy = hash31(cx + 31.4f, cy + 17.7f, cz + 5.3f);
                float jz = hash31(cx + 11.1f, cy + 47.3f, cz + 23.9f);
                float fx = dx + jx - xf, fy = dy + jy - yf, fz = dz + jz - zf;
                float d = std::sqrt(fx * fx + fy * fy + fz * fz);
                if (d < best_d) {
                    best_d = d;
                    best_r = 0.25f + 0.2f * hash31(cx + 1.3f, cy + 2.7f, cz + 3.9f);  // radius
                }
            }
    if (best_r <= 0.0f || best_d > best_r) return 1.0f;  // plains between craters
    float t = best_d / best_r;  // 0 at the centre .. 1 at the rim
    // Bowl: darken the floor, brighten a thin raised rim just inside the edge.
    float floor_dark = -0.5f * (1.0f - t);
    float u = (t - 0.88f) / 0.10f;
    float rim = std::exp(-u * u);
    return std::clamp(1.0f + c.strength * (floor_dark + 0.6f * rim), 0.2f, 1.4f);
}

Vec3 terrain_color(const Material& m, Vec3 p, float season_swing) {
    const Terrain& t = m.terrain;
    // Posterize a [0,1] value into `t.levels` flat steps so colored regions get crisp
    // boundaries instead of a smooth (mushy) gradient. n<=1 leaves it continuous.
    auto step = [&](float x, int n) {
        if (n <= 1) return x;
        int b = std::min(n - 1, static_cast<int>(x * n));
        return static_cast<float>(b) / (n - 1);
    };
    float e = surface_field(m, p);  // elevation [0,1] (fbm; band_strength stays 0)
    Vec3 base;
    if (e < t.sea_level) {
        // Ocean: darken toward 0 elevation so deep water reads deeper than coast.
        float depth = (t.sea_level > 1e-4f) ? e / t.sea_level : 1.0f;  // 0 deep .. 1 shore
        if (t.levels > 1) depth = step(depth, 2);  // shelf + deep, crisp coast
        base = t.ocean * (0.5f + 0.5f * depth);
    } else {
        // Land height remapped to [0,1], posterized, then colored by the ramp.
        float land = (e - t.sea_level) / std::max(1e-4f, 1.0f - t.sea_level);
        base = surface_color(m, step(land, t.levels));
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
        if (material.terrain.enabled) {
            rec.material.albedo = terrain_color(material, ln, season_swing);
        } else {
            // Gas-giant bands sample at a (possibly) drifting angle so belts/storms
            // slide slowly over time; rocky surfaces use the plain spin angle.
            Vec3 bn = (material.band_drift != 0.0f)
                          ? rotate_about(rec.normal, spin_axis, -band_angle)
                          : ln;
            rec.material.albedo = surface_color(material, surface_field(material, bn));
            // Gas-giant storms: oval vortices blended over the banded surface.
            if (material.storm > 0.0f) {
                float w = storm_weight(material, bn);
                rec.material.albedo = rec.material.albedo * (1.0f - w) + material.storm_color * w;
            }
        }
        // Impact craters (airless rocky bodies): bowl shading over the base albedo.
        if (material.craters.enabled)
            rec.material.albedo = rec.material.albedo * crater_shade(material.craters, ln);
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
