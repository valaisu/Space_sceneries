// Headless sanity checks for the core (math, geometry, shading, scene, render).
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "vec3.h"
#include "ray.h"
#include "sphere.h"
#include "disk.h"
#include "camera.h"
#include "render.h"
#include "scene.h"
#include "post.h"

static bool approx(float a, float b) { return std::fabs(a - b) < 1e-5f; }

static void phase1() {
    // Vec3 arithmetic
    Vec3 a(1, 2, 3), b(4, 5, 6);
    assert(approx(dot(a, b), 32.0f));

    Vec3 c = cross(Vec3(1, 0, 0), Vec3(0, 1, 0));  // -> +z
    assert(approx(c.x, 0) && approx(c.y, 0) && approx(c.z, 1));

    Vec3 n = normalize(Vec3(3, 0, 4));
    assert(approx(length(n), 1.0f) && approx(n.x, 0.6f) && approx(n.z, 0.8f));

    Vec3 sum = a + b;
    assert(approx(sum.x, 5) && approx(sum.y, 7) && approx(sum.z, 9));

    // Ray
    Ray r(Vec3(0, 0, 0), Vec3(0, 0, 1));
    assert(approx(r.point_at_parameter(5.0f).z, 5.0f));
}

static void phase2() {
    // --- Intersection math (4.2) ---
    Material white{Vec3(1, 1, 1), false};
    Sphere s(Vec3(0, 0, -5), 1.0f, white);
    Ray fwd(Vec3(0, 0, 0), Vec3(0, 0, -1));
    HitRecord rec;
    assert(s.hit(fwd, 1e-3f, 1e30f, rec));
    assert(approx(rec.t, 4.0f));               // front face at z = -4
    assert(approx(rec.normal.z, 1.0f));        // outward normal points at camera
    assert(!s.hit(Ray(Vec3(0, 0, 0), Vec3(0, 1, 0)), 1e-3f, 1e30f, rec));  // miss

    Disk d(Vec3(0, 0, -5), Vec3(0, 0, 1), 1.0f, 2.0f, white);
    assert(!d.hit(fwd, 1e-3f, 1e30f, rec));    // hits plane at center -> inside inner radius
    Ray edge(Vec3(1.5f, 0, 0), Vec3(0, 0, -1));
    assert(d.hit(edge, 1e-3f, 1e30f, rec) && approx(rec.t, 5.0f));  // within [1,2]

    // --- Camera (4.1) ---
    Camera cam(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0), 90.0f, 2.0f);
    Ray center = cam.get_ray(0.5f, 0.5f);
    assert(approx(center.direction.x, 0) && approx(center.direction.y, 0) &&
           approx(center.direction.z, -1.0f));  // straight ahead

    // --- Shading: background, lit, shadow (4.3/4.4) ---
    World world;
    world.push_back(std::make_shared<Sphere>(Vec3(0, 0, -5), 1.0f,
                                             Material{Vec3(0.8f, 0.3f, 0.3f), false}));
    Vec3 light_dir(0, 0, -1);  // travels -z, so it lights the +z face (toward camera)

    constexpr float AMBIENT = 0.15f;
    auto dir_light = [](Vec3 d) { return std::vector<Light>{Light{true, d, Vec3(1, 1, 1), false}}; };

    Background nostar;
    nostar.density = 0.0f;  // disable stars so a miss is exactly the sky color
    Vec3 bg = ray_color(cam.get_ray(0.0f, 0.0f), world, dir_light(light_dir), AMBIENT,
                        true, nostar);  // corner -> miss
    assert(approx(bg.x, 0.01f) && approx(bg.z, 0.02f));

    Vec3 lit = ray_color(center, world, dir_light(light_dir), AMBIENT, true);  // fully lit
    assert(approx(lit.x, 0.8f) && approx(lit.y, 0.3f) && approx(lit.z, 0.3f));

    // Dark side: front face lit from behind (light travels +z) gets ambient only.
    Vec3 dark = ray_color(center, world, dir_light(Vec3(0, 0, 1)), AMBIENT, true);
    assert(approx(dark.x, 0.8f * AMBIENT) && approx(dark.y, 0.3f * AMBIENT));

    // Emissive ignores lighting.
    World emit;
    emit.push_back(std::make_shared<Sphere>(Vec3(0, 0, -5), 1.0f,
                                            Material{Vec3(1, 1, 0), true}));
    Vec3 sun = ray_color(center, emit, dir_light(Vec3(0, 0, 1)), AMBIENT, true);
    assert(approx(sun.x, 1.0f) && approx(sun.y, 1.0f) && approx(sun.z, 0.0f));

    // Shadow: an occluder between the lit point and the light dims it to ambient.
    // Light is angled (travels down+into screen) so the shadow ray leaves the
    // camera axis; the occluder sits off-axis (y=1) and so misses the primary ray.
    World shadowed;
    shadowed.push_back(std::make_shared<Sphere>(Vec3(0, 0, -5), 1.0f,
                                                Material{Vec3(0.8f, 0.3f, 0.3f), false}));
    shadowed.push_back(std::make_shared<Sphere>(Vec3(0, 1, -3), 0.5f, white));
    Vec3 angled(0, -1, -1);  // to_light = (0,1,1)/sqrt(2)
    Vec3 dim = ray_color(center, shadowed, dir_light(angled), AMBIENT, true);
    assert(approx(dim.x, 0.8f * AMBIENT));  // occluded -> ambient floor only

    // Disabling shadows lets the same occluded point receive its diffuse again.
    Vec3 nosh = ray_color(center, shadowed, dir_light(angled), AMBIENT, false);
    assert(nosh.x > 0.8f * AMBIENT + 1e-3f);

    // A positional light behaves like the directional one when far along -z.
    std::vector<Light> pt{Light{false, Vec3(0, 0, 100), Vec3(1, 1, 1), false}};
    Vec3 plit = ray_color(center, world, pt, AMBIENT, true);
    assert(approx(plit.x, 0.8f) && approx(plit.y, 0.3f));
}

static void phase3() {
    // Build a scene, serialize, parse back, and check the round-trip (5.2).
    Scene scene;
    scene.cam.position = Vec3(0, 2, 3);
    scene.cam.fov = 60.0f;
    scene.light_dir = Vec3(0, -1, -0.5f);
    scene.width = 320;
    scene.height = 240;

    auto earth = std::make_shared<Sphere>(Vec3(0, 0, -5), 1.0f,
                                          Material{Vec3(0.2f, 0.4f, 0.9f), false});
    earth->name = "Earth";
    auto sun = std::make_shared<Sphere>(Vec3(10, 5, -2), 2.0f,
                                        Material{Vec3(1, 1, 0.8f), true});
    sun->name = "Sun";
    auto ring = std::make_shared<Disk>(Vec3(0, 0, -5), Vec3(0, 1, 0.2f), 1.5f, 3.0f,
                                       Material{Vec3(0.7f, 0.6f, 0.5f), false});
    ring->name = "Rings";
    Body moon{earth};
    moon.orbit.active = true;
    moon.orbit.parent = 1;
    moon.orbit.radius = 2.5f;
    moon.orbit.period = 4.0f;
    moon.spin.period = 1.5f;
    scene.bodies = {Body{earth}, Body{sun}, Body{ring}, moon};

    Scene back = scene_from_json(scene_to_json(scene));

    // Scalars / vectors survive.
    assert(approx(back.cam.position.y, 2.0f) && approx(back.cam.fov, 60.0f));
    assert(approx(back.light_dir.z, -0.5f));
    assert(back.width == 320 && back.height == 240);

    // Bodies survive with type, name, geometry, material, orbit, spin.
    assert(back.bodies.size() == 4);
    auto e = std::dynamic_pointer_cast<Sphere>(back.bodies[0].shape);
    assert(e && e->name == "Earth" && approx(e->radius, 1.0f) &&
           approx(e->material.albedo.z, 0.9f) && !e->material.emissive);
    auto s = std::dynamic_pointer_cast<Sphere>(back.bodies[1].shape);
    assert(s && s->name == "Sun" && s->material.emissive);
    auto d = std::dynamic_pointer_cast<Disk>(back.bodies[2].shape);
    assert(d && d->name == "Rings" && approx(d->inner_radius, 1.5f) &&
           approx(d->outer_radius, 3.0f));
    assert(back.bodies[3].orbit.active && back.bodies[3].orbit.parent == 1 &&
           approx(back.bodies[3].orbit.radius, 2.5f) &&
           approx(back.bodies[3].spin.period, 1.5f));

    // Re-serializing the parsed scene yields identical JSON (stable round-trip).
    assert(scene_to_json(back) == scene_to_json(scene));
}

static void phase5() {
    // Render a 3x3 frame: center ray hits a lit sphere, corner misses to background.
    Scene scene;
    scene.width = 3;
    scene.height = 3;
    scene.cam.position = Vec3(0, 0, 0);
    scene.cam.look = CamLook::Direction;
    scene.cam.direction = Vec3(0, 0, -1);
    scene.cam.fov = 90.0f;             // aspect 1
    scene.light_dir = Vec3(0, 0, -1);  // lights the camera-facing hemisphere
    scene.bodies.push_back(Body{std::make_shared<Sphere>(
        Vec3(0, 0, -5), 1.0f, Material{Vec3(0.8f, 0.3f, 0.3f), false})});

    std::vector<uint32_t> px;
    render_scene(scene, 0.0f, px);
    assert(px.size() == 9);

    auto byte = [](uint32_t p, int shift) { return (p >> shift) & 0xFFu; };

    // Center pixel (1,1): exact center ray, fully lit -> albedo at full intensity.
    uint32_t c = px[1 * 3 + 1];
    assert(byte(c, 0) == 204 && byte(c, 8) == 77 && byte(c, 16) == 77);  // R,G,B
    assert(byte(c, 24) == 255);                                          // A

    // Corner pixel (0,0): misses -> near-black background (0.01,0.01,0.02).
    uint32_t bg = px[0];
    assert(byte(bg, 0) == 3 && byte(bg, 8) == 3 && byte(bg, 16) == 5);
}

static void phase6() {
    // Orbit posing: a planet circles the origin in the XZ plane; a moon circles
    // the planet. Default normal (0,1,0) puts angle 0 at +Z, quarter turn at +X.
    Scene scene;
    auto star = std::make_shared<Sphere>(Vec3(0, 0, 0), 1.0f, Material{Vec3(1, 1, 1), true});
    Body sun{star};  // stationary at origin

    auto p = std::make_shared<Sphere>(Vec3(0, 0, 0), 0.5f, Material{Vec3(0, 0, 1), false});
    Body planet{p};
    planet.orbit.active = true;
    planet.orbit.parent = 0;
    planet.orbit.radius = 4.0f;
    planet.orbit.period = 8.0f;

    auto m = std::make_shared<Sphere>(Vec3(0, 0, 0), 0.2f, Material{Vec3(0.5f, 0.5f, 0.5f), false});
    Body moon{m};
    moon.orbit.active = true;
    moon.orbit.parent = 1;  // orbits the planet
    moon.orbit.radius = 1.0f;
    moon.orbit.period = 8.0f;

    scene.bodies = {sun, planet, moon};

    // t = 0: planet at +Z*radius; moon at planet + +Z*1.
    Vec3 p0 = body_world_pos(scene, 1, 0.0f);
    assert(approx(p0.x, 0) && approx(p0.y, 0) && approx(p0.z, 4.0f));
    Vec3 m0 = body_world_pos(scene, 2, 0.0f);
    assert(approx(m0.x, 0) && approx(m0.z, 5.0f));  // 4 (planet) + 1 (moon)

    // t = period/4: quarter turn -> +X.
    Vec3 pq = body_world_pos(scene, 1, 2.0f);
    assert(approx(pq.x, 4.0f) && approx(pq.z, 0.0f));

    // Stationary body stays put; posed world has one shape per body.
    assert(approx(body_world_pos(scene, 0, 3.7f).z, 0.0f));
    World w = world_at_time(scene, 1.0f);
    assert(w.size() == 3);
}

static void kepler_orbit() {
    // Elliptic orbit: semi-major a=4, e=0.5, XZ plane (default normal), parent at a
    // focus. Periapsis (t=0, phase 0) is the near point at +Z*a(1-e); half a period
    // later is apoapsis at -Z*a(1+e).
    Scene scene;
    auto p = std::make_shared<Sphere>(Vec3(0, 0, 0), 0.5f, Material{Vec3(0, 0, 1), false});
    Body planet{p};
    planet.orbit.active = true;
    planet.orbit.parent = -1;
    planet.orbit.radius = 4.0f;
    planet.orbit.period = 8.0f;
    planet.orbit.eccentricity = 0.5f;
    scene.bodies = {planet};

    Vec3 peri = body_world_pos(scene, 0, 0.0f);
    assert(approx(peri.x, 0.0f) && approx(peri.z, 2.0f));   // a(1-e) = 4*0.5
    Vec3 apo = body_world_pos(scene, 0, 4.0f);
    assert(approx(apo.x, 0.0f) && approx(apo.z, -6.0f));    // a(1+e) = 4*1.5
    assert(length(peri) < length(apo));                     // periapsis is closer

    // Equal-area motion: starting at periapsis (fast), a quarter period sweeps PAST
    // the 90 degrees mark a circle would reach (+X, z=0) — so z has gone negative.
    Vec3 q = body_world_pos(scene, 0, 2.0f);
    assert(q.x > 0.0f && q.z < 0.0f);

    // e = 0 reproduces the circular orbit (and round-trips through JSON).
    planet.orbit.eccentricity = 0.0f;
    scene.bodies = {planet};
    Vec3 c0 = body_world_pos(scene, 0, 0.0f);
    assert(approx(c0.z, 4.0f) && approx(c0.x, 0.0f));
    Scene back = scene_from_json(scene_to_json(scene));
    assert(approx(back.bodies[0].orbit.eccentricity, 0.0f));
    Scene s2;
    s2.bodies = {planet};
    s2.bodies[0].orbit.eccentricity = 0.5f;
    Scene b2 = scene_from_json(scene_to_json(s2));
    assert(approx(b2.bodies[0].orbit.eccentricity, 0.5f));
}

static void file_io() {
    // Palette library round-trips through disk.
    std::vector<Vec3> pal{Vec3(0.1f, 0.2f, 0.3f), Vec3(1, 0, 0.5f), Vec3(0, 0.8f, 0.4f)};
    const char* ppath = "test_palette.json";
    assert(save_palette(pal, ppath));
    std::vector<Vec3> back;
    assert(load_palette(back, ppath));
    assert(back.size() == 3 && approx(back[1].x, 1.0f) && approx(back[2].y, 0.8f));
    std::remove(ppath);

    // New Background fields (galactic band + shooting stars) survive scene JSON.
    Scene s;
    s.background.band_enabled = true;
    s.background.band_width = 0.33f;
    s.background.band_density = 0.7f;
    s.background.region_star_boost = 0.4f;
    s.background.shoot_enabled = true;
    s.background.shoot_rate = 2.5f;
    s.background.shoot_seed = 42;
    Scene r = scene_from_json(scene_to_json(s));
    assert(r.background.band_enabled && approx(r.background.band_width, 0.33f) &&
           approx(r.background.band_density, 0.7f));
    assert(approx(r.background.region_star_boost, 0.4f));
    assert(r.background.shoot_enabled && approx(r.background.shoot_rate, 2.5f) &&
           r.background.shoot_seed == 42);
}

static void phase7() {
    // project() must be the exact inverse of get_ray() so overlays align with the
    // raytraced image.
    Camera cam(Vec3(1, 2, 3), Vec3(0, 0, -1), Vec3(0, 1, 0), 60.0f, 1.6f);
    for (float s : {0.2f, 0.5f, 0.85f}) {
        for (float t : {0.1f, 0.5f, 0.9f}) {
            Ray r = cam.get_ray(s, t);
            Vec3 p = r.point_at_parameter(7.0f);
            Projection pr = cam.project(p);
            assert(pr.depth > 0.0f && approx(pr.s, s) && approx(pr.t, t));
        }
    }

    // orbit_ring_point: radius 4 in the XZ plane -> +Z at angle 0, +X at pi/2.
    Scene scene;
    auto p = std::make_shared<Sphere>(Vec3(0, 0, 0), 0.5f, Material{Vec3(0, 0, 1), false});
    Body planet{p};
    planet.orbit.active = true;
    planet.orbit.parent = -1;
    planet.orbit.radius = 4.0f;
    scene.bodies = {planet};

    Vec3 a0 = orbit_ring_point(scene, 0, 0.0f, 0.0f);
    assert(approx(a0.z, 4.0f) && approx(a0.x, 0.0f));
    Vec3 a90 = orbit_ring_point(scene, 0, 0.0f, 1.57079632679f);
    assert(approx(a90.x, 4.0f) && approx(a90.z, 0.0f));
}

static void phase9() {
    // A mottled sphere: the sampled albedo should vary across the surface, and
    // spinning the body should change what's sampled at a fixed hit point.
    Material m{Vec3(1, 1, 1), false};
    m.pattern = 2;            // mottled
    m.detail = Vec3(0, 0, 0);
    Sphere s(Vec3(0, 0, 0), 1.0f, m);

    auto sample = [&](Ray r, float spin) {
        s.spin_angle = spin;
        HitRecord rec;
        bool hit = s.hit(r, 1e-3f, 1e30f, rec);
        assert(hit);
        return rec.material.albedo.x;  // grayscale here (white->black mix)
    };

    // Two different surface points -> generally different texture values.
    float front = sample(Ray(Vec3(0, 0, 5), Vec3(0, 0, -1)), 0.0f);   // hits (0,0,1)
    float side = sample(Ray(Vec3(5, 0, 0), Vec3(-1, 0, 0)), 0.0f);    // hits (1,0,0)
    assert(std::fabs(front - side) > 1e-4f);

    // Same hit point, different spin -> different sampled value.
    float spin0 = sample(Ray(Vec3(0, 0, 5), Vec3(0, 0, -1)), 0.0f);
    float spin1 = sample(Ray(Vec3(0, 0, 5), Vec3(0, 0, -1)), 1.0f);
    assert(std::fabs(spin0 - spin1) > 1e-4f);

    // A solid material is unaffected by spin.
    Sphere solid(Vec3(0, 0, 0), 1.0f, Material{Vec3(0.3f, 0.6f, 0.9f), false});
    HitRecord rec;
    solid.spin_angle = 2.0f;
    solid.hit(Ray(Vec3(0, 0, 5), Vec3(0, 0, -1)), 1e-3f, 1e30f, rec);
    assert(approx(rec.material.albedo.x, 0.3f) && approx(rec.material.albedo.z, 0.9f));
}

static void phase10() {
    // Camera object: Target mode aims at a body; an orbit moves the eye; Direction
    // mode faces a constant world direction regardless of position.
    Scene scene;
    auto star = std::make_shared<Sphere>(Vec3(0, 0, 0), 1.0f, Material{Vec3(1, 1, 1), true});
    scene.bodies = {Body{star}};
    scene.cam.position = Vec3(0, 0, 10);
    scene.cam.look = CamLook::Target;
    scene.cam.target = 0;  // look at the star at the origin

    Camera c = scene_camera(scene, 0.0f, 1.0f);
    Vec3 f = c.forward();  // from (0,0,10) toward origin -> -z
    assert(approx(f.x, 0) && approx(f.y, 0) && approx(f.z, -1.0f));
    assert(approx(c.eye().z, 10.0f));

    // Orbit the camera (radius 5, XZ plane): +Z at t=0, +X a quarter period later.
    scene.cam.orbit.active = true;
    scene.cam.orbit.parent = -1;
    scene.cam.orbit.radius = 5.0f;
    scene.cam.orbit.period = 8.0f;
    Vec3 e0 = camera_eye(scene, 0.0f);
    assert(approx(e0.x, 0) && approx(e0.z, 5.0f));
    Vec3 eq = camera_eye(scene, 2.0f);
    assert(approx(eq.x, 5.0f) && approx(eq.z, 0.0f));

    // Direction mode: constant facing, independent of position.
    scene.cam.orbit.active = false;
    scene.cam.position = Vec3(3, 0, 0);
    scene.cam.look = CamLook::Direction;
    scene.cam.direction = Vec3(0, 0, -1);
    Vec3 fd = scene_camera(scene, 0.0f, 1.0f).forward();
    assert(approx(fd.x, 0.0f) && approx(fd.z, -1.0f));
}

static void ring_ramp() {
    // A ring spanning radius [1,3] with stops at 0 (red), 0.5 (green), 1 (blue).
    // Sampling at known radii should reproduce the stop / midpoint colors.
    Material m{Vec3(0, 0, 0), false};
    m.ring_ramp = {{0.0f, Vec3(1, 0, 0)}, {0.5f, Vec3(0, 1, 0)}, {1.0f, Vec3(0, 0, 1)}};
    Disk d(Vec3(0, 0, 0), Vec3(0, 0, 1), 1.0f, 3.0f, m);

    HitRecord rec;
    auto hit_at = [&](float radius) {
        Ray r(Vec3(radius, 0, 5), Vec3(0, 0, -1));
        bool ok = d.hit(r, 1e-3f, 1e30f, rec);
        assert(ok);
        return rec.material.albedo;
    };
    Vec3 inner = hit_at(1.0f);   // t = 0 -> red
    assert(approx(inner.x, 1.0f) && approx(inner.y, 0.0f));
    Vec3 mid = hit_at(2.0f);     // t = 0.5 -> green
    assert(approx(mid.y, 1.0f) && approx(mid.x, 0.0f) && approx(mid.z, 0.0f));
    Vec3 quarter = hit_at(1.5f); // t = 0.25 -> halfway red->green
    assert(approx(quarter.x, 0.5f) && approx(quarter.y, 0.5f));

    // Empty ramp leaves the solid albedo untouched.
    Material solid{Vec3(0.3f, 0.4f, 0.5f), false};
    Disk plain(Vec3(0, 0, 0), Vec3(0, 0, 1), 1.0f, 3.0f, solid);
    Ray r(Vec3(2, 0, 5), Vec3(0, 0, -1));
    assert(plain.hit(r, 1e-3f, 1e30f, rec));
    assert(approx(rec.material.albedo.x, 0.3f) && approx(rec.material.albedo.z, 0.5f));

    // JSON round-trips the stops.
    Scene scene;
    scene.bodies = {Body{std::make_shared<Disk>(d)}};
    Scene back = scene_from_json(scene_to_json(scene));
    auto bd = std::dynamic_pointer_cast<Disk>(back.bodies[0].shape);
    assert(bd && bd->material.ring_ramp.size() == 3);
    assert(approx(bd->material.ring_ramp[1].pos, 0.5f) &&
           approx(bd->material.ring_ramp[1].color.y, 1.0f));
}

static void starfield() {
    // density=0 -> always the sky color, regardless of direction.
    Background off;
    off.density = 0.0f;
    off.sky = Vec3(0.02f, 0.03f, 0.05f);
    for (Vec3 d : {Vec3(1, 0, 0), Vec3(0, 1, 0.3f), Vec3(-0.4f, 0.2f, -1)}) {
        Vec3 c = background(Ray(Vec3(0, 0, 0), d), off);
        assert(approx(c.x, 0.02f) && approx(c.y, 0.03f) && approx(c.z, 0.05f));
    }

    // density=1 -> every cell has a star; brightness adds light somewhere over the
    // sky floor. Sweep directions and confirm at least one ray lands on a star core.
    Background on;
    on.density = 1.0f;
    on.sky = Vec3(0, 0, 0);
    on.brightness = 1.0f;
    bool any_star = false;
    for (int i = 0; i < 200; ++i) {
        float a = i * 0.314159f;
        Vec3 d(std::cos(a), 0.3f * std::sin(a * 2.0f), std::sin(a));
        Vec3 c = background(Ray(Vec3(0, 0, 0), d), on);
        if (c.x + c.y + c.z > 0.0f) any_star = true;
    }
    assert(any_star);

    // Deterministic per seed: same ray + same params -> identical color.
    Vec3 a = background(Ray(Vec3(0, 0, 0), Vec3(0.3f, 0.5f, -0.8f)), on);
    Vec3 b = background(Ray(Vec3(0, 0, 0), Vec3(0.3f, 0.5f, -0.8f)), on);
    assert(approx(a.x, b.x) && approx(a.y, b.y) && approx(a.z, b.z));

    // JSON round-trips the background params.
    Scene scene;
    scene.background.density = 0.4f;
    scene.background.seed = 99;
    scene.background.tint = Vec3(0.9f, 0.8f, 1.0f);
    scene.background.region_scale = 3.5f;
    scene.background.region_strength = 0.7f;
    scene.background.region_glow = 0.1f;
    Scene back = scene_from_json(scene_to_json(scene));
    assert(approx(back.background.density, 0.4f) && back.background.seed == 99 &&
           approx(back.background.tint.z, 1.0f));
    assert(approx(back.background.region_scale, 3.5f) &&
           approx(back.background.region_strength, 0.7f) &&
           approx(back.background.region_glow, 0.1f));
}

static void surface_texture() {
    Material m;
    m.pattern = 1;
    m.noise_scale = 3.0f;
    m.noise_octaves = 4;
    m.albedo = Vec3(0, 0, 0);
    m.detail = Vec3(1, 1, 1);

    // Deterministic and in range.
    Vec3 a = normalize(Vec3(0.3f, 0.7f, 0.2f));
    float fa = surface_field(m, a);
    assert(fa >= 0.0f && fa <= 1.0f);
    assert(approx(fa, surface_field(m, a)));

    // Continuity: a tiny step on the surface changes the field only slightly.
    Vec3 b = normalize(a + Vec3(0.002f, 0.0f, 0.0f));
    assert(std::fabs(fa - surface_field(m, b)) < 0.05f);

    // No pole pinch: two points near the north pole at different longitudes are
    // genuinely different 3D samples, so the field differs (UV mapping would have
    // crushed them together).
    float n1 = surface_field(m, normalize(Vec3(0.05f, 1.0f, 0.0f)));
    float n2 = surface_field(m, normalize(Vec3(0.0f, 1.0f, 0.05f)));
    assert(std::fabs(n1 - n2) > 1e-4f);

    // Latitude bands: with full band strength the field depends only on latitude
    // (y), so sweeping latitude produces a clear spread of values, while moving
    // along a parallel (same y) leaves it unchanged.
    Material g = m;
    g.band_strength = 1.0f;
    g.band_freq = 4.0f;
    g.warp = 0.0f;
    float lo = 2.0f, hi = -1.0f;
    for (int i = 0; i <= 8; ++i) {
        float y = -0.9f + 0.225f * i;
        float f = surface_field(g, normalize(Vec3(1, y, 0)));
        lo = std::min(lo, f);
        hi = std::max(hi, f);
    }
    assert(hi - lo > 0.5f);  // bands span a wide range across latitude
    float p1 = surface_field(g, normalize(Vec3(1, 0.5f, 0.0f)));
    float p2 = surface_field(g, normalize(Vec3(0.0f, 0.5f, 1.0f)));  // same y
    assert(approx(p1, p2));

    // surface_color: ramp wins when present, else albedo<->detail lerp.
    assert(approx(surface_color(m, 0.0f).x, 0.0f) && approx(surface_color(m, 1.0f).x, 1.0f));
    m.tex_ramp = {{0.0f, Vec3(0.2f, 0, 0)}, {1.0f, Vec3(0.8f, 0, 0)}};
    assert(approx(surface_color(m, 0.5f).x, 0.5f));

    // Terrain: below sea level reads as ocean (blue-dominant), above as land
    // (from the ramp); a high-latitude point picks up the polar cap.
    Material e;
    e.pattern = 2;
    e.noise_scale = 3.0f;
    e.terrain.enabled = true;
    e.terrain.sea_level = 2.0f;             // force everything underwater
    e.terrain.ocean = Vec3(0.1f, 0.2f, 0.6f);
    e.terrain.cap = 2.0f;                   // no caps for now
    Vec3 sea = terrain_color(e, normalize(Vec3(0.3f, 0.2f, 0.5f)), 0.0f);
    assert(sea.z > sea.x && sea.z > sea.y);  // ocean is blue-dominant
    e.terrain.sea_level = -1.0f;            // force everything land
    e.tex_ramp = {{0.0f, Vec3(0.2f, 0.6f, 0.2f)}, {1.0f, Vec3(0.2f, 0.6f, 0.2f)}};
    Vec3 land = terrain_color(e, normalize(Vec3(0.3f, 0.2f, 0.5f)), 0.0f);
    assert(land.y > land.x && land.y > land.z);  // land is green-dominant
    // A near-pole point with caps on goes bright (toward white cap color).
    e.terrain.cap = 0.5f;
    Vec3 pole = terrain_color(e, normalize(Vec3(0.05f, 1.0f, 0.05f)), 0.0f);
    assert(pole.x > 0.7f && pole.z > 0.7f);

    // JSON round-trips the new texture fields.
    Scene scene;
    g.band_var = 0.7f;
    g.terrain.enabled = true;
    g.terrain.sea_level = 0.4f;
    g.clouds.enabled = true;
    g.clouds.drift = 0.05f;
    auto sp = std::make_shared<Sphere>(Vec3(0, 0, 0), 1.0f, g);
    scene.bodies = {Body{sp}};
    Scene back = scene_from_json(scene_to_json(scene));
    auto bs = std::dynamic_pointer_cast<Sphere>(back.bodies[0].shape);
    assert(bs && approx(bs->material.band_strength, 1.0f) &&
           bs->material.noise_octaves == 4 && approx(bs->material.band_freq, 4.0f));
    assert(approx(bs->material.band_var, 0.7f) && bs->material.terrain.enabled &&
           approx(bs->material.terrain.sea_level, 0.4f) &&
           approx(bs->material.clouds.drift, 0.05f));
}

static void lighting() {
    Material red{Vec3(0.8f, 0.3f, 0.3f), false};
    World w;
    w.push_back(std::make_shared<Sphere>(Vec3(0, 0, -5), 1.0f, red));  // front face z=-4
    Ray center(Vec3(0, 0, 0), Vec3(0, 0, -1));
    const float A = 0.15f;

    // Lights add: two identical directional lights are brighter than one.
    std::vector<Light> one{Light{true, Vec3(0, 0, -1), Vec3(1, 1, 1), false}};
    std::vector<Light> two = one; two.push_back(one[0]);
    Vec3 c1 = ray_color(center, w, one, A, true);
    Vec3 c2 = ray_color(center, w, two, A, true);
    assert(c2.x > c1.x + 1e-3f);

    // Distance falloff dims a positional light but stays above the ambient floor.
    std::vector<Light> nf{Light{false, Vec3(0, 0, 10), Vec3(1, 1, 1), false}};
    std::vector<Light> wf{Light{false, Vec3(0, 0, 10), Vec3(1, 1, 1), true}};
    Vec3 cnf = ray_color(center, w, nf, A, true);
    Vec3 cwf = ray_color(center, w, wf, A, true);
    assert(cwf.x < cnf.x - 1e-3f && cwf.x >= 0.8f * A - 1e-4f);

    // Positional shadows only count occluders between the point and the light:
    // an occluder beyond the light casts none.
    World w2;
    w2.push_back(std::make_shared<Sphere>(Vec3(0, 0, -5), 1.0f, red));
    w2.push_back(std::make_shared<Sphere>(Vec3(0, 0, 2), 0.5f, Material{Vec3(1, 1, 1), false}));
    std::vector<Light> lp{Light{false, Vec3(0, 0, -3), Vec3(1, 1, 1), false}};  // light at z=-3
    Vec3 cbehind = ray_color(center, w2, lp, A, true);
    assert(cbehind.x > 0.8f * A + 1e-3f);

    // Scene lighting flags round-trip.
    Scene sc; sc.fill_light = true; sc.light_falloff = true;
    Scene bk = scene_from_json(scene_to_json(sc));
    assert(bk.fill_light && bk.light_falloff);
}

static void atmosphere() {
    Atmosphere a;
    a.enabled = true;
    a.color = Vec3(0.3f, 0.5f, 1.0f);
    a.sunset = Vec3(1.0f, 0.4f, 0.1f);
    a.thickness = 0.4f;
    a.intensity = 1.0f;

    Vec3 V(0, 0, 1);          // viewer looks down -z, view dir toward camera = +z
    Vec3 sun(1, 0, 0);        // sun to the +x side

    // Face-on, sun-lit point: little/no rim (normal ~ view dir).
    Vec3 center = atmosphere_glow(a, Vec3(0, 0, 1), V, sun);
    // Silhouette point on the sun side: strong rim glow.
    Vec3 limb = atmosphere_glow(a, Vec3(0.98f, 0, 0.2f), V, sun);
    float cmag = center.x + center.y + center.z;
    float lmag = limb.x + limb.y + limb.z;
    assert(lmag > cmag + 0.05f);     // rim brightens toward the silhouette

    // Night-side silhouette (facing away from the sun): glow fades out.
    Vec3 night = atmosphere_glow(a, Vec3(-0.98f, 0, 0.2f), V, sun);
    assert(night.x + night.y + night.z < lmag * 0.5f);

    // Terminator rim (normal perpendicular to sun) tints toward sunset: more red
    // than blue, unlike the day limb which is blue-dominant.
    Vec3 term = atmosphere_glow(a, normalize(Vec3(0.1f, 0, 1.0f)), V, sun);
    assert(term.x > term.z);         // sunset (red) dominates at the terminator
    assert(limb.z > limb.x);         // day limb is blue-dominant

    // Disabled / off contributes nothing in the renderer: round-trips too.
    Material m{Vec3(0.5f, 0.5f, 0.5f), false};
    m.atmosphere = a;
    Scene scene;
    scene.bodies = {Body{std::make_shared<Sphere>(Vec3(0, 0, 0), 1.0f, m)}};
    Scene back = scene_from_json(scene_to_json(scene));
    auto bs = std::dynamic_pointer_cast<Sphere>(back.bodies[0].shape);
    assert(bs && bs->material.atmosphere.enabled &&
           approx(bs->material.atmosphere.sunset.x, 1.0f) &&
           approx(bs->material.atmosphere.thickness, 0.4f));
}

static void postprocess() {
    // generate_palette yields exactly palette_size colors, endpoints matching bases.
    PostProcess pp;
    pp.base_a = Vec3(0, 0, 0);
    pp.base_b = Vec3(1, 1, 1);
    pp.palette_size = 5;
    generate_palette(pp);
    assert(pp.palette.size() == 5);
    assert(approx(pp.palette.front().x, 0.0f) && approx(pp.palette.back().x, 1.0f));

    // Seeded generation is reproducible; a different seed gives a different palette.
    auto same_pal = [](const std::vector<Vec3>& A, const std::vector<Vec3>& B) {
        if (A.size() != B.size()) return false;
        for (size_t i = 0; i < A.size(); ++i)
            if (!(approx(A[i].x, B[i].x) && approx(A[i].y, B[i].y) && approx(A[i].z, B[i].z)))
                return false;
        return true;
    };
    PostProcess g1; g1.anchor_count = 0; g1.randomness = 0.6f; g1.palette_size = 6;
    g1.palette_seed = 42;
    PostProcess g2 = g1;
    generate_palette(g1);
    generate_palette(g2);
    assert(g1.palette.size() == 6 && same_pal(g1.palette, g2.palette));  // same seed -> identical
    PostProcess g3 = g1; g3.palette_seed = 43; generate_palette(g3);
    assert(!same_pal(g3.palette, g1.palette));  // different seed -> different

    // A 2x2 RGBA buffer of mixed grays. With a black/white palette, no dither,
    // no blur: every pixel snaps to the nearest of black or white, deterministically.
    auto pack = [](Vec3 c) {
        auto b = [](float v) { return static_cast<uint32_t>(v * 255.0f + 0.5f); };
        return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | (0xFFu << 24);
    };
    PostProcess bw;
    bw.palette = {Vec3(0, 0, 0), Vec3(1, 1, 1)};
    bw.blur_radius = 0.0f;
    bw.dither = DitherMode::None;
    std::vector<uint32_t> px = {pack(Vec3(0.1f, 0.1f, 0.1f)), pack(Vec3(0.9f, 0.9f, 0.9f)),
                                pack(Vec3(0.4f, 0.4f, 0.4f)), pack(Vec3(0.6f, 0.6f, 0.6f))};
    std::vector<uint32_t> a = px, b = px;
    apply_post(bw, 2, 2, a);
    apply_post(bw, 2, 2, b);
    assert(a == b);  // deterministic
    assert((a[0] & 0xFFFFFF) == 0x000000);   // 0.1 -> black
    assert((a[1] & 0xFFFFFF) == 0xFFFFFF);   // 0.9 -> white
    assert((a[2] & 0xFFFFFF) == 0x000000);   // 0.4 -> black (nearer)
    assert((a[3] & 0xFFFFFF) == 0xFFFFFF);   // 0.6 -> white (nearer)

    // Ordered dither is also deterministic and stays within the palette.
    PostProcess od = bw;
    od.dither = DitherMode::Ordered;
    std::vector<uint32_t> d1 = px, d2 = px;
    apply_post(od, 2, 2, d1);
    apply_post(od, 2, 2, d2);
    assert(d1 == d2);
    for (uint32_t p : d1) {
        uint32_t rgb = p & 0xFFFFFF;
        assert(rgb == 0x000000 || rgb == 0xFFFFFF);
    }

    // Disabled or empty palette leaves the buffer untouched.
    PostProcess off = bw;
    off.enabled = false;
    std::vector<uint32_t> untouched = px;
    apply_post(off, 2, 2, untouched);
    assert(untouched == px);

    // JSON round-trips the post settings.
    Scene scene;
    scene.post = od;
    scene.post.iterations = 3;
    scene.post.anchor_count = 1;
    scene.post.randomness = 0.3f;
    scene.post.palette_seed = 7;
    Scene back = scene_from_json(scene_to_json(scene));
    assert(back.post.iterations == 3 && back.post.dither == DitherMode::Ordered &&
           back.post.palette.size() == 2);
    assert(back.post.anchor_count == 1 && approx(back.post.randomness, 0.3f) &&
           back.post.palette_seed == 7);
}

// The Generate tab's key promise: a moon stays closer to the planet it orbits
// than to any other planet or the sun, at every point in its orbit.
static void system_gen() {
    for (int seed = 1; seed <= 8; ++seed) {
        SystemGenParams p;
        p.seed = seed;
        p.planet_count = 6;
        p.max_moons = 3;
        Scene s;
        generate_system(s, p);
        assert(s.bodies.size() >= 1 && s.bodies[0].shape->name == "Sun");

        // Indices of the major bodies (sun + planets), i.e. bodies orbiting the sun
        // plus the sun itself. Moons orbit a planet (parent != 0).
        std::vector<int> majors;
        for (int i = 0; i < static_cast<int>(s.bodies.size()); ++i) {
            const Orbit& o = s.bodies[i].orbit;
            if (!o.active || o.parent == 0) majors.push_back(i);  // sun or a planet
        }

        for (float t = 0.0f; t < 40.0f; t += 1.7f) {
            for (int i = 0; i < static_cast<int>(s.bodies.size()); ++i) {
                const Orbit& o = s.bodies[i].orbit;
                if (!o.active || o.parent == 0) continue;  // only moons
                Vec3 moon = body_world_pos(s, i, t);
                float d_parent = length(moon - body_world_pos(s, o.parent, t));
                for (int m : majors) {
                    if (m == o.parent) continue;
                    float d = length(moon - body_world_pos(s, m, t));
                    assert(d > d_parent);  // parent is strictly the closest major body
                }
            }
        }
    }
}

int main() {
    phase1();
    phase2();
    phase3();
    phase5();
    phase6();
    kepler_orbit();
    file_io();
    phase7();
    phase9();
    phase10();
    ring_ramp();
    starfield();
    surface_texture();
    lighting();
    atmosphere();
    postprocess();
    system_gen();
    std::printf("Phase 1-3,5,6,7,9,10 + kepler + file io + ring ramp + starfield + texture + lighting + atmosphere + post + system-gen checks passed.\n");
    return 0;
}
