#pragma once

#include <memory>

#include "vec3.h"
#include "hittable.h"

// Phase 6: animation attributes for a scene body.

// Circular orbit around a parent body (or the world origin if parent < 0).
// The orbit lies in the plane whose normal is `normal` (default = horizontal, Y up).
struct Orbit {
    bool active = false;
    int parent = -1;       // index into Scene::bodies, or -1 for the world origin
    float radius = 5.0f;   // semi-major axis (a); the orbit radius when eccentricity = 0
    float period = 10.0f;  // seconds per revolution
    float phase = 0.0f;    // mean-anomaly offset at t = 0, radians
    Vec3 normal{0, 1, 0};  // orbital-plane normal; tilt this for inclined orbits
    // Kepler eccentricity (0 = circle). The parent sits at a focus and the body
    // moves faster at periapsis (equal-area). 0 reproduces the old circular orbit.
    float eccentricity = 0.0f;
};

// Self-rotation about `axis`. Stored now; becomes visible once spheres are
// textured (Phase 9).
struct Spin {
    Vec3 axis{0, 1, 0};
    float period = 0.0f;  // seconds per rotation; 0 = no spin
};

// A scene entity: a primitive plus its motion. `shape->center` is the body's
// static position when not orbiting; for orbiting bodies it is computed from the
// orbit each frame.
struct Body {
    std::shared_ptr<Hittable> shape;
    Orbit orbit;
    Spin spin;
};
