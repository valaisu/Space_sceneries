#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
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

json ramp_to_json(const std::vector<ColorStop>& stops) {
    json ramp = json::array();
    for (const auto& s : stops)
        ramp.push_back({{"pos", s.pos}, {"color", to_json(s.color)}, {"alpha", s.alpha}});
    return ramp;
}

void ramp_from_json(const json& j, std::vector<ColorStop>& out) {
    for (const auto& s : j) {
        ColorStop cs{s.at("pos").get<float>(), vec3_from_json(s.at("color"))};
        cs.alpha = s.value("alpha", 1.0f);
        out.push_back(cs);
    }
}

json to_json(const Material& m) {
    json j{{"albedo", to_json(m.albedo)}, {"emissive", m.emissive},
           {"two_sided", m.two_sided},
           {"pattern", m.pattern}, {"detail", to_json(m.detail)},
           {"noise_scale", m.noise_scale}, {"noise_octaves", m.noise_octaves},
           {"band_strength", m.band_strength}, {"band_freq", m.band_freq},
           {"warp", m.warp}};
    if (!m.ring_ramp.empty()) j["ring_ramp"] = ramp_to_json(m.ring_ramp);
    if (!m.tex_ramp.empty()) j["tex_ramp"] = ramp_to_json(m.tex_ramp);
    const Atmosphere& a = m.atmosphere;
    if (a.enabled)
        j["atmosphere"] = json{{"enabled", a.enabled}, {"color", to_json(a.color)},
                               {"sunset", to_json(a.sunset)}, {"thickness", a.thickness},
                               {"intensity", a.intensity}};
    const Clouds& cl = m.clouds;
    if (cl.enabled)
        j["clouds"] = json{{"enabled", cl.enabled}, {"scale", cl.scale},
                           {"octaves", cl.octaves}, {"coverage", cl.coverage},
                           {"opacity", cl.opacity}, {"ramp", ramp_to_json(cl.ramp)}};
    return j;
}

Material material_from_json(const json& j) {
    Material m;
    m.albedo = vec3_from_json(j.at("albedo"));
    m.emissive = j.value("emissive", false);
    m.two_sided = j.value("two_sided", false);
    m.pattern = j.value("pattern", 0);
    if (j.contains("detail")) m.detail = vec3_from_json(j.at("detail"));
    m.noise_scale = j.value("noise_scale", m.noise_scale);
    m.noise_octaves = j.value("noise_octaves", m.noise_octaves);
    m.band_strength = j.value("band_strength", m.band_strength);
    m.band_freq = j.value("band_freq", m.band_freq);
    m.warp = j.value("warp", m.warp);
    if (j.contains("ring_ramp")) ramp_from_json(j.at("ring_ramp"), m.ring_ramp);
    if (j.contains("tex_ramp")) ramp_from_json(j.at("tex_ramp"), m.tex_ramp);
    if (j.contains("atmosphere")) {
        const json& a = j.at("atmosphere");
        m.atmosphere.enabled = a.value("enabled", false);
        if (a.contains("color")) m.atmosphere.color = vec3_from_json(a.at("color"));
        if (a.contains("sunset")) m.atmosphere.sunset = vec3_from_json(a.at("sunset"));
        m.atmosphere.thickness = a.value("thickness", m.atmosphere.thickness);
        m.atmosphere.intensity = a.value("intensity", m.atmosphere.intensity);
    }
    if (j.contains("clouds")) {
        const json& c = j.at("clouds");
        m.clouds.enabled = c.value("enabled", false);
        m.clouds.scale = c.value("scale", m.clouds.scale);
        m.clouds.octaves = c.value("octaves", m.clouds.octaves);
        m.clouds.coverage = c.value("coverage", m.clouds.coverage);
        m.clouds.opacity = c.value("opacity", m.clouds.opacity);
        if (c.contains("ramp")) ramp_from_json(c.at("ramp"), m.clouds.ramp);
    }
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

json to_json(const Background& b) {
    return json{{"sky", to_json(b.sky)}, {"density", b.density},
                {"brightness", b.brightness}, {"size", b.size},
                {"tint", to_json(b.tint)}, {"color_variation", b.color_variation},
                {"seed", b.seed}, {"region_scale", b.region_scale},
                {"region_strength", b.region_strength}, {"region_glow", b.region_glow},
                {"region_star_boost", b.region_star_boost},
                {"band_enabled", b.band_enabled}, {"band_normal", to_json(b.band_normal)},
                {"band_width", b.band_width}, {"band_density", b.band_density},
                {"band_glow", b.band_glow}, {"band_tint", to_json(b.band_tint)},
                {"shoot_enabled", b.shoot_enabled}, {"shoot_rate", b.shoot_rate},
                {"shoot_speed", b.shoot_speed}, {"shoot_length", b.shoot_length},
                {"shoot_size", b.shoot_size}, {"shoot_brightness", b.shoot_brightness},
                {"shoot_seed", b.shoot_seed}};
}

Background background_from_json(const json& j) {
    Background b;
    if (j.contains("sky")) b.sky = vec3_from_json(j.at("sky"));
    b.density = j.value("density", b.density);
    b.brightness = j.value("brightness", b.brightness);
    b.size = j.value("size", b.size);
    if (j.contains("tint")) b.tint = vec3_from_json(j.at("tint"));
    b.color_variation = j.value("color_variation", b.color_variation);
    b.seed = j.value("seed", b.seed);
    b.region_scale = j.value("region_scale", b.region_scale);
    b.region_strength = j.value("region_strength", b.region_strength);
    b.region_glow = j.value("region_glow", b.region_glow);
    b.region_star_boost = j.value("region_star_boost", b.region_star_boost);
    b.band_enabled = j.value("band_enabled", b.band_enabled);
    if (j.contains("band_normal")) b.band_normal = vec3_from_json(j.at("band_normal"));
    b.band_width = j.value("band_width", b.band_width);
    b.band_density = j.value("band_density", b.band_density);
    b.band_glow = j.value("band_glow", b.band_glow);
    if (j.contains("band_tint")) b.band_tint = vec3_from_json(j.at("band_tint"));
    b.shoot_enabled = j.value("shoot_enabled", b.shoot_enabled);
    b.shoot_rate = j.value("shoot_rate", b.shoot_rate);
    b.shoot_speed = j.value("shoot_speed", b.shoot_speed);
    b.shoot_length = j.value("shoot_length", b.shoot_length);
    b.shoot_size = j.value("shoot_size", b.shoot_size);
    b.shoot_brightness = j.value("shoot_brightness", b.shoot_brightness);
    b.shoot_seed = j.value("shoot_seed", b.shoot_seed);
    return b;
}

json to_json(const PostProcess& p) {
    json pal = json::array();
    for (const auto& c : p.palette) pal.push_back(to_json(c));
    return json{{"enabled", p.enabled}, {"palette", pal},
                {"base_a", to_json(p.base_a)}, {"base_b", to_json(p.base_b)},
                {"base_c", to_json(p.base_c)},
                {"palette_size", p.palette_size}, {"blur_radius", p.blur_radius},
                {"dither", static_cast<int>(p.dither)}, {"iterations", p.iterations},
                {"scheme", p.scheme},
                {"anchor_count", p.anchor_count}, {"base_hue", p.base_hue},
                {"spread", p.spread},
                {"randomness", p.randomness}, {"palette_seed", p.palette_seed}};
}

PostProcess post_from_json(const json& j) {
    PostProcess p;
    p.enabled = j.value("enabled", p.enabled);
    if (j.contains("palette")) {
        p.palette.clear();
        for (const auto& c : j.at("palette")) p.palette.push_back(vec3_from_json(c));
    }
    if (j.contains("base_a")) p.base_a = vec3_from_json(j.at("base_a"));
    if (j.contains("base_b")) p.base_b = vec3_from_json(j.at("base_b"));
    if (j.contains("base_c")) p.base_c = vec3_from_json(j.at("base_c"));
    p.palette_size = j.value("palette_size", p.palette_size);
    p.blur_radius = j.value("blur_radius", p.blur_radius);
    p.dither = static_cast<DitherMode>(j.value("dither", static_cast<int>(p.dither)));
    p.iterations = j.value("iterations", p.iterations);
    p.scheme = j.value("scheme", p.scheme);
    p.anchor_count = j.value("anchor_count", p.anchor_count);
    p.base_hue = j.value("base_hue", p.base_hue);
    p.spread = j.value("spread", p.spread);
    p.randomness = j.value("randomness", p.randomness);
    p.palette_seed = j.value("palette_seed", p.palette_seed);
    return p;
}

json to_json(const Orbit& o) {
    return json{{"active", o.active}, {"parent", o.parent}, {"radius", o.radius},
                {"period", o.period}, {"phase", o.phase}, {"normal", to_json(o.normal)},
                {"eccentricity", o.eccentricity}};
}

Orbit orbit_from_json(const json& j) {
    Orbit o;
    o.active = j.value("active", false);
    o.parent = j.value("parent", -1);
    o.radius = j.value("radius", 5.0f);
    o.period = j.value("period", 10.0f);
    o.phase = j.value("phase", 0.0f);
    if (j.contains("normal")) o.normal = vec3_from_json(j.at("normal"));
    o.eccentricity = j.value("eccentricity", 0.0f);
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

// Offset from the parent (which sits at a focus) for an explicit eccentric anomaly
// E, in the orbital plane. With eccentricity 0 this is a circle of radius `radius`
// and E is just the angle — reproducing the old circular orbit exactly.
Vec3 orbit_offset_at(const Orbit& o, float E) {
    Vec3 n = normalize(o.normal);
    // Build an orthonormal basis (u, v) spanning the orbital plane.
    Vec3 ref = (std::fabs(n.x) > 0.9f) ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
    Vec3 u = normalize(cross(ref, n));
    Vec3 v = cross(n, u);
    float a = o.radius;
    float e = std::clamp(o.eccentricity, 0.0f, 0.99f);
    // Ellipse with the focus at the origin: x = a(cosE - e), y = b sinE.
    float x = a * (std::cos(E) - e);
    float y = a * std::sqrt(1.0f - e * e) * std::sin(E);
    return u * x + v * y;
}

// Position offset from the parent at time t. The mean anomaly advances uniformly;
// solving Kepler's equation for the eccentric anomaly gives the equal-area motion
// (faster at periapsis). e = 0 collapses to the uniform circular case.
Vec3 orbit_offset(const Orbit& o, float t) {
    constexpr float TWO_PI = 6.28318530717958647692f;
    float w = (o.period != 0.0f) ? (TWO_PI / o.period) : 0.0f;
    float M = o.phase + w * t;                       // mean anomaly
    float e = std::clamp(o.eccentricity, 0.0f, 0.99f);
    float E = M;                                     // eccentric anomaly (Newton solve)
    for (int i = 0; i < 6; ++i) {
        float f = E - e * std::sin(E) - M;
        E -= f / (1.0f - e * std::cos(E));
    }
    return orbit_offset_at(o, E);
}

// A unit vector near +Y, tilted by up to `amount` * 18 deg about a random
// horizontal axis. Used for near-coplanar orbit normals and spin axes.
Vec3 tilted_up(std::mt19937& rng, float amount) {
    constexpr float PI = 3.14159265358979323846f;
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    float ang = amount * (18.0f * PI / 180.0f) * u01(rng);
    float az = u01(rng) * 2.0f * PI;
    Vec3 axis(std::cos(az), 0.0f, std::sin(az));  // horizontal tilt axis
    return normalize(rotate_about(Vec3(0, 1, 0), axis, ang));
}

}  // namespace

void generate_system(Scene& scene, const SystemGenParams& p) {
    constexpr float TWO_PI = 6.28318530717958647692f;
    std::mt19937 rng(static_cast<uint32_t>(p.seed));
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    auto rf = [&](float a, float b) { return a + (b - a) * u01(rng); };
    auto ri = [&](int a, int b) { return a + static_cast<int>((b - a + 1) * u01(rng)); };

    const int count = std::max(1, p.planet_count);
    std::vector<Body> bodies;

    // --- Sun (body 0): the light source ---
    Material sun_mat{Vec3(1.0f, rf(0.85f, 0.97f), rf(0.45f, 0.7f)), true};
    auto sun = std::make_shared<Sphere>(Vec3(0, 0, 0), p.sun_radius, sun_mat);
    sun->name = "Sun";
    bodies.push_back(Body{sun});

    // --- Planets on geometrically growing orbits (Titius-Bode-ish) ---
    std::vector<float> orbit_r(count), planet_rad(count), ecc(count), ring_outer(count, 0.0f);
    std::vector<int> planet_idx(count);
    std::vector<char> is_gas(count, 0);

    float r = p.sun_radius + rf(3.0f, 4.5f);  // first orbit clears the sun comfortably
    for (int i = 0; i < count; ++i) {
        float frac = (count > 1) ? static_cast<float>(i) / (count - 1) : 0.0f;
        bool gas = u01(rng) < 0.2f + 0.55f * frac;  // gas giants grow likelier outward
        float prad = gas ? rf(0.7f, 1.3f) : rf(0.25f, 0.6f);
        orbit_r[i] = r;
        planet_rad[i] = prad;
        is_gas[i] = gas ? 1 : 0;
        ecc[i] = p.eccentricity * u01(rng);

        Material m{};
        m.pattern = gas ? 1 : 2;
        m.noise_octaves = 4;
        m.noise_scale = gas ? rf(2.0f, 3.5f) : rf(3.0f, 5.0f);
        if (gas) {  // banded two-tone gas giant (tan/orange or blue family)
            Vec3 a = (u01(rng) < 0.5f)
                         ? Vec3(rf(0.6f, 0.85f), rf(0.5f, 0.7f), rf(0.3f, 0.45f))
                         : Vec3(rf(0.3f, 0.5f), rf(0.5f, 0.7f), rf(0.7f, 0.9f));
            m.albedo = a;
            m.detail = a * rf(0.55f, 0.8f);
            m.band_strength = rf(0.6f, 0.95f);
            m.band_freq = rf(4.0f, 10.0f);
            m.warp = rf(0.1f, 0.6f);
        } else {  // mottled rocky planet (brown / gray / rust)
            Vec3 a = Vec3(rf(0.35f, 0.7f), rf(0.3f, 0.55f), rf(0.25f, 0.45f));
            m.albedo = a;
            m.detail = a * rf(0.5f, 0.8f);
            m.band_strength = rf(0.0f, 0.2f);
        }
        if (p.atmospheres && u01(rng) < 0.45f) {
            m.atmosphere.enabled = true;
            m.atmosphere.color = Vec3(rf(0.3f, 0.5f), rf(0.5f, 0.7f), rf(0.8f, 1.0f));
            m.atmosphere.thickness = rf(0.3f, 0.6f);
            m.atmosphere.intensity = rf(0.6f, 1.3f);
        }

        auto sph = std::make_shared<Sphere>(Vec3(0, 0, 0), prad, m);
        sph->name = (gas ? "Gas Giant " : "Planet ") + std::to_string(i + 1);
        Body b{sph};
        b.orbit.active = true;
        b.orbit.parent = 0;  // orbit the sun
        b.orbit.radius = r;
        b.orbit.period = std::pow(r, 1.5f) * rf(1.0f, 1.3f);  // Kepler-ish: outer = slower
        b.orbit.phase = rf(0.0f, TWO_PI);
        b.orbit.eccentricity = ecc[i];
        b.orbit.normal = tilted_up(rng, p.inclination);
        b.spin.axis = tilted_up(rng, 0.4f);
        b.spin.period = rf(3.0f, 10.0f) * (gas ? 0.6f : 1.0f);
        planet_idx[i] = static_cast<int>(bodies.size());
        bodies.push_back(b);

        r *= p.spacing * rf(0.9f, 1.15f);  // next orbit, with a little jitter
    }

    // --- Rings on some gas giants (added before moons so moons clear the ring) ---
    for (int i = 0; i < count; ++i) {
        if (!p.rings || !is_gas[i] || u01(rng) < 0.55f) continue;  // ~45% of gas giants
        float inner = planet_rad[i] * rf(1.2f, 1.5f);
        float outer = inner + planet_rad[i] * rf(0.4f, 0.9f);
        ring_outer[i] = outer;
        Material rm{Vec3(rf(0.6f, 0.8f), rf(0.55f, 0.7f), rf(0.45f, 0.6f)), false};
        rm.two_sided = true;  // lit from either face, reads as translucent
        Vec3 axis = bodies[planet_idx[i]].spin.axis;  // ring sits in the equatorial plane
        auto disk = std::make_shared<Disk>(Vec3(0, 0, 0), axis, inner, outer, rm);
        disk->name = bodies[planet_idx[i]].shape->name + " Ring";
        Body rb{disk};
        rb.orbit.active = true;
        rb.orbit.parent = planet_idx[i];  // radius-0 attachment, tracks the planet
        rb.orbit.radius = 0.0f;
        rb.orbit.normal = axis;
        bodies.push_back(rb);
    }

    // --- Moons, kept in a bubble that never reaches a neighbour or the sun ---
    // The bubble cap is < half the *worst-case* clearance to either neighbouring
    // orbit (using their perihelion/aphelion edges), so a moon's distance to its
    // planet is always less than its distance to any other planet or the sun.
    for (int i = 0; i < count && p.max_moons > 0; ++i) {
        float inner_edge = orbit_r[i] * (1.0f - ecc[i]);
        float outer_edge = orbit_r[i] * (1.0f + ecc[i]);
        float in_neighbor = (i > 0) ? orbit_r[i - 1] * (1.0f + ecc[i - 1]) : p.sun_radius;
        float gap_in = inner_edge - in_neighbor;
        float gap_out = (i < count - 1) ? (orbit_r[i + 1] * (1.0f - ecc[i + 1]) - outer_edge)
                                        : gap_in;
        float bubble = 0.4f * std::max(0.0f, std::min(gap_in, gap_out));
        float prev = std::max(planet_rad[i], ring_outer[i]);  // clear the planet (and ring)

        int nm = ri(0, p.max_moons);
        for (int k = 0; k < nm; ++k) {
            float mrad = std::min(planet_rad[i] * 0.4f, rf(0.08f, 0.2f));
            float mo = prev + mrad + rf(0.3f, 0.7f);  // step outward from the last moon
            if (mo > bubble) break;                   // would leave the safe bubble
            Material mm{Vec3(rf(0.45f, 0.7f), rf(0.45f, 0.7f), rf(0.45f, 0.7f)), false};
            mm.pattern = 2;
            mm.noise_scale = rf(4.0f, 7.0f);
            mm.detail = mm.albedo * 0.6f;
            auto ms = std::make_shared<Sphere>(Vec3(0, 0, 0), mrad, mm);
            ms->name = bodies[planet_idx[i]].shape->name + " Moon " + std::to_string(k + 1);
            Body mb{ms};
            mb.orbit.active = true;
            mb.orbit.parent = planet_idx[i];
            mb.orbit.radius = mo;
            mb.orbit.period = std::pow(mo, 1.5f) * rf(1.5f, 3.0f);
            mb.orbit.phase = rf(0.0f, TWO_PI);
            mb.orbit.normal = tilted_up(rng, p.inclination * 0.5f);
            mb.spin.period = rf(2.0f, 6.0f);
            bodies.push_back(mb);
            prev = mo + mrad;
        }
    }

    scene.bodies = std::move(bodies);

    // Reframe: orbit a planet (prefer a gas giant) and look at it, so a generated
    // system opens on a moving close-up of a planet rather than a static wide shot.
    std::vector<int> giants;
    for (int i = 0; i < count; ++i)
        if (is_gas[i]) giants.push_back(i);
    int fp = giants.empty() ? ri(0, count - 1)
                            : giants[ri(0, static_cast<int>(giants.size()) - 1)];
    float fr = planet_rad[fp];
    scene.cam.orbit.active = true;
    scene.cam.orbit.parent = planet_idx[fp];  // circle the chosen planet
    scene.cam.orbit.radius = std::max(fr * 6.0f, ring_outer[fp] + fr * 3.0f);
    scene.cam.orbit.period = rf(20.0f, 40.0f);
    scene.cam.orbit.phase = rf(0.0f, TWO_PI);
    scene.cam.orbit.eccentricity = 0.0f;
    scene.cam.orbit.normal = normalize(Vec3(rf(-0.3f, 0.3f), 1.0f, rf(-0.3f, 0.3f)));
    scene.cam.vup = Vec3(0, 1, 0);
    scene.cam.look = CamLook::Target;
    scene.cam.target = planet_idx[fp];  // keep that planet framed
}

std::string scene_to_json(const Scene& scene) {
    json bodies = json::array();
    for (const auto& b : scene.bodies)
        bodies.push_back(body_to_json(b));

    json j{
        {"camera", to_json(scene.cam)},
        {"light_dir", to_json(scene.light_dir)},
        {"fill_light", scene.fill_light},
        {"light_falloff", scene.light_falloff},
        {"background", to_json(scene.background)},
        {"post", to_json(scene.post)},
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
    scene.fill_light = j.value("fill_light", false);
    scene.light_falloff = j.value("light_falloff", false);

    if (j.contains("background"))
        scene.background = background_from_json(j.at("background"));

    if (j.contains("post"))
        scene.post = post_from_json(j.at("post"));

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

bool save_palette(const std::vector<Vec3>& palette, const std::string& path) {
    json colors = json::array();
    for (const auto& c : palette) colors.push_back(to_json(c));
    std::ofstream f(path);
    if (!f) return false;
    f << json{{"palette", colors}}.dump(2);
    return static_cast<bool>(f);
}

bool load_palette(std::vector<Vec3>& out, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    try {
        json j = json::parse(text);
        out.clear();
        for (const auto& c : j.at("palette")) out.push_back(vec3_from_json(c));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}
