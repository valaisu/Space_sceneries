#include <algorithm>
#include <cmath>

#include "sphere.h"

namespace {
constexpr float PI = 3.14159265358979323846f;

float hash21(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return h - std::floor(h);
}

// Smooth value noise in [0,1], tiling in x with period `xp` so it wraps seamlessly
// around the sphere's longitude (u = 0 and u = 1 are the same meridian).
float value_noise(float x, float y, int xp) {
    float xi = std::floor(x), yi = std::floor(y);
    float xf = x - xi, yf = y - yi;
    float sx = xf * xf * (3.0f - 2.0f * xf);
    float sy = yf * yf * (3.0f - 2.0f * yf);
    auto wrap = [xp](float c) {
        float m = std::fmod(c, static_cast<float>(xp));
        return m < 0 ? m + xp : m;
    };
    float x0 = wrap(xi), x1 = wrap(xi + 1);
    float a = hash21(x0, yi), b = hash21(x1, yi);
    float c = hash21(x0, yi + 1), d = hash21(x1, yi + 1);
    float top = a + (b - a) * sx, bot = c + (d - c) * sx;
    return top + (bot - top) * sy;
}

// Pattern mix factor in [0,1] for spherical coords (u = longitude, v = latitude).
float pattern_factor(int pattern, float u, float v) {
    if (pattern == 1) return 0.5f + 0.5f * std::sin(u * 2.0f * PI * 8.0f);  // stripes
    return value_noise(u * 8.0f, v * 8.0f, 8);                             // mottled
}
}  // namespace

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

    // Phase 9: procedural surface texture. Sample in body-local space (un-spun)
    // so the pattern rotates with the body's spin over time.
    if (material.pattern != 0) {
        Vec3 ln = rotate_about(rec.normal, spin_axis, -spin_angle);
        float u = 0.5f + std::atan2(ln.z, ln.x) / (2.0f * PI);
        float v = 0.5f - std::asin(std::clamp(ln.y, -1.0f, 1.0f)) / PI;
        float f = pattern_factor(material.pattern, u, v);
        rec.material.albedo = material.albedo * (1.0f - f) + material.detail * f;
    }
    return true;
}
