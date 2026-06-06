#include <algorithm>
#include <cstdint>

#include "render.h"
#include "scene.h"

bool hit_world(const World& world, const Ray& r, float t_min, float t_max, HitRecord& rec) {
    bool hit_anything = false;
    float closest = t_max;
    HitRecord temp;
    for (const auto& obj : world) {
        if (obj->hit(r, t_min, closest, temp)) {
            hit_anything = true;
            closest = temp.t;
            rec = temp;
        }
    }
    return hit_anything;
}

Vec3 background(const Ray& r) {
    (void)r;
    return Vec3(0.01f, 0.01f, 0.02f);  // near-black space (4.4)
}

Vec3 ray_color(const Ray& r, const World& world, Vec3 light_dir) {
    constexpr float EPS = 1e-3f;       // shadow-acne offset (4.3)
    constexpr float AMBIENT = 0.15f;   // background fill so shadows aren't pure black (4.3)

    HitRecord rec;
    if (!hit_world(world, r, EPS, 1e30f, rec))
        return background(r);

    // Emissive surfaces ignore lighting/shadows (sun, self-lit bodies). (3.4)
    if (rec.material.emissive)
        return rec.material.albedo;

    // Direction from the surface toward the light. light_dir is the travel
    // direction (sun -> scene), so we negate it.
    Vec3 to_light = normalize(-light_dir);
    float diffuse = std::max(0.0f, dot(rec.normal, to_light));

    // Shadow ray toward the light; any hit puts the point in shadow (directional
    // light has no distance to clamp against). (4.3)
    Ray shadow(rec.p + rec.normal * EPS, to_light);
    HitRecord occ;
    float direct = hit_world(world, shadow, EPS, 1e30f, occ) ? 0.0f : diffuse;

    // Ambient floor + direct diffuse, kept within [AMBIENT, 1].
    float intensity = AMBIENT + (1.0f - AMBIENT) * direct;
    return rec.material.albedo * intensity;
}

namespace {
// Pack a linear color into 0xAABBGGRR (little-endian byte order R,G,B,A).
uint32_t pack_rgba(Vec3 c) {
    auto to_byte = [](float v) {
        v = std::min(1.0f, std::max(0.0f, v));
        return static_cast<uint32_t>(v * 255.0f + 0.5f);
    };
    return to_byte(c.x) | (to_byte(c.y) << 8) | (to_byte(c.z) << 16) | (0xFFu << 24);
}
}  // namespace

void render_view(const Scene& scene, const Camera& cam, float time,
                 int w, int h, std::vector<uint32_t>& out) {
    out.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

    const World world = world_at_time(scene, time);  // pose orbits/spin (Phase 6)
    for (int y = 0; y < h; ++y) {
        // Row 0 is the top; camera v runs bottom (0) -> top (1), so flip y.
        float v = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            Vec3 color = ray_color(cam.get_ray(u, v), world, scene.light_dir);
            out[static_cast<size_t>(y) * w + x] = pack_rgba(color);
        }
    }
}

void render_scene(const Scene& scene, float time, std::vector<uint32_t>& out) {
    render_view(scene, scene_camera(scene, time, scene.aspect_ratio()), time,
                scene.width, scene.height, out);
}
