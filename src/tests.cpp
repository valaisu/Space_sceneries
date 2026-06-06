// Headless sanity checks for the core (math, geometry, shading, scene, render).
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

    Vec3 bg = ray_color(cam.get_ray(0.0f, 0.0f), world, light_dir);  // corner -> miss
    assert(approx(bg.x, 0.01f) && approx(bg.z, 0.02f));

    Vec3 lit = ray_color(center, world, light_dir);  // hits front face, fully lit
    assert(approx(lit.x, 0.8f) && approx(lit.y, 0.3f) && approx(lit.z, 0.3f));

    // Dark side: front face lit from behind (light travels +z) gets ambient only.
    constexpr float AMBIENT = 0.15f;
    Vec3 dark = ray_color(center, world, Vec3(0, 0, 1));
    assert(approx(dark.x, 0.8f * AMBIENT) && approx(dark.y, 0.3f * AMBIENT));

    // Emissive ignores lighting.
    World emit;
    emit.push_back(std::make_shared<Sphere>(Vec3(0, 0, -5), 1.0f,
                                            Material{Vec3(1, 1, 0), true}));
    Vec3 sun = ray_color(center, emit, Vec3(0, 0, 1));  // light pointing away
    assert(approx(sun.x, 1.0f) && approx(sun.y, 1.0f) && approx(sun.z, 0.0f));

    // Shadow: an occluder between the lit point and the light dims it to 0.1.
    // Light is angled (travels down+into screen) so the shadow ray leaves the
    // camera axis; the occluder sits off-axis (y=1) and so misses the primary ray.
    World shadowed;
    shadowed.push_back(std::make_shared<Sphere>(Vec3(0, 0, -5), 1.0f,
                                                Material{Vec3(0.8f, 0.3f, 0.3f), false}));
    shadowed.push_back(std::make_shared<Sphere>(Vec3(0, 1, -3), 0.5f, white));
    Vec3 angled(0, -1, -1);  // to_light = (0,1,1)/sqrt(2)
    Vec3 dim = ray_color(center, shadowed, angled);
    assert(approx(dim.x, 0.8f * AMBIENT));  // occluded -> ambient floor only
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

int main() {
    phase1();
    phase2();
    phase3();
    phase5();
    phase6();
    phase7();
    phase9();
    phase10();
    std::printf("Phase 1-3,5,6,7,9,10 sanity checks passed.\n");
    return 0;
}
