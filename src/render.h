#pragma once

#include <memory>
#include <vector>

#include "vec3.h"
#include "ray.h"
#include "hittable.h"

// Phase 2: shading core. A "world" is just a list of hittables for now; the full
// Scene class (camera + light + serialization) arrives in Phase 3.
using World = std::vector<std::shared_ptr<Hittable>>;

// Nearest hit across the world within (t_min, t_max).
bool hit_world(const World& world, const Ray& r, float t_min, float t_max, HitRecord& rec);

// Phase 2 / 4.4: color for a ray that hits nothing (near-black space).
Vec3 background(const Ray& r);

// Phase 2 / 4.3: diffuse shading + hard shadows for a primary ray.
// `light_dir` is the direction the light TRAVELS (sun -> scene); it need not be
// normalized. See README/spec note on the lighting convention.
Vec3 ray_color(const Ray& r, const World& world, Vec3 light_dir);
