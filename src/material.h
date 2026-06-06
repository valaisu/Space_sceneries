#pragma once

#include "vec3.h"

// Phase 1 / 3.4: Minimal material — no PBR, just what the pixel-art shading needs.
// Phase 9: an optional procedural surface pattern that mixes `albedo` with
// `detail` across the sphere, so self-rotation becomes visible.
struct Material {
    Vec3 albedo;
    bool emissive = false;  // if true, shading ignores lighting/shadows (sun, self-lit bodies)

    int pattern = 0;        // 0 = solid, 1 = stripes (longitudinal), 2 = mottled
    Vec3 detail{0, 0, 0};   // second color the pattern mixes toward
};
