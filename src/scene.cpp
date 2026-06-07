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
           {"band_var", m.band_var}, {"warp", m.warp},
           {"band_levels", m.band_levels}, {"turbulence", m.turbulence},
           {"band_drift", m.band_drift},
           {"storm", m.storm}, {"storm_seed", m.storm_seed},
           {"storm_color", to_json(m.storm_color)}};
    if (!m.ring_ramp.empty()) j["ring_ramp"] = ramp_to_json(m.ring_ramp);
    if (!m.tex_ramp.empty()) j["tex_ramp"] = ramp_to_json(m.tex_ramp);
    const Terrain& tr = m.terrain;
    if (tr.enabled)
        j["terrain"] = json{{"enabled", tr.enabled}, {"sea_level", tr.sea_level},
                            {"ocean", to_json(tr.ocean)}, {"levels", tr.levels},
                            {"cap", tr.cap}, {"cap_color", to_json(tr.cap_color)},
                            {"cap_season", tr.cap_season},
                            {"season_period", tr.season_period}};
    const Craters& cr = m.craters;
    if (cr.enabled)
        j["craters"] = json{{"enabled", cr.enabled}, {"density", cr.density},
                            {"strength", cr.strength}, {"seed", cr.seed}};
    const Atmosphere& a = m.atmosphere;
    if (a.enabled)
        j["atmosphere"] = json{{"enabled", a.enabled}, {"color", to_json(a.color)},
                               {"sunset", to_json(a.sunset)}, {"thickness", a.thickness},
                               {"intensity", a.intensity}};
    const Clouds& cl = m.clouds;
    if (cl.enabled)
        j["clouds"] = json{{"enabled", cl.enabled}, {"scale", cl.scale},
                           {"octaves", cl.octaves}, {"coverage", cl.coverage},
                           {"opacity", cl.opacity}, {"drift", cl.drift},
                           {"ramp", ramp_to_json(cl.ramp)}};
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
    m.band_var = j.value("band_var", m.band_var);
    m.warp = j.value("warp", m.warp);
    m.band_levels = j.value("band_levels", m.band_levels);
    m.turbulence = j.value("turbulence", m.turbulence);
    m.band_drift = j.value("band_drift", m.band_drift);
    m.storm = j.value("storm", m.storm);
    m.storm_seed = j.value("storm_seed", m.storm_seed);
    if (j.contains("storm_color")) m.storm_color = vec3_from_json(j.at("storm_color"));
    if (j.contains("ring_ramp")) ramp_from_json(j.at("ring_ramp"), m.ring_ramp);
    if (j.contains("tex_ramp")) ramp_from_json(j.at("tex_ramp"), m.tex_ramp);
    if (j.contains("terrain")) {
        const json& t = j.at("terrain");
        m.terrain.enabled = t.value("enabled", false);
        m.terrain.sea_level = t.value("sea_level", m.terrain.sea_level);
        if (t.contains("ocean")) m.terrain.ocean = vec3_from_json(t.at("ocean"));
        m.terrain.levels = t.value("levels", m.terrain.levels);
        m.terrain.cap = t.value("cap", m.terrain.cap);
        if (t.contains("cap_color")) m.terrain.cap_color = vec3_from_json(t.at("cap_color"));
        m.terrain.cap_season = t.value("cap_season", m.terrain.cap_season);
        m.terrain.season_period = t.value("season_period", m.terrain.season_period);
    }
    if (j.contains("craters")) {
        const json& cr = j.at("craters");
        m.craters.enabled = cr.value("enabled", false);
        m.craters.density = cr.value("density", m.craters.density);
        m.craters.strength = cr.value("strength", m.craters.strength);
        m.craters.seed = cr.value("seed", m.craters.seed);
    }
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
        m.clouds.drift = c.value("drift", m.clouds.drift);
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

json to_json(const SystemGenParams& p) {
    return json{
        {"seed", p.seed}, {"planet_count", p.planet_count}, {"max_moons", p.max_moons},
        {"spacing", p.spacing}, {"sun_radius", p.sun_radius}, {"inclination", p.inclination},
        {"eccentricity", p.eccentricity}, {"rings", p.rings}, {"atmospheres", p.atmospheres},
        {"gas_ratio", p.gas_ratio}, {"hero_kind", p.hero_kind},
        {"gas_storm_chance", p.gas_storm_chance}, {"gas_storm_strength", p.gas_storm_strength},
        {"gas_belt_count", p.gas_belt_count}, {"gas_belt_var", p.gas_belt_var},
        {"gas_turbulence", p.gas_turbulence}, {"gas_swirl", p.gas_swirl}, {"gas_drift", p.gas_drift},
        {"spin_speed", p.spin_speed}, {"axial_tilt", p.axial_tilt},
        {"extreme_tilt_chance", p.extreme_tilt_chance},
        {"water_chance", p.water_chance}, {"exotic_chance", p.exotic_chance},
        {"biome_chance", p.biome_chance}, {"cap_chance", p.cap_chance},
        {"frozen_chance", p.frozen_chance}, {"season_chance", p.season_chance},
        {"cloud_chance", p.cloud_chance}, {"crater_chance", p.crater_chance},
        {"ring_chance", p.ring_chance}, {"ring_colors", p.ring_colors},
        {"atmosphere_chance", p.atmosphere_chance}, {"moon_cap_chance", p.moon_cap_chance},
        {"sun_texture_chance", p.sun_texture_chance}};
}

SystemGenParams gen_params_from_json(const json& j) {
    SystemGenParams p;  // defaults for any missing key (old files)
    p.seed = j.value("seed", p.seed);
    p.planet_count = j.value("planet_count", p.planet_count);
    p.max_moons = j.value("max_moons", p.max_moons);
    p.spacing = j.value("spacing", p.spacing);
    p.sun_radius = j.value("sun_radius", p.sun_radius);
    p.inclination = j.value("inclination", p.inclination);
    p.eccentricity = j.value("eccentricity", p.eccentricity);
    p.rings = j.value("rings", p.rings);
    p.atmospheres = j.value("atmospheres", p.atmospheres);
    p.gas_ratio = j.value("gas_ratio", p.gas_ratio);
    p.hero_kind = j.value("hero_kind", p.hero_kind);
    p.gas_storm_chance = j.value("gas_storm_chance", p.gas_storm_chance);
    p.gas_storm_strength = j.value("gas_storm_strength", p.gas_storm_strength);
    p.gas_belt_count = j.value("gas_belt_count", p.gas_belt_count);
    p.gas_belt_var = j.value("gas_belt_var", p.gas_belt_var);
    p.gas_turbulence = j.value("gas_turbulence", p.gas_turbulence);
    p.gas_swirl = j.value("gas_swirl", p.gas_swirl);
    p.gas_drift = j.value("gas_drift", p.gas_drift);
    p.spin_speed = j.value("spin_speed", p.spin_speed);
    p.axial_tilt = j.value("axial_tilt", p.axial_tilt);
    p.extreme_tilt_chance = j.value("extreme_tilt_chance", p.extreme_tilt_chance);
    p.water_chance = j.value("water_chance", p.water_chance);
    p.exotic_chance = j.value("exotic_chance", p.exotic_chance);
    p.biome_chance = j.value("biome_chance", p.biome_chance);
    p.cap_chance = j.value("cap_chance", p.cap_chance);
    p.frozen_chance = j.value("frozen_chance", p.frozen_chance);
    p.season_chance = j.value("season_chance", p.season_chance);
    p.cloud_chance = j.value("cloud_chance", p.cloud_chance);
    p.crater_chance = j.value("crater_chance", p.crater_chance);
    p.ring_chance = j.value("ring_chance", p.ring_chance);
    p.ring_colors = j.value("ring_colors", p.ring_colors);
    p.atmosphere_chance = j.value("atmosphere_chance", p.atmosphere_chance);
    p.moon_cap_chance = j.value("moon_cap_chance", p.moon_cap_chance);
    p.sun_texture_chance = j.value("sun_texture_chance", p.sun_texture_chance);
    return p;
}

json to_json(const EditorSettings& e) {
    return json{{"gen", to_json(e.gen)},
                {"scene_seconds", e.scene_seconds}, {"cam_loops", e.cam_loops},
                {"pal_transition", e.pal_transition}, {"infinite_mode", e.infinite_mode},
                {"play_speed", e.play_speed},
                {"shade_mode", static_cast<int>(e.shade_mode)},
                {"show_orbits", e.show_orbits}};
}

EditorSettings editor_settings_from_json(const json& j) {
    EditorSettings e;
    if (j.contains("gen")) e.gen = gen_params_from_json(j.at("gen"));
    e.scene_seconds = j.value("scene_seconds", e.scene_seconds);
    e.cam_loops = j.value("cam_loops", e.cam_loops);
    e.pal_transition = j.value("pal_transition", e.pal_transition);
    e.infinite_mode = j.value("infinite_mode", e.infinite_mode);
    e.play_speed = j.value("play_speed", e.play_speed);
    e.shade_mode = static_cast<ShadeMode>(j.value("shade_mode", static_cast<int>(e.shade_mode)));
    e.show_orbits = j.value("show_orbits", e.show_orbits);
    return e;
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

// HSV -> RGB (h wrapped to [0,1)). Used to give some generated worlds an
// unfamiliar (non-Earth) hue family instead of always green/blue/tan.
Vec3 hsv(float h, float s, float v) {
    h -= std::floor(h);
    float i = std::floor(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: return Vec3(v, t, p);
        case 1: return Vec3(q, v, p);
        case 2: return Vec3(p, v, t);
        case 3: return Vec3(p, q, v);
        case 4: return Vec3(t, p, v);
        default: return Vec3(v, p, q);
    }
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
    if (u01(rng) < p.sun_texture_chance) {  // faint granulation, sometimes soft banding
        sun_mat.pattern = 1;
        sun_mat.noise_scale = rf(4.0f, 8.0f);
        sun_mat.noise_octaves = 4;
        Vec3 cool = sun_mat.albedo * rf(0.78f, 0.9f);  // slightly darker mottle
        sun_mat.tex_ramp = {{0.0f, cool}, {1.0f, sun_mat.albedo}};
        if (u01(rng) < 0.4f) {  // sometimes faint latitude banding instead of pure mottle
            sun_mat.band_strength = rf(0.2f, 0.4f);
            sun_mat.band_freq = rf(3.0f, 7.0f);
        }
    }
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
        // gas_ratio sets the overall fraction; a mild outward tilt (vanishing at the
        // 0/1 extremes) keeps inner planets a touch rockier, outer ones gassier.
        float pgas = p.gas_ratio + p.gas_ratio * (1.0f - p.gas_ratio) * (frac - 0.5f) * 0.8f;
        bool gas = u01(rng) < pgas;
        float prad = gas ? rf(0.7f, 1.3f) : rf(0.25f, 0.6f);
        orbit_r[i] = r;
        planet_rad[i] = prad;
        is_gas[i] = gas ? 1 : 0;
        ecc[i] = p.eccentricity * u01(rng);

        Material m{};
        m.pattern = gas ? 1 : 2;
        m.noise_octaves = 4;
        m.noise_scale = gas ? rf(2.0f, 3.5f) : rf(3.0f, 5.0f);
        if (gas) {  // banded gas giant: crisp belts + zonal filaments, sometimes a storm
            bool warm = u01(rng) < 0.5f;
            Vec3 a = warm
                         ? Vec3(rf(0.6f, 0.85f), rf(0.5f, 0.7f), rf(0.3f, 0.45f))
                         : Vec3(rf(0.3f, 0.5f), rf(0.5f, 0.7f), rf(0.7f, 0.9f));
            m.albedo = a;
            m.detail = a * rf(0.55f, 0.8f);
            // High band_strength keeps belts distinct (a little noise stays for texture).
            m.band_strength = rf(0.82f, 0.95f);
            m.band_freq = p.gas_belt_count * rf(0.7f, 1.3f);
            m.band_var = std::min(1.3f, p.gas_belt_var * rf(0.6f, 1.4f));  // uneven WIDTHS
            m.warp = p.gas_swirl * rf(0.4f, 1.6f);        // swirl (calm keeps belts as belts)
            m.turbulence = p.gas_turbulence * rf(0.6f, 1.4f);  // zonal belt-edge filaments
            m.band_drift = p.gas_drift * rf(-1.0f, 1.0f);      // belts slide slowly over time
            // Each belt samples its own color along the ramp; usually free (0), some-
            // times snapped to a small set of distinct shades.
            m.band_levels = (u01(rng) < 0.35f) ? ri(4, 8) : 0;
            // A 3-stop ramp (the "line" belts sample from) gives a richer set of belt
            // colors than the albedo<->detail fallback; most gas giants get one.
            if (u01(rng) < 0.7f) {
                Vec3 c1 = a * rf(0.55f, 0.75f);
                Vec3 accent = warm ? Vec3(rf(0.8f, 0.95f), rf(0.55f, 0.7f), rf(0.35f, 0.5f))
                                   : Vec3(rf(0.55f, 0.75f), rf(0.75f, 0.9f), rf(0.85f, 1.0f));
                m.tex_ramp = {{0.0f, c1}, {0.55f, a}, {1.0f, accent}};
            }
            // Storms: a red/cream oval (or alien tint) over the belts.
            if (u01(rng) < p.gas_storm_chance) {
                m.storm = std::min(1.0f, p.gas_storm_strength * rf(0.85f, 1.15f));
                m.storm_seed = ri(1, 9999);
                float sroll = u01(rng);
                m.storm_color = (sroll < 0.5f) ? Vec3(rf(0.7f, 0.9f), rf(0.25f, 0.4f), rf(0.15f, 0.3f))  // red
                              : (sroll < 0.8f) ? Vec3(rf(0.92f, 1.0f), rf(0.9f, 0.97f), rf(0.85f, 0.95f)) // cream
                                               : a * rf(0.4f, 0.6f);                                       // dark vortex
            }
        } else {  // rocky planet: ocean/land terrain, varied ice caps, sometimes clouds
            m.band_strength = 0.0f;       // terrain wants a pure elevation field
            m.noise_octaves = 3;          // smoother field -> cleaner posterized regions
            Terrain& tr = m.terrain;
            tr.enabled = true;
            bool watery = u01(rng) < p.water_chance;
            bool exotic = u01(rng) < p.exotic_chance;  // mostly familiar, sometimes alien hues
            tr.sea_level = watery ? rf(0.42f, 0.62f) : rf(0.0f, 0.2f);  // dry worlds barely flood
            Vec3 ocean = watery
                ? Vec3(rf(0.05f, 0.15f), rf(0.2f, 0.35f), rf(0.45f, 0.65f))   // blue sea
                : Vec3(rf(0.2f, 0.35f), rf(0.15f, 0.25f), rf(0.1f, 0.2f));    // dark dust basins
            // Three distinct biome colors (coast/sand -> mid -> highland) rather than a
            // two-color gradient, so posterizing yields clearly different regions.
            Vec3 c0, c1, c2;
            if (exotic) {  // an unfamiliar hue family, but still spread in hue + value
                float h = u01(rng);
                ocean = hsv(h + rf(0.45f, 0.6f), rf(0.5f, 0.75f), rf(0.25f, 0.45f));
                c0 = hsv(h,                  rf(0.5f, 0.8f), rf(0.35f, 0.55f));
                c1 = hsv(h + rf(0.1f, 0.25f), rf(0.5f, 0.8f), rf(0.5f, 0.7f));
                c2 = hsv(h + rf(0.4f, 0.65f), rf(0.35f, 0.6f), rf(0.65f, 0.85f));
            } else if (watery) {
                c0 = Vec3(rf(0.75f, 0.9f), rf(0.7f, 0.85f), rf(0.45f, 0.6f));   // sandy coast
                c1 = Vec3(rf(0.2f, 0.4f),  rf(0.45f, 0.65f), rf(0.2f, 0.35f));  // green
                c2 = Vec3(rf(0.45f, 0.6f), rf(0.4f, 0.5f),   rf(0.35f, 0.45f)); // rock
            } else {  // dry / desert
                c0 = Vec3(rf(0.8f, 0.95f), rf(0.7f, 0.85f), rf(0.5f, 0.65f));   // pale sand
                c1 = Vec3(rf(0.6f, 0.8f),  rf(0.4f, 0.55f),  rf(0.25f, 0.4f));  // rust
                c2 = Vec3(rf(0.35f, 0.5f), rf(0.3f, 0.4f),   rf(0.25f, 0.35f)); // dark rock
            }
            tr.ocean = ocean;
            m.tex_ramp = {{0.0f, c0}, {0.5f, c1}, {1.0f, c2}};
            m.albedo = c1;
            // Posterize into crisp bands on most worlds; a few stay smooth for variety.
            tr.levels = (u01(rng) < p.biome_chance) ? 3 : 0;
            // Ice caps: no caps above cap_chance, fully frozen below frozen_chance,
            // otherwise a varied (usually visible) cap.
            float caproll = u01(rng);
            if (caproll > p.cap_chance) tr.cap = 1.1f;             // no caps
            else if (caproll < p.frozen_chance) tr.cap = rf(0.3f, 0.5f);  // frozen: big caps
            else tr.cap = rf(0.5f, 0.9f);                          // varied, often clearly visible
            float caphue = u01(rng);  // a spread of cap tints, not just white
            tr.cap_color =
                (caphue < 0.5f)  ? Vec3(rf(0.88f, 0.97f), rf(0.92f, 0.98f), 1.0f)            // white-blue ice
              : (caphue < 0.7f)  ? Vec3(rf(0.85f, 0.97f), rf(0.7f, 0.85f), rf(0.55f, 0.75f)) // warm dust / CO2
              : (caphue < 0.85f) ? Vec3(rf(0.6f, 0.78f),  rf(0.78f, 0.9f),  rf(0.92f, 1.0f)) // pale cyan
              : exotic           ? hsv(u01(rng), rf(0.2f, 0.45f), rf(0.9f, 1.0f))            // alien tint
                                 : Vec3(rf(0.92f, 1.0f), rf(0.82f, 0.92f), rf(0.85f, 0.95f)); // faint pink-white
            if (tr.cap < 1.0f && u01(rng) < p.season_chance) {  // capped worlds breathe seasons
                tr.cap_season = rf(0.06f, 0.18f);
                tr.season_period = rf(20.0f, 60.0f);
            }
            // Clouds on some worlds (commoner when watery); not necessarily white.
            if (u01(rng) < (watery ? p.cloud_chance : p.cloud_chance * 0.5f)) {
                Clouds& cl = m.clouds;
                cl.enabled = true;
                cl.scale = rf(3.0f, 6.0f);
                cl.octaves = 4;
                cl.coverage = rf(0.4f, 0.65f);
                cl.opacity = rf(0.5f, 0.9f);
                cl.drift = rf(-0.05f, 0.05f);
                float croll = u01(rng);
                Vec3 cc = (croll < 0.55f) ? Vec3(1.0f, 1.0f, 1.0f)            // white
                        : exotic          ? hsv(u01(rng), rf(0.25f, 0.5f), 1.0f)  // alien tint
                                          : Vec3(rf(0.9f, 1.0f), rf(0.82f, 0.95f),
                                                 rf(0.7f, 0.9f));             // warm haze
                cl.ramp = {{0.0f, cc, 0.0f}, {1.0f, cc, 1.0f}};
            }
        }
        if (p.atmospheres && u01(rng) < p.atmosphere_chance) {
            m.atmosphere.enabled = true;
            m.atmosphere.color = Vec3(rf(0.3f, 0.5f), rf(0.5f, 0.7f), rf(0.8f, 1.0f));
            m.atmosphere.thickness = rf(0.3f, 0.6f);
            m.atmosphere.intensity = rf(0.6f, 1.3f);
        }
        // Airless rocky worlds get impact craters (gas giants and worlds with an
        // atmosphere keep a smooth surface).
        if (!gas && !m.atmosphere.enabled && u01(rng) < p.crater_chance) {
            m.craters.enabled = true;
            m.craters.density = rf(3.0f, 7.0f);
            m.craters.strength = rf(0.35f, 0.7f);
            m.craters.seed = ri(1, 9999);
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
        b.spin.axis = tilted_up(rng, p.axial_tilt / 18.0f);  // small random axial tilt
        if (u01(rng) < p.extreme_tilt_chance) {  // Uranus-like extreme tilt (~90 deg, on its side)
            float az = u01(rng) * TWO_PI;
            Vec3 ax(std::cos(az), 0.0f, std::sin(az));
            float ang = rf(75.0f, 105.0f) * TWO_PI / 360.0f;
            b.spin.axis = normalize(rotate_about(Vec3(0, 1, 0), ax, ang));
            // Tipped on its side, the "poles" face the orbital plane, so static polar
            // ice caps make no physical sense — drop them on extreme-tilt worlds.
            sph->material.terrain.cap = 1.1f;
        }
        b.spin.period = rf(3.0f, 10.0f) * (gas ? 0.6f : 1.0f) / std::max(0.05f, p.spin_speed);
        planet_idx[i] = static_cast<int>(bodies.size());
        bodies.push_back(b);

        r *= p.spacing * rf(0.9f, 1.15f);  // next orbit, with a little jitter
    }

    // --- Rings on some gas giants (added before moons so moons clear the ring) ---
    for (int i = 0; i < count; ++i) {
        if (!p.rings || !is_gas[i] || u01(rng) >= p.ring_chance) continue;
        float inner = planet_rad[i] * rf(1.2f, 1.5f);
        float outer = inner + planet_rad[i] * rf(0.4f, 0.9f);
        ring_outer[i] = outer;
        Vec3 ring_base(rf(0.6f, 0.8f), rf(0.55f, 0.7f), rf(0.45f, 0.6f));
        Material rm{ring_base, false};
        rm.two_sided = true;  // lit from either face, reads as translucent
        // Concentric color bands: ring_colors distinct brightness/tint steps across the
        // radius, so the ring reads as banded (Saturn-like) rather than a flat sheet.
        int nbands = std::max(1, static_cast<int>(p.ring_colors * rf(0.7f, 1.3f) + 0.5f));
        for (int s = 0; s < nbands && nbands > 1; ++s) {
            float pos = static_cast<float>(s) / (nbands - 1);
            Vec3 col = ring_base * rf(0.55f, 1.1f);  // per-band brightness
            rm.ring_ramp.push_back({pos,
                Vec3(std::min(1.0f, col.x), std::min(1.0f, col.y), std::min(1.0f, col.z)), 1.0f});
        }
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
            if (u01(rng) < p.moon_cap_chance) {  // some moons wear bright polar caps
                mm.terrain.enabled = true;
                mm.terrain.sea_level = 0.0f;  // airless: no ocean, just the mottle + caps
                mm.terrain.cap = rf(0.78f, 0.92f);
            }
            if (u01(rng) < p.crater_chance) {  // moons are airless -> cratered
                mm.craters.enabled = true;
                mm.craters.density = rf(4.0f, 8.0f);
                mm.craters.strength = rf(0.4f, 0.75f);
                mm.craters.seed = ri(1, 9999);
            }
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

    // Reframe: orbit a planet and look at it, so a generated system opens on a
    // moving close-up rather than a static wide shot. hero_kind picks the type:
    // 0 prefers a gas giant (the default close-up), 1 wants a rocky/terrain world
    // (shows off oceans + caps), 2 forces a gas giant. Falls back to any planet.
    std::vector<int> prefer;
    for (int i = 0; i < count; ++i)
        if ((p.hero_kind == 1) ? !is_gas[i] : (bool)is_gas[i]) prefer.push_back(i);
    int fp = prefer.empty() ? ri(0, count - 1)
                            : prefer[ri(0, static_cast<int>(prefer.size()) - 1)];
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

void generate_eclipse_system(Scene& scene, const SystemGenParams& p,
                             float scene_seconds, int cam_loops) {
    constexpr float PI = 3.14159265358979323846f;
    float T = std::max(1.0f, scene_seconds);
    int N = std::max(1, cam_loops);  // camera revolutions per cycle

    // Bias toward a more spread-out, tilted system so fewer planets crowd the view
    // and read as fully dark (issue: too many whole-planet shadows). Floors only —
    // the user can still push spacing/tilt higher from the Generate tab.
    std::mt19937 erng(static_cast<uint32_t>(p.seed) * 2654435761u + 12345u);
    std::uniform_real_distribution<float> eu(0.0f, 1.0f);

    SystemGenParams q = p;
    q.spacing = std::max(p.spacing, 2.0f);  // keep neighbours comfortably apart in frame
    q.inclination = std::max(p.inclination, 0.4f);
    // Pick the close-up hero type at random each cycle (seed bumps per cycle), so the
    // loop mixes rocky/ocean worlds and gas giants without a predictable A-B-A-B
    // pattern. Moons aren't eligible — the eclipse geometry needs a sun-orbiting hero.
    q.hero_kind = (eu(erng) < 0.5f) ? 1 : 2;
    generate_system(scene, q);  // reuse all the body/texture/ring/moon generation

    // Slow the moons relative to the cycle: generate_system gives them short Keplerian
    // periods that whir around many times per cycle and wreck the calm. Re-time each to
    // ~1-2.5 cycles per orbit so they drift gently. (Rings are disks; skipped here.)
    for (auto& b : scene.bodies)
        if (b.orbit.active && b.orbit.parent != 0)
            if (auto s = std::dynamic_pointer_cast<Sphere>(b.shape))
                if (!s->material.emissive)
                    b.orbit.period = T * (1.0f + 1.5f * eu(erng));

    // Shrink the inner planets (small near the sun, full size out where the
    // foreground gas giant lives). Planets are the non-emissive spheres orbiting
    // the sun (body 0), pushed in orbit order, so index fraction = how far out.
    std::vector<int> planets;
    for (int i = 1; i < static_cast<int>(scene.bodies.size()); ++i)
        if (scene.bodies[i].orbit.parent == 0)
            if (auto s = std::dynamic_pointer_cast<Sphere>(scene.bodies[i].shape))
                if (!s->material.emissive) planets.push_back(i);
    int np = static_cast<int>(planets.size());
    for (int k = 0; k < np; ++k) {
        float frac = (np > 1) ? static_cast<float>(k) / (np - 1) : 1.0f;
        auto s = std::dynamic_pointer_cast<Sphere>(scene.bodies[planets[k]].shape);
        s->radius *= 0.6f + 0.4f * frac;  // inner 0.6x .. outer 1.0x
    }

    int fg = scene.cam.orbit.parent;  // the planet generate_system chose to orbit
    if (fg < 1 || fg >= static_cast<int>(scene.bodies.size())) return;

    // Hide every OTHER sun-orbiting planet at the cut: force it coplanar (normal +Y)
    // and start it at phase 0 so at t = 0 it sits on the +Z axis behind the foreground
    // planet (and the sun), out of view. Re-time to an integer number of orbits per
    // cycle so it returns to exactly that hidden spot at the next cut (t = T), then
    // shrink it so it never reads as large as the sun when it swings into view
    // mid-cycle. Mid-cycle it is only briefly visible off to the side (issue #5 / #6 /
    // #8 together: hidden at the cut, a small glimpse the rest of the time).
    for (int idx : planets) {
        if (idx == fg) continue;
        Orbit& o = scene.bodies[idx].orbit;
        o.normal = Vec3(0, 1, 0);
        o.phase = 0.0f;
        int k = 1 + static_cast<int>(2.0f * eu(erng));  // 1..2 orbits per cycle (gentle)
        o.period = T / k;
        if (auto s = std::dynamic_pointer_cast<Sphere>(scene.bodies[idx].shape))
            s->radius *= 0.7f;  // clearly secondary to the hero / sun
    }

    float r_fg = 1.0f;
    if (auto s = std::dynamic_pointer_cast<Sphere>(scene.bodies[fg].shape))
        r_fg = s->radius;
    float R = scene.bodies[fg].orbit.radius;  // planet orbit radius
    float camdist = 6.0f * r_fg;

    // Hidden sun: keep its angular size well under the planet's. With camdist = 6r,
    // R/8 is comfortably inside r_fg * (R + camdist) / camdist.
    if (auto sun = std::dynamic_pointer_cast<Sphere>(scene.bodies[0].shape))
        sun->radius = std::min(sun->radius, R / 8.0f);

    // Foreground planet onto a circular, coplanar orbit so at t = 0 it sits at
    // (0,0,-R): with e = 0 and normal +Y, orbit_offset_at gives (a sinE, 0, a cosE),
    // so phase (= E at t=0) = PI -> (0,0,-R). It does cam_loops-1 orbits per cycle,
    // so camera and anti-sun realign exactly once per cycle (one eclipse). With
    // cam_loops == 1 the planet is parked at (0,0,-R) (pure camera orbit, no sun
    // motion — the calmest sweep).
    Orbit& po = scene.bodies[fg].orbit;
    if (N >= 2) {
        po.active = true;
        po.eccentricity = 0.0f;
        po.normal = Vec3(0, 1, 0);
        po.phase = PI;
        po.period = T / (N - 1);
    } else {
        po.active = false;
        scene.bodies[fg].shape->center = Vec3(0, 0, -R);
    }

    // Camera orbits the planet in the same plane, cam_loops revolutions per cycle,
    // at a fixed distance + FOV so the framing repeats identically every eclipse.
    SceneCamera& c = scene.cam;
    c.orbit.active = true;
    c.orbit.parent = fg;
    c.orbit.eccentricity = 0.0f;
    c.orbit.normal = Vec3(0, 1, 0);
    c.orbit.phase = PI;                      // eye at (0,0,-camdist) relative to planet
    c.orbit.radius = camdist;
    c.orbit.period = T / N;
    c.vup = Vec3(0, 1, 0);
    c.fov = 50.0f;
    c.look = CamLook::Target;
    c.target = fg;
}

namespace {
json scene_to_json_obj(const Scene& scene) {
    json bodies = json::array();
    for (const auto& b : scene.bodies)
        bodies.push_back(body_to_json(b));

    return json{
        {"camera", to_json(scene.cam)},
        {"light_dir", to_json(scene.light_dir)},
        {"fill_light", scene.fill_light},
        {"light_falloff", scene.light_falloff},
        {"background", to_json(scene.background)},
        {"post", to_json(scene.post)},
        {"resolution", {{"width", scene.width}, {"height", scene.height}}},
        {"bodies", bodies},
    };
}
}  // namespace

std::string scene_to_json(const Scene& scene) {
    return scene_to_json_obj(scene).dump(2);
}

std::string scene_to_json(const Scene& scene, const EditorSettings& settings) {
    json j = scene_to_json_obj(scene);
    j["editor"] = to_json(settings);
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
            // Clouds drift relative to the surface; caps swing with the seasons.
            const Material& mat = s->material;
            s->cloud_angle = s->spin_angle + TWO_PI * t * mat.clouds.drift;
            s->band_angle = s->spin_angle + TWO_PI * t * mat.band_drift;
            const Terrain& tr = mat.terrain;
            s->season_swing = (tr.enabled && tr.season_period != 0.0f)
                ? tr.cap_season * std::sin(TWO_PI * t / tr.season_period)
                : 0.0f;
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

bool save_scene(const Scene& scene, const EditorSettings& settings,
                const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << scene_to_json(scene, settings);
    return static_cast<bool>(f);
}

bool load_scene(Scene& out, EditorSettings& settings, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    try {
        json j = json::parse(text);
        out = scene_from_json(text);
        // Editor section is optional (old scenes won't have it → keep defaults).
        settings = j.contains("editor") ? editor_settings_from_json(j.at("editor"))
                                        : EditorSettings{};
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
