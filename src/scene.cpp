#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "scene.h"
#include "sphere.h"
#include "disk.h"

using nlohmann::json;

namespace {

json to_json(Vec3 v) { return json::array({v.x, v.y, v.z}); }

Vec3 vec3_from_json(const json& j) {
    return Vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
}

json to_json(const Material& m) {
    return json{{"albedo", to_json(m.albedo)}, {"emissive", m.emissive},
                {"pattern", m.pattern}, {"detail", to_json(m.detail)}};
}

Material material_from_json(const json& j) {
    Material m;
    m.albedo = vec3_from_json(j.at("albedo"));
    m.emissive = j.value("emissive", false);
    m.pattern = j.value("pattern", 0);
    if (j.contains("detail")) m.detail = vec3_from_json(j.at("detail"));
    return m;
}

json object_to_json(const std::shared_ptr<Hittable>& obj) {
    if (auto s = std::dynamic_pointer_cast<Sphere>(obj)) {
        return json{{"type", "sphere"}, {"name", s->name},
                    {"center", to_json(s->center)}, {"radius", s->radius},
                    {"material", to_json(s->material)}};
    }
    if (auto d = std::dynamic_pointer_cast<Disk>(obj)) {
        return json{{"type", "disk"}, {"name", d->name},
                    {"center", to_json(d->center)}, {"normal", to_json(d->normal)},
                    {"inner_radius", d->inner_radius}, {"outer_radius", d->outer_radius},
                    {"material", to_json(d->material)}};
    }
    throw std::runtime_error("scene_to_json: unknown Hittable type");
}

std::shared_ptr<Hittable> object_from_json(const json& j) {
    const std::string type = j.at("type").get<std::string>();
    std::shared_ptr<Hittable> obj;
    if (type == "sphere") {
        obj = std::make_shared<Sphere>(vec3_from_json(j.at("center")),
                                       j.at("radius").get<float>(),
                                       material_from_json(j.at("material")));
    } else if (type == "disk") {
        obj = std::make_shared<Disk>(vec3_from_json(j.at("center")),
                                     vec3_from_json(j.at("normal")),
                                     j.at("inner_radius").get<float>(),
                                     j.at("outer_radius").get<float>(),
                                     material_from_json(j.at("material")));
    } else {
        throw std::runtime_error("scene_from_json: unknown object type '" + type + "'");
    }
    obj->name = j.value("name", std::string{});
    return obj;
}

json to_json(const Orbit& o) {
    return json{{"active", o.active}, {"parent", o.parent}, {"radius", o.radius},
                {"period", o.period}, {"phase", o.phase}, {"normal", to_json(o.normal)}};
}

Orbit orbit_from_json(const json& j) {
    Orbit o;
    o.active = j.value("active", false);
    o.parent = j.value("parent", -1);
    o.radius = j.value("radius", 5.0f);
    o.period = j.value("period", 10.0f);
    o.phase = j.value("phase", 0.0f);
    if (j.contains("normal")) o.normal = vec3_from_json(j.at("normal"));
    return o;
}

json to_json(const Spin& s) {
    return json{{"axis", to_json(s.axis)}, {"period", s.period}};
}

Spin spin_from_json(const json& j) {
    Spin s;
    if (j.contains("axis")) s.axis = vec3_from_json(j.at("axis"));
    s.period = j.value("period", 0.0f);
    return s;
}

json to_json(const SceneCamera& c) {
    return json{{"position", to_json(c.position)}, {"orbit", to_json(c.orbit)},
                {"vup", to_json(c.vup)}, {"fov", c.fov},
                {"look", static_cast<int>(c.look)}, {"direction", to_json(c.direction)},
                {"target", c.target}, {"spin_period", c.spin_period},
                {"spin_phase", c.spin_phase}};
}

SceneCamera camera_from_json(const json& j) {
    SceneCamera c;
    if (j.contains("position")) c.position = vec3_from_json(j.at("position"));
    if (j.contains("orbit")) c.orbit = orbit_from_json(j.at("orbit"));
    if (j.contains("vup")) c.vup = vec3_from_json(j.at("vup"));
    c.fov = j.value("fov", 50.0f);
    c.look = static_cast<CamLook>(j.value("look", static_cast<int>(CamLook::Target)));
    if (j.contains("direction")) c.direction = vec3_from_json(j.at("direction"));
    c.target = j.value("target", -1);
    c.spin_period = j.value("spin_period", 0.0f);
    c.spin_phase = j.value("spin_phase", 0.0f);
    return c;
}

json body_to_json(const Body& b) {
    return json{{"shape", object_to_json(b.shape)},
                {"orbit", to_json(b.orbit)},
                {"spin", to_json(b.spin)}};
}

Body body_from_json(const json& j) {
    Body b;
    b.shape = object_from_json(j.at("shape"));
    if (j.contains("orbit")) b.orbit = orbit_from_json(j.at("orbit"));
    if (j.contains("spin")) b.spin = spin_from_json(j.at("spin"));
    return b;
}

// Offset from the orbit center for an explicit angle, in the orbital plane.
Vec3 orbit_offset_at(const Orbit& o, float ang) {
    Vec3 n = normalize(o.normal);
    // Build an orthonormal basis (u, v) spanning the orbital plane.
    Vec3 ref = (std::fabs(n.x) > 0.9f) ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
    Vec3 u = normalize(cross(ref, n));
    Vec3 v = cross(n, u);
    return (u * std::cos(ang) + v * std::sin(ang)) * o.radius;
}

// Position offset from the orbit center at time t, in the orbital plane.
Vec3 orbit_offset(const Orbit& o, float t) {
    constexpr float TWO_PI = 6.28318530717958647692f;
    float w = (o.period != 0.0f) ? (TWO_PI / o.period) : 0.0f;
    return orbit_offset_at(o, o.phase + w * t);
}

}  // namespace

std::string scene_to_json(const Scene& scene) {
    json bodies = json::array();
    for (const auto& b : scene.bodies)
        bodies.push_back(body_to_json(b));

    json j{
        {"camera", to_json(scene.cam)},
        {"light_dir", to_json(scene.light_dir)},
        {"resolution", {{"width", scene.width}, {"height", scene.height}}},
        {"bodies", bodies},
    };
    return j.dump(2);
}

Scene scene_from_json(const std::string& text) {
    json j = json::parse(text);
    Scene scene;

    scene.cam = camera_from_json(j.at("camera"));

    scene.light_dir = vec3_from_json(j.at("light_dir"));

    const json& res = j.at("resolution");
    scene.width = res.at("width").get<int>();
    scene.height = res.at("height").get<int>();

    for (const auto& b : j.at("bodies"))
        scene.bodies.push_back(body_from_json(b));

    return scene;
}

namespace {
// `depth` bounds the parent walk so a malformed cycle can't recurse forever.
Vec3 body_world_pos_rec(const Scene& scene, int i, float t, int depth) {
    if (i < 0 || i >= static_cast<int>(scene.bodies.size()) || depth < 0)
        return Vec3(0, 0, 0);
    const Body& b = scene.bodies[i];
    if (!b.orbit.active)
        return b.shape->center;

    Vec3 base(0, 0, 0);
    if (b.orbit.parent >= 0 && b.orbit.parent != i)
        base = body_world_pos_rec(scene, b.orbit.parent, t, depth - 1);
    return base + orbit_offset(b.orbit, t);
}
}  // namespace

Vec3 body_world_pos(const Scene& scene, int i, float t) {
    return body_world_pos_rec(scene, i, t, static_cast<int>(scene.bodies.size()));
}

Vec3 camera_eye(const Scene& scene, float t) {
    const SceneCamera& c = scene.cam;
    if (!c.orbit.active) return c.position;
    Vec3 base(0, 0, 0);
    int p = c.orbit.parent;
    if (p >= 0 && p < static_cast<int>(scene.bodies.size()))
        base = body_world_pos(scene, p, t);
    return base + orbit_offset(c.orbit, t);
}

Camera scene_camera(const Scene& scene, float t, float aspect) {
    const SceneCamera& c = scene.cam;
    Vec3 eye = camera_eye(scene, t);
    Vec3 at;
    switch (c.look) {
        case CamLook::Direction:
            at = eye + normalize(c.direction);
            break;
        case CamLook::Target: {
            Vec3 tgt(0, 0, 0);
            if (c.target >= 0 && c.target < static_cast<int>(scene.bodies.size()))
                tgt = body_world_pos(scene, c.target, t);
            at = tgt;
            break;
        }
        case CamLook::Spin: {
            constexpr float TWO_PI = 6.28318530717958647692f;
            float w = (c.spin_period != 0.0f) ? (TWO_PI / c.spin_period) : 0.0f;
            Vec3 dir = rotate_about(Vec3(0, 0, -1), c.vup, c.spin_phase + w * t);
            at = eye + dir;
            break;
        }
    }
    return Camera(eye, at, c.vup, c.fov, aspect);
}

World world_at_time(const Scene& scene, float t) {
    World world;
    world.reserve(scene.bodies.size());
    constexpr float TWO_PI = 6.28318530717958647692f;
    for (int i = 0; i < static_cast<int>(scene.bodies.size()); ++i) {
        auto shape = scene.bodies[i].shape->clone();
        shape->center = body_world_pos(scene, i, t);
        // Bake in the current spin orientation so textured spheres rotate (Phase 9).
        if (auto s = std::dynamic_pointer_cast<Sphere>(shape)) {
            const Spin& sp = scene.bodies[i].spin;
            s->spin_axis = sp.axis;
            s->spin_angle = (sp.period != 0.0f) ? (TWO_PI * t / sp.period) : 0.0f;
        }
        world.push_back(shape);
    }
    return world;
}

Vec3 orbit_ring_point(const Scene& scene, int i, float t, float angle) {
    if (i < 0 || i >= static_cast<int>(scene.bodies.size()))
        return Vec3(0, 0, 0);
    const Orbit& o = scene.bodies[i].orbit;
    Vec3 base(0, 0, 0);
    if (o.parent >= 0 && o.parent != i)
        base = body_world_pos(scene, o.parent, t);
    if (!o.active)
        return base;
    return base + orbit_offset_at(o, angle);
}

bool save_scene(const Scene& scene, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << scene_to_json(scene);
    return static_cast<bool>(f);
}

bool load_scene(Scene& out, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    try {
        out = scene_from_json(text);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}
