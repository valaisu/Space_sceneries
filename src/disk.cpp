#include <algorithm>
#include <cmath>

#include "disk.h"

// Phase 2 / 4.2: ray-disk intersection. Hit the infinite plane, then accept only if
// the hit point's distance from center is within [inner_radius, outer_radius].
// Infinitely thin, so it can shimmer at grazing angles (see 4.2 note) — acceptable
// for the 400x200 target.
bool Disk::hit(const Ray& r, float t_min, float t_max, HitRecord& rec,
               bool shading) const {
    float denom = dot(normal, r.direction);
    if (std::fabs(denom) < 1e-8f) return false;  // ray parallel to the plane

    float t = dot(center - r.origin, normal) / denom;
    if (t <= t_min || t >= t_max) return false;

    Vec3 p = r.point_at_parameter(t);
    float dist2 = dot(p - center, p - center);
    if (dist2 < inner_radius * inner_radius || dist2 > outer_radius * outer_radius)
        return false;

    rec.t = t;
    rec.p = p;
    // Orient the normal to face the incoming ray so both sides shade.
    rec.normal = (denom < 0.0f) ? normal : -normal;
    rec.material = &material;
    rec.albedo = material.albedo;

    // Radial color ramp (Saturn-style bands), sampled inner->outer. Shadow rays skip it.
    if (shading && !material.ring_ramp.empty() && outer_radius > inner_radius) {
        float radial = (std::sqrt(dist2) - inner_radius) /
                       (outer_radius - inner_radius);
        rec.albedo = sample_color_ramp(material.ring_ramp,
                                       std::clamp(radial, 0.0f, 1.0f));
    }
    return true;
}
