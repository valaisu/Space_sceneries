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
    int palette_size = 8;

    float blur_radius = 1.0f;          // box-blur radius in px (0 = no blur)
    DitherMode dither = DitherMode::Ordered;
    int iterations = 1;                // repeat the blur+quantize pass N times
};

// (Re)fill pp.palette with `palette_size` colors interpolated base_a -> base_b
// in HSV, giving a harmonic ramp. Overwrites any existing swatches.
void generate_palette(PostProcess& pp);

// Apply the pipeline in place over an RGBA8 buffer (one uint32_t per pixel, byte
// order R,G,B,A; row 0 = top). No-op when disabled or the palette is empty.
void apply_post(const PostProcess& pp, int w, int h, std::vector<uint32_t>& pixels);
