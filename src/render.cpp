#include <algorithm>
#include <cmath>
#include <cstdint>

#include "render.h"
#include "scene.h"
#include "sphere.h"

namespace {
float fract(float x) { return x - std::floor(x); }
Vec3 vfract(Vec3 v) { return Vec3(fract(v.x), fract(v.y), fract(v.z)); }
Vec3 vfloor(Vec3 v) {
    return Vec3(std::floor(v.x), std::floor(v.y), std::floor(v.z));
}

// Dave Hoskins' hash33: deterministic vec3 -> vec3 in [0,1]^3, no trig.
Vec3 hash33(Vec3 p) {
    p = vfract(Vec3(p.x * 0.1031f, p.y * 0.1030f, p.z * 0.0973f));
    float d = dot(p, Vec3(p.y, p.x, p.z) + Vec3(33.33f, 33.33f, 33.33f));
    p = p + Vec3(d, d, d);
    return vfract(Vec3((p.x + p.y) * p.z, (p.x + p.x) * p.y, (p.y + p.z) * p.x));
}

// Trilinear value noise + a small fbm over a direction, for large-scale star-
// density regions (darker patches / a denser band, not true nebulae).
float hash1(Vec3 p) { return hash33(p).x; }
float vnoise(Vec3 p) {
    Vec3 i = vfloor(p);
    Vec3 f = p - i;
    Vec3 u(f.x * f.x * (3 - 2 * f.x), f.y * f.y * (3 - 2 * f.y), f.z * f.z * (3 - 2 * f.z));
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    float x00 = L(hash1(i + Vec3(0,0,0)), hash1(i + Vec3(1,0,0)), u.x);
    float x10 = L(hash1(i + Vec3(0,1,0)), hash1(i + Vec3(1,1,0)), u.x);
    float x01 = L(hash1(i + Vec3(0,0,1)), hash1(i + Vec3(1,0,1)), u.x);
    float x11 = L(hash1(i + Vec3(0,1,1)), hash1(i + Vec3(1,1,1)), u.x);
    return L(L(x00, x10, u.y), L(x01, x11, u.y), u.z);
}
float fbm2(Vec3 p) {
    return 0.6667f * vnoise(p) + 0.3333f * vnoise(p * 2.03f + Vec3(11, 17, 23));
}
}  // namespace

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

namespace {
float smoothstep01(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}  // namespace

Vec3 atmosphere_glow(const Atmosphere& a, Vec3 normal, Vec3 view_dir, Vec3 to_light) {
    // Fresnel-like rim: bright at the silhouette (normal perpendicular to view),
    // zero face-on. `thickness` widens the rim by lowering the exponent.
    float ndv = std::max(0.0f, dot(normal, view_dir));
    float power = 1.0f + (1.0f - std::clamp(a.thickness, 0.0f, 1.0f)) * 6.0f;
    float rim = std::pow(1.0f - ndv, power);

    // Only the sun-lit side glows; fade through the terminator into the night side.
    float sun = dot(normal, to_light);
    float visibility = smoothstep01(-0.25f, 0.4f, sun);

    // Sunset tint peaks where the sun grazes (terminator, sun ~ 0).
    float sunset_w = std::clamp(1.0f - std::fabs(sun) * 3.0f, 0.0f, 1.0f);
    Vec3 col = a.color * (1.0f - sunset_w) + a.sunset * sunset_w;

    return col * (rim * visibility * a.intensity);
}

Vec3 background(const Ray& r, const Background& bg) {
    // Project the ray direction onto a 3D grid; each cell may hold one star.
    // Sampling by direction (not UV) avoids pole pinching.
    constexpr float FREQ = 60.0f;     // grid resolution; finer = smaller cells
    Vec3 dir = normalize(r.direction);
    Vec3 seed_off(bg.seed * 0.137f, bg.seed * 0.071f, bg.seed * 0.219f);
    Vec3 p = dir * FREQ + seed_off;
    Vec3 base = vfloor(p);

    // Large-scale regions modulate local star density (darker/denser areas).
    float region = fbm2(dir * std::max(0.05f, bg.region_scale) + seed_off * 0.01f);
    float dfactor = 1.0f + bg.region_strength * (region - 0.5f) * 2.0f;
    float local_density = std::clamp(bg.density * dfactor, 0.0f, 1.0f);
    float radius = std::max(0.01f, bg.size);

    // Scan the 3x3x3 neighborhood so a fully-jittered star near a cell edge isn't
    // clipped into an arc — the old single-cell sample produced visible grid rings.
    // Keep the brightest contributor so stars stay round points, not blobs.
    float best = 0.0f;
    Vec3 best_tint(0, 0, 0);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                Vec3 cell = base + Vec3(static_cast<float>(dx), static_cast<float>(dy),
                                        static_cast<float>(dz));
                Vec3 r1 = hash33(cell);
                if (r1.x >= local_density) continue;  // no star in this cell
                Vec3 r2 = hash33(cell + Vec3(13.1f, 47.3f, 7.7f));
                Vec3 star = cell + r2;  // full-cell jitter -> no lattice rows
                float dist = length(p - star);
                float core = std::max(0.0f, 1.0f - dist / radius);
                float intensity = core * core * bg.brightness * (0.4f + 0.6f * r1.y);
                if (intensity > best) {
                    best = intensity;
                    float cv = (r1.z - 0.5f) * 2.0f * bg.color_variation;
                    best_tint = Vec3(bg.tint.x + cv, bg.tint.y, bg.tint.z - cv);
                }
            }

    Vec3 col = bg.sky;
    if (bg.region_glow > 0.0f)  // faint additive haze in the densest regions
        col = col + bg.tint * (bg.region_glow * smoothstep01(0.55f, 1.0f, region));
    return col + best_tint * best;
}

Vec3 ray_color(const Ray& r, const World& world, const std::vector<Light>& lights,
               float ambient, bool shadows, const Background& bg) {
    constexpr float EPS = 1e-3f;       // shadow-acne offset (4.3)

    HitRecord rec;
    if (!hit_world(world, r, EPS, 1e30f, rec))
        return background(r, bg);

    // Emissive surfaces ignore lighting/shadows (sun, self-lit bodies). (3.4)
    if (rec.material.emissive)
        return rec.material.albedo;

    const Vec3 albedo = rec.material.albedo;
    Vec3 color = albedo * ambient;       // ambient floor so shadows aren't pure black
    const float scale = 1.0f - ambient;  // a single full-on light reaches full albedo

    for (const Light& l : lights) {
        Vec3 to_light;
        float max_dist;     // how far a shadow occluder counts
        float atten = 1.0f;
        if (l.directional) {
            to_light = normalize(-l.vec);  // l.vec is the travel direction
            max_dist = 1e30f;
        } else {
            Vec3 d = l.vec - rec.p;        // l.vec is the world position
            max_dist = length(d);
            to_light = (max_dist > 1e-6f) ? d / max_dist : Vec3(0, 1, 0);
            if (l.falloff) {
                // Soft inverse-square; the +1 keeps nearby lights from blowing out.
                atten = 1.0f / (1.0f + max_dist * max_dist);
            }
        }

        float diffuse = std::max(0.0f, dot(rec.normal, to_light));
        if (diffuse <= 0.0f) continue;

        if (shadows) {
            // Only opaque bodies cast shadows; emissive bodies (the suns, including
            // the light's own sphere at max_dist) are transparent to shadow rays.
            Ray shadow(rec.p + rec.normal * EPS, to_light);
            HitRecord occ;
            if (hit_world(world, shadow, EPS, max_dist - EPS, occ) && !occ.material.emissive)
                continue;
        }

        // Tint the albedo by the light color (component-wise), then weight it.
        Vec3 lit(albedo.x * l.color.x, albedo.y * l.color.y, albedo.z * l.color.z);
        color = color + lit * (diffuse * atten * scale);
    }

    // Stage 6: additive atmosphere limb glow, keyed off the main (first) light.
    if (rec.material.atmosphere.enabled && !lights.empty()) {
        Vec3 view_dir = normalize(-r.direction);
        const Light& main = lights[0];
        Vec3 to_light = main.directional ? normalize(-main.vec)
                                         : normalize(main.vec - rec.p);
        color = color + atmosphere_glow(rec.material.atmosphere, rec.normal,
                                        view_dir, to_light);
    }
    return color;
}

namespace {
// Stage 5: build the light set for a posed world under a display mode.
std::vector<Light> build_lights(const Scene& scene, const World& world, ShadeMode mode) {
    std::vector<Light> lights;
    auto first_sun = [&](Vec3& pos) -> bool {
        for (const auto& obj : world)
            if (auto s = std::dynamic_pointer_cast<Sphere>(obj))
                if (s->material.emissive) { pos = s->center; return true; }
        return false;
    };

    if (mode == ShadeMode::Lit) {
        for (const auto& obj : world)
            if (auto s = std::dynamic_pointer_cast<Sphere>(obj))
                if (s->material.emissive)
                    lights.push_back({false, s->center, s->material.albedo, scene.light_falloff});
        // Optional directional fill, plus a fallback so a sunless scene isn't flat.
        if (scene.fill_light || lights.empty())
            lights.push_back({true, scene.light_dir, Vec3(1, 1, 1), false});
    } else if (mode == ShadeMode::Direction) {
        lights.push_back({true, scene.light_dir, Vec3(1, 1, 1), false});
    } else {  // InBetween: a single directional light aimed from the main sun.
        Vec3 sun;
        Vec3 dir = first_sun(sun) ? (Vec3(0, 0, 0) - sun) : scene.light_dir;
        lights.push_back({true, dir, Vec3(1, 1, 1), false});
    }
    return lights;
}
}  // namespace

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
                 int w, int h, std::vector<uint32_t>& out, ShadeMode mode) {
    out.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

    const World world = world_at_time(scene, time);  // pose orbits/spin (Phase 6)
    const std::vector<Light> lights = build_lights(scene, world, mode);
    const bool shadows = (mode != ShadeMode::InBetween);  // In-between skips shadows
    constexpr float AMBIENT = 0.15f;
    for (int y = 0; y < h; ++y) {
        // Row 0 is the top; camera v runs bottom (0) -> top (1), so flip y.
        float v = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            Vec3 color = ray_color(cam.get_ray(u, v), world, lights, AMBIENT,
                                   shadows, scene.background);
            out[static_cast<size_t>(y) * w + x] = pack_rgba(color);
        }
    }

    // Stage 3: palette post-process (blur + quantize/dither), gated by enabled.
    apply_post(scene.post, w, h, out);
}

void render_scene(const Scene& scene, float time, std::vector<uint32_t>& out) {
    // Final render always uses full lighting (Lit), matching the export.
    render_view(scene, scene_camera(scene, time, scene.aspect_ratio()), time,
                scene.width, scene.height, out, ShadeMode::Lit);
}

void render_material_preview(const Material& m, int w, int h, std::vector<uint32_t>& out) {
    out.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    World world{std::make_shared<Sphere>(Vec3(0, 0, 0), 1.0f, m)};
    Camera cam(Vec3(0, 0, 3), Vec3(0, 0, 0), Vec3(0, 1, 0), 40.0f,
               static_cast<float>(w) / static_cast<float>(h));
    // One soft directional key from the upper-left gives form; a high ambient floor
    // keeps it neutral so the texture (not the lighting) is what you read.
    std::vector<Light> lights{{true, Vec3(-0.5f, -0.6f, -0.7f), Vec3(1, 1, 1), false}};
    Background bg;
    bg.density = 0.0f;
    bg.sky = Vec3(0.12f, 0.12f, 0.14f);  // flat neutral backdrop
    constexpr float AMBIENT = 0.3f;
    for (int y = 0; y < h; ++y) {
        float v = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            Vec3 c = ray_color(cam.get_ray(u, v), world, lights, AMBIENT, false, bg);
            out[static_cast<size_t>(y) * w + x] = pack_rgba(c);
        }
    }
}

void render_background_preview(const Background& bg, int w, int h, std::vector<uint32_t>& out) {
    out.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    Camera cam(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0), 70.0f,
               static_cast<float>(w) / static_cast<float>(h));
    for (int y = 0; y < h; ++y) {
        float v = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            out[static_cast<size_t>(y) * w + x] = pack_rgba(background(cam.get_ray(u, v), bg));
        }
    }
}
