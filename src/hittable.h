#pragma once

#include <memory>
#include <string>

#include "vec3.h"
#include "ray.h"
#include "material.h"

// Phase 1 / 3.3: Intersection interface.
struct HitRecord {
    float t = 0.0f;
    Vec3 p;          // intersection point
    Vec3 normal;     // surface normal at p
    // The material is referenced, not copied (it lives in the posed shape for the
    // duration of the render) — copying it per hit meant deep-copying its color-ramp
    // vectors on every primary and shadow ray. `albedo` is the final surface color,
    // i.e. material->albedo after any procedural texture/ramp sampling.
    const Material* material = nullptr;
    Vec3 albedo;
};

class Hittable {
public:
    virtual ~Hittable() = default;

    // Returns true and fills `rec` if the ray hits within (t_min, t_max). When
    // `shading` is false (shadow/occlusion rays) the expensive procedural surface
    // texture is skipped — only geometry + the material pointer are filled.
    virtual bool hit(const Ray& r, float t_min, float t_max, HitRecord& rec,
                     bool shading = true) const = 0;

    // Deep copy preserving the dynamic type — used to pose a body at a given time
    // without mutating the authored scene (Phase 6).
    virtual std::shared_ptr<Hittable> clone() const = 0;

    // World-space position of the body. Lifted to the base so motion code can pose
    // any primitive generically. For orbiting bodies it is overwritten each frame.
    Vec3 center;

    // Display label for the editor / serialization (5.1). Not used by shading.
    std::string name;
};
