#include <algorithm>
#include <cmath>

#include "post.h"

namespace {

Vec3 clamp01(Vec3 c) {
    return Vec3(std::clamp(c.x, 0.0f, 1.0f), std::clamp(c.y, 0.0f, 1.0f),
                std::clamp(c.z, 0.0f, 1.0f));
}

// --- HSV <-> RGB (for harmonic palette generation) -------------------------
Vec3 rgb_to_hsv(Vec3 c) {
    float mx = std::max({c.x, c.y, c.z});
    float mn = std::min({c.x, c.y, c.z});
    float d = mx - mn;
    float h = 0.0f;
    if (d > 1e-6f) {
        if (mx == c.x)      h = std::fmod((c.y - c.z) / d, 6.0f);
        else if (mx == c.y) h = (c.z - c.x) / d + 2.0f;
        else                h = (c.x - c.y) / d + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    float s = (mx > 1e-6f) ? d / mx : 0.0f;
    return Vec3(h, s, mx);
}

Vec3 hsv_to_rgb(Vec3 c) {
    float h = c.x * 6.0f, s = c.y, v = c.z;
    float i = std::floor(h);
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (static_cast<int>(i) % 6) {
        case 0: return Vec3(v, t, p);
        case 1: return Vec3(q, v, p);
        case 2: return Vec3(p, v, t);
        case 3: return Vec3(p, q, v);
        case 4: return Vec3(t, p, v);
        default: return Vec3(v, p, q);
    }
}

// --- packing ---------------------------------------------------------------
Vec3 unpack(uint32_t px) {
    return Vec3((px & 0xFF) / 255.0f, ((px >> 8) & 0xFF) / 255.0f,
                ((px >> 16) & 0xFF) / 255.0f);
}
uint32_t pack(Vec3 c) {
    auto b = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | (0xFFu << 24);
}

float dist2(Vec3 a, Vec3 b) {
    Vec3 d = a - b;
    return dot(d, d);
}

// Indices of the nearest and second-nearest palette colors to `c`.
void two_nearest(const std::vector<Vec3>& pal, Vec3 c, int& i0, int& i1) {
    float d0 = 1e30f, d1 = 1e30f;
    i0 = 0; i1 = 0;
    for (int i = 0; i < static_cast<int>(pal.size()); ++i) {
        float d = dist2(pal[i], c);
        if (d < d0) { d1 = d0; i1 = i0; d0 = d; i0 = i; }
        else if (d < d1) { d1 = d; i1 = i; }
    }
}

// 4x4 Bayer matrix, normalized to (0,1).
float bayer(int x, int y) {
    static const int m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (m[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
}

float hash_rand(int x, int y, int iter) {
    float h = std::sin(x * 12.9898f + y * 78.233f + iter * 37.719f) * 43758.5453f;
    return h - std::floor(h);
}

// Separable box blur of radius r over a Vec3 image.
void box_blur(std::vector<Vec3>& img, int w, int h, int r) {
    if (r <= 0) return;
    std::vector<Vec3> tmp(img.size());
    auto at = [&](const std::vector<Vec3>& s, int x, int y) -> Vec3 {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return s[static_cast<size_t>(y) * w + x];
    };
    float inv = 1.0f / (2 * r + 1);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y)             // horizontal
        for (int x = 0; x < w; ++x) {
            Vec3 sum(0, 0, 0);
            for (int k = -r; k <= r; ++k) sum = sum + at(img, x + k, y);
            tmp[static_cast<size_t>(y) * w + x] = sum * inv;
        }
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y)             // vertical
        for (int x = 0; x < w; ++x) {
            Vec3 sum(0, 0, 0);
            for (int k = -r; k <= r; ++k) sum = sum + at(tmp, x, y + k);
            img[static_cast<size_t>(y) * w + x] = sum * inv;
        }
}

}  // namespace

namespace {
// Tiny LCG so generation is reproducible from `palette_seed`.
struct Rng {
    uint32_t s;
    float next() {  // [0,1)
        s = s * 1664525u + 1013904223u;
        return (s >> 8) * (1.0f / 16777216.0f);
    }
};
// A harmonic hue offset (analogous / triadic / complementary) chosen by the rng.
float harmonic_offset(Rng& rng) {
    static const float offs[5] = {0.083f, -0.083f, 0.333f, -0.333f, 0.5f};
    return offs[static_cast<int>(rng.next() * 5.0f) % 5];
}
}  // namespace

void generate_palette(PostProcess& pp) {
    pp.palette.clear();
    int n = std::max(1, pp.palette_size);
    Rng rng{static_cast<uint32_t>(pp.palette_seed) * 2654435761u + 1u};
    const float r = std::max(0.0f, pp.randomness);
    auto jitter = [&](Vec3& hsv) {  // perturb a swatch — jitter, not scatter
        if (r <= 0.0f) return;
        hsv.x += (rng.next() - 0.5f) * 0.05f * r;
        hsv.y += (rng.next() - 0.5f) * 0.15f * r;
        hsv.z += (rng.next() - 0.5f) * 0.15f * r;
    };
    auto push_hsv = [&](Vec3 hsv) {
        hsv.x -= std::floor(hsv.x);
        hsv.y = std::clamp(hsv.y, 0.0f, 1.0f);
        hsv.z = std::clamp(hsv.z, 0.0f, 1.0f);
        pp.palette.push_back(clamp01(hsv_to_rgb(hsv)));
    };

    // Rule-based harmonic schemes (1-4): pick the hue(s) by a color-wheel rule from
    // base_a's hue, then give each hue its own dark -> light ramp so the palette
    // always spans brightness (light+dark of each hue; usable dark for backgrounds).
    if (pp.scheme != 0) {
        Vec3 base = rgb_to_hsv(clamp01(pp.base_a));
        float h0 = base.x + pp.base_hue;
        float sat = (base.y < 0.1f) ? 0.6f : base.y;  // near-grey base: use a usable saturation
        std::vector<float> offs;
        switch (pp.scheme) {
            case 1: offs = {0.0f}; break;                            // monochromatic
            case 2: offs = {0.0f, 0.5f}; break;                      // complementary
            case 3: offs = {0.0f, 1.0f / 3, 2.0f / 3}; break;        // triadic
            default: offs = {-1.0f / 12, 0.0f, 1.0f / 12}; break;    // analogous
        }
        const int m = static_cast<int>(offs.size());
        const float v_dark = 0.12f, v_light = 0.97f;
        const float s_dark = std::min(1.0f, sat + 0.10f);   // shadows a touch more saturated
        const float s_light = std::max(0.12f, sat - 0.30f);  // highlights desaturate
        for (int j = 0; j < m; ++j) {
            int c = n / m + (j < n % m ? 1 : 0);  // split swatches across the hues
            for (int k = 0; k < c; ++k) {
                float t = (c == 1) ? 0.5f : static_cast<float>(k) / (c - 1);  // dark -> light
                Vec3 hsv(h0 + offs[j], s_dark + (s_light - s_dark) * t,
                         v_dark + (v_light - v_dark) * t);
                jitter(hsv);
                push_hsv(hsv);
            }
        }
        return;
    }

    // Build the HSV key points the ramp passes through. Usually two endpoints;
    // with three anchors the ramp bends through base_c, so it can span three
    // distinct hues (blue+green+orange) — a triangle, not a single line.
    std::vector<Vec3> keys;
    if (pp.anchor_count <= 0) {  // fully seed-chosen harmonic pair
        float h = rng.next();
        float s = 0.5f + 0.4f * rng.next();
        keys.push_back(Vec3(h, s, 0.15f + 0.2f * rng.next()));
        keys.push_back(Vec3(h + harmonic_offset(rng),
                            s * (0.6f + 0.3f * rng.next()), 0.9f));
    } else if (pp.anchor_count == 1) {  // ramp around base_a's hue
        Vec3 base = rgb_to_hsv(clamp01(pp.base_a));
        keys.push_back(Vec3(base.x, base.y, std::max(0.15f, base.z * 0.4f)));
        keys.push_back(Vec3(base.x + harmonic_offset(rng),
                            std::clamp(base.y * 0.8f, 0.0f, 1.0f),
                            std::min(1.0f, base.z * 1.2f + 0.3f)));
    } else if (pp.anchor_count == 2) {  // classic base_a -> base_b
        keys.push_back(rgb_to_hsv(clamp01(pp.base_a)));
        keys.push_back(rgb_to_hsv(clamp01(pp.base_b)));
    } else {  // three anchors: base_a -> base_b -> base_c
        keys.push_back(rgb_to_hsv(clamp01(pp.base_a)));
        keys.push_back(rgb_to_hsv(clamp01(pp.base_b)));
        keys.push_back(rgb_to_hsv(clamp01(pp.base_c)));
    }

    // Unwrap each segment's hue to the shorter way around the wheel, accumulating
    // into a continuous (possibly multi-turn) path we can lerp along.
    std::vector<float> hue(keys.size());
    hue[0] = keys[0].x;
    for (size_t k = 1; k < keys.size(); ++k) {
        float d = keys[k].x - keys[k - 1].x;
        if (d > 0.5f) d -= 1.0f;
        if (d < -0.5f) d += 1.0f;
        hue[k] = hue[k - 1] + d;
    }

    // Spread fans the two-anchor ramp across a wider hue arc (so Randomize can
    // sweep blue->green->orange). Skipped for three anchors so the hand-picked
    // hues stay exact.
    if (keys.size() == 2 && pp.spread > 0.0f) {
        float d = hue[1] - hue[0];
        float sgn = (std::fabs(d) < 1e-4f) ? 1.0f : (d >= 0.0f ? 1.0f : -1.0f);
        hue[1] += sgn * std::clamp(pp.spread, 0.0f, 1.0f) * 0.85f;
    }

    const int segs = static_cast<int>(keys.size()) - 1;
    for (int i = 0; i < n; ++i) {
        float t = (n == 1) ? 0.0f : static_cast<float>(i) / (n - 1);
        float u = t * segs;  // position along the (possibly multi-) segment path
        int seg = std::min(static_cast<int>(u), segs - 1);
        float lt = u - seg;
        const Vec3& k0 = keys[seg];
        const Vec3& k1 = keys[seg + 1];
        Vec3 hsv(hue[seg] + (hue[seg + 1] - hue[seg]) * lt,
                 k0.y + (k1.y - k0.y) * lt, k0.z + (k1.z - k0.z) * lt);
        hsv.x += pp.base_hue;  // rotate the whole ramp to the chosen base hue
        jitter(hsv);
        push_hsv(hsv);
    }
}

void apply_post(const PostProcess& pp, int w, int h, std::vector<uint32_t>& pixels) {
    if (!pp.enabled || pp.palette.empty()) return;
    if (w <= 0 || h <= 0) return;

    std::vector<Vec3> img(pixels.size());
    for (size_t i = 0; i < pixels.size(); ++i) img[i] = unpack(pixels[i]);

    int iters = std::max(1, pp.iterations);
    int radius = std::max(0, static_cast<int>(pp.blur_radius + 0.5f));
    for (int it = 0; it < iters; ++it) {
        box_blur(img, w, h, radius);
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y) * w + x;
                int i0, i1;
                two_nearest(pp.palette, img[idx], i0, i1);
                Vec3 c0 = pp.palette[i0];
                if (pp.dither == DitherMode::None || i0 == i1) {
                    img[idx] = c0;
                    continue;
                }
                // Blend factor: how far the pixel sits from c0 toward c1, by
                // projecting onto the c0->c1 segment. Threshold it against the
                // dither pattern to pick one of the two nearest colors.
                Vec3 c1 = pp.palette[i1];
                Vec3 seg = c1 - c0;
                float len2 = dot(seg, seg);
                float tblend = (len2 > 1e-6f) ? dot(img[idx] - c0, seg) / len2 : 0.0f;
                tblend = std::clamp(tblend, 0.0f, 1.0f);
                float th = (pp.dither == DitherMode::Ordered) ? bayer(x, y)
                                                              : hash_rand(x, y, it);
                img[idx] = (tblend > th) ? c1 : c0;
            }
    }

    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = pack(img[i]);
}
