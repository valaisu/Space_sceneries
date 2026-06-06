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
    for (int y = 0; y < h; ++y)             // horizontal
        for (int x = 0; x < w; ++x) {
            Vec3 sum(0, 0, 0);
            for (int k = -r; k <= r; ++k) sum = sum + at(img, x + k, y);
            tmp[static_cast<size_t>(y) * w + x] = sum * inv;
        }
    for (int y = 0; y < h; ++y)             // vertical
        for (int x = 0; x < w; ++x) {
            Vec3 sum(0, 0, 0);
            for (int k = -r; k <= r; ++k) sum = sum + at(tmp, x, y + k);
            img[static_cast<size_t>(y) * w + x] = sum * inv;
        }
}

}  // namespace

void generate_palette(PostProcess& pp) {
    pp.palette.clear();
    int n = std::max(1, pp.palette_size);
    Vec3 a = rgb_to_hsv(clamp01(pp.base_a));
    Vec3 b = rgb_to_hsv(clamp01(pp.base_b));
    // Take the shorter way around the hue wheel so the ramp stays harmonic.
    float dh = b.x - a.x;
    if (dh > 0.5f) dh -= 1.0f;
    if (dh < -0.5f) dh += 1.0f;
    for (int i = 0; i < n; ++i) {
        float t = (n == 1) ? 0.0f : static_cast<float>(i) / (n - 1);
        float hue = a.x + dh * t;
        hue -= std::floor(hue);
        Vec3 hsv(hue, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
        pp.palette.push_back(clamp01(hsv_to_rgb(hsv)));
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
