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
    Material material;
};

class Hittable {
public:
    virtual ~Hittable() = default;

    // Returns true and fills `rec` if the ray hits within (t_min, t_max).
    // Concrete intersection math is implemented in Phase 2 (4.2).
    virtual bool hit(const Ray& r, float t_min, float t_max, HitRecord& rec) const = 0;

    // Deep copy preserving the dynamic type — used to pose a body at a given time
    // without mutating the authored scene (Phase 6).
    virtual std::shared_ptr<Hittable> clone() const = 0;

    // World-space position of the body. Lifted to the base so motion code can pose
    // any primitive generically. For orbiting bodies it is overwritten each frame.
    Vec3 center;

    // Display label for the editor / serialization (5.1). Not used by shading.
    std::string name;
};
