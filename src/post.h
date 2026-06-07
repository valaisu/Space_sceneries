#pragma once

#include <cstdint>
#include <vector>

#include "vec3.h"

// Stage 3: palette post-process pipeline. Runs over the final RGBA8 buffer
// (after stars are composited) and is used by both the editor viewport and the
// PNG export, so the stylized look is identical in preview and output.
//
// Per iteration: blur (smoothing) -> quantize each pixel to the nearest palette
// color with optional dithering between the two nearest colors.

enum class DitherMode { None = 0, Ordered = 1, Random = 2 };

struct PostProcess {
    bool enabled = true;

    // Colors quantized to. Generated from base_a/base_b (see generate_palette),
    // then hand-editable. Empty palette = quantize is a no-op (raw colors pass).
    std::vector<Vec3> palette;

    // Generation endpoints: a harmonic ramp is built interpolating a -> b in HSV.
    Vec3 base_a{0.08f, 0.10f, 0.24f};  // dark / cool
    Vec3 base_b{0.96f, 0.86f, 0.66f};  // light / warm
    Vec3 base_c{0.18f, 0.55f, 0.28f};  // third anchor (anchor_count == 3) — e.g. a green
    int palette_size = 8;

    // Generation scheme. 0 = Anchors (interpolate base_a..base_c — the classic line,
    // uses anchor_count/spread). 1 = Monochromatic, 2 = Complementary, 3 = Triadic,
    // 4 = Analogous: rule-based harmonies that take their hue(s) from base_a's hue
    // (+ base_hue) and ramp each hue **dark -> light**, so the palette always spans
    // brightness (so backgrounds/shadows always have a dark swatch to map to).
    int scheme = 0;

    // How many of the base colors anchor generation: 3 = base_a -> base_b -> base_c
    // (a bent path through three hues, e.g. blue+green+orange), 2 = base_a -> base_b
    // ramp (classic), 1 = ramp around base_a's hue, 0 = a fully seed-chosen hue pair.
    int anchor_count = 2;
    float base_hue = 0.0f;    // hue rotation (0..1) applied to the whole ramp — recenters it on green/blue/orange/etc.
    float spread = 0.0f;      // 0 = straight line between the two anchors (classic); >0 fans the ramp across a wider hue arc. Ignored when anchor_count == 3.
    float randomness = 0.0f;  // 0 = deterministic ramp; >0 jitters each swatch in HSV
    int palette_seed = 0;     // PRNG seed; bump it (Randomize) for a new variation

    float blur_radius = 1.0f;          // box-blur radius in px (0 = no blur)
    DitherMode dither = DitherMode::Ordered;
    int iterations = 1;                // repeat the blur+quantize pass N times
};

// (Re)fill pp.palette with `palette_size` colors interpolated along the anchor
// path in HSV (base_a -> base_b, or -> base_c with three anchors), optionally
// fanned wider by `spread`. Gives a harmonic ramp. Overwrites existing swatches.
void generate_palette(PostProcess& pp);

// Apply the pipeline in place over an RGBA8 buffer (one uint32_t per pixel, byte
// order R,G,B,A; row 0 = top). No-op when disabled or the palette is empty.
void apply_post(const PostProcess& pp, int w, int h, std::vector<uint32_t>& pixels);
