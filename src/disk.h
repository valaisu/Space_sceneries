#pragma once

#include "hittable.h"

// Phase 1 / 3.3: Disk primitive (planetary rings). Infinitely thin.
// `center` is inherited from Hittable.
class Disk : public Hittable {
public:
    Vec3 normal;
    float inner_radius = 0.0f;
    float outer_radius = 1.0f;
    Material material;

    Disk() = default;
    Disk(Vec3 c, Vec3 normal, float inner_radius, float outer_radius, Material material)
        : normal(normal),
          inner_radius(inner_radius), outer_radius(outer_radius), material(material) { center = c; }

    bool hit(const Ray& r, float t_min, float t_max, HitRecord& rec,
             bool shading = true) const override;
    std::shared_ptr<Hittable> clone() const override {
        return std::make_shared<Disk>(*this);
    }
};
