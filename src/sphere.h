#pragma once

#include "hittable.h"

// Phase 1 / 3.3: Sphere primitive (planets, moons — distinguished only by material/label).
// `center` is inherited from Hittable.
class Sphere : public Hittable {
public:
    float radius = 1.0f;
    Material material;

    // Phase 9: spin orientation baked in at pose time (world_at_time), so the
    // texture in hit() can be sampled in body-local space and appears to rotate.
    Vec3 spin_axis{0, 1, 0};
    float spin_angle = 0.0f;
    // Time-driven texture state, also baked at pose time: clouds rotate at
    // spin_angle + their drift; season_swing oscillates the polar cap edge;
    // band_angle drifts the gas-giant band layer (spin_angle + band_drift).
    float cloud_angle = 0.0f;
    float season_swing = 0.0f;
    float band_angle = 0.0f;

    Sphere() = default;
    Sphere(Vec3 c, float radius, Material material)
        : radius(radius), material(material) { center = c; }

    bool hit(const Ray& r, float t_min, float t_max, HitRecord& rec) const override;
    std::shared_ptr<Hittable> clone() const override {
        return std::make_shared<Sphere>(*this);
    }
};

// Stage 4: procedural surface texture, sampled in 3D object space (no pole
// pinching). `surface_field` returns a scalar in [0,1] at a unit body-local
// point; `surface_color` maps that field to an albedo via the material's
// tex_ramp (or albedo<->detail when the ramp is empty). Exposed for testing.
float surface_field(const Material& m, Vec3 local_unit);
Vec3 surface_color(const Material& m, float field);

// Rocky terrain albedo at a unit body-local point: ocean below `sea_level`, land
// (via tex_ramp) above, with seasonal polar ice caps. `season_swing` shifts the
// cap edge (baked from time in world_at_time). Exposed for testing.
Vec3 terrain_color(const Material& m, Vec3 local_unit, float season_swing);

// Gas-giant storm coverage in [0,1] at a unit body-local point: a few oval
// vortices placed from `storm_seed`, elongated east-west. 0 when storm <= 0.
float storm_weight(const Material& m, Vec3 local_unit);

// Crater shading multiplier (~0.2..1.4) at a unit body-local point: cellular bowls
// with dark floors + bright rims, 1 on the plains between craters. Returns 1 when
// craters.enabled is false. Multiplied onto the base albedo. Exposed for testing.
float crater_shade(const Craters& c, Vec3 local_unit);
