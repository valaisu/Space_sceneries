// gif.h — minimal animated GIF89a writer (single header, public domain).
//
// A small, self-contained encoder in the spirit of the other vendored single
// headers (stb_image_write, nlohmann/json). It is tailored to this project: the
// palette post-process crushes every exported frame to a small set of colors, so
// each frame's local color table is built from its *exact* distinct colors and
// the encode is lossless. If a frame happens to carry more than 256 distinct
// colors (post-process disabled), it falls back to a fixed 3-3-2 bit palette.
//
// LZW uses the standard constant-code-size scheme: emit a Clear code and reset
// before the dictionary would force a larger code size. Output is a touch larger
// than an adaptive encoder but is valid GIF and decodes everywhere.
//
// Usage:
//   GifWriter w;
//   GifBegin(&w, "out.gif", width, height, delayCs);   // delay in centiseconds
//   GifWriteFrame(&w, rgba, width, height, delayCs);    // rgba: 4 bytes/px, top row first
//   ...
//   GifEnd(&w);
//
// Pixel layout matches this project's pack_rgba (bytes R,G,B,A) and stb.

#ifndef GIF_H
#define GIF_H

#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

struct GifWriter {
    std::FILE* file = nullptr;
};

namespace gif_detail {

inline void put_u16(std::FILE* f, int v) {
    std::fputc(v & 0xFF, f);
    std::fputc((v >> 8) & 0xFF, f);
}

// LSB-first bit packer for the LZW code stream.
struct BitWriter {
    std::vector<uint8_t> bytes;
    int bitpos = 0;
    uint8_t cur = 0;
    void put(int code, int nbits) {
        for (int i = 0; i < nbits; ++i) {
            if (code & (1 << i)) cur |= static_cast<uint8_t>(1 << bitpos);
            if (++bitpos == 8) { bytes.push_back(cur); cur = 0; bitpos = 0; }
        }
    }
    void flush() {
        if (bitpos > 0) { bytes.push_back(cur); cur = 0; bitpos = 0; }
    }
};

inline int ceil_log2(int n) {
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    return bits;
}

}  // namespace gif_detail

inline bool GifBegin(GifWriter* w, const char* filename, int width, int height,
                     int /*delayCs*/) {
    w->file = std::fopen(filename, "wb");
    if (!w->file) return false;
    std::FILE* f = w->file;
    std::fwrite("GIF89a", 1, 6, f);
    // Logical screen descriptor: no global color table (each frame brings its own).
    gif_detail::put_u16(f, width);
    gif_detail::put_u16(f, height);
    std::fputc(0x00, f);  // packed: GCT flag = 0
    std::fputc(0x00, f);  // background color index
    std::fputc(0x00, f);  // pixel aspect ratio
    // NETSCAPE2.0 application extension -> loop forever.
    std::fputc(0x21, f);
    std::fputc(0xFF, f);
    std::fputc(0x0B, f);
    std::fwrite("NETSCAPE2.0", 1, 11, f);
    std::fputc(0x03, f);
    std::fputc(0x01, f);
    gif_detail::put_u16(f, 0);  // loop count: 0 = infinite
    std::fputc(0x00, f);        // block terminator
    return true;
}

inline bool GifWriteFrame(GifWriter* w, const uint8_t* rgba, int width, int height,
                          int delayCs) {
    if (!w->file) return false;
    std::FILE* f = w->file;
    const int npx = width * height;

    // --- Build this frame's color table from its distinct colors. ---
    std::unordered_map<uint32_t, int> lut;
    std::vector<uint32_t> colors;  // 0x00RRGGBB
    std::vector<uint8_t> idx(npx);
    bool overflow = false;
    for (int i = 0; i < npx; ++i) {
        uint32_t rgb = (static_cast<uint32_t>(rgba[i * 4 + 0]) << 16) |
                       (static_cast<uint32_t>(rgba[i * 4 + 1]) << 8) |
                       (static_cast<uint32_t>(rgba[i * 4 + 2]));
        auto it = lut.find(rgb);
        if (it != lut.end()) {
            idx[i] = static_cast<uint8_t>(it->second);
        } else if (colors.size() < 256) {
            int id = static_cast<int>(colors.size());
            lut.emplace(rgb, id);
            colors.push_back(rgb);
            idx[i] = static_cast<uint8_t>(id);
        } else {
            overflow = true;
            break;
        }
    }
    if (overflow) {
        // Rare path (post-process off): fixed 3-3-2 bit palette, 256 entries.
        colors.resize(256);
        for (int e = 0; e < 256; ++e) {
            int r = (e >> 5) & 0x7, g = (e >> 2) & 0x7, b = e & 0x3;
            colors[e] = (static_cast<uint32_t>(r * 255 / 7) << 16) |
                        (static_cast<uint32_t>(g * 255 / 7) << 8) |
                        (static_cast<uint32_t>(b * 255 / 3));
        }
        for (int i = 0; i < npx; ++i) {
            int r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
            idx[i] = static_cast<uint8_t>((r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6));
        }
    }

    int tableSize = 1 << gif_detail::ceil_log2(static_cast<int>(colors.size()) < 2
                                                   ? 2
                                                   : static_cast<int>(colors.size()));
    int minCode = gif_detail::ceil_log2(tableSize);
    if (minCode < 2) { minCode = 2; tableSize = 4; }

    // --- Graphic control extension (frame delay; no transparency). ---
    std::fputc(0x21, f);
    std::fputc(0xF9, f);
    std::fputc(0x04, f);
    std::fputc(0x00, f);  // packed: no disposal, no transparency
    gif_detail::put_u16(f, delayCs);
    std::fputc(0x00, f);  // transparent color index (unused)
    std::fputc(0x00, f);  // block terminator

    // --- Image descriptor with a local color table. ---
    std::fputc(0x2C, f);
    gif_detail::put_u16(f, 0);       // left
    gif_detail::put_u16(f, 0);       // top
    gif_detail::put_u16(f, width);
    gif_detail::put_u16(f, height);
    std::fputc(0x80 | (minCode - 1), f);  // LCT flag + table size (2^(n+1))

    for (int e = 0; e < tableSize; ++e) {
        uint32_t c = (e < static_cast<int>(colors.size())) ? colors[e] : 0;
        std::fputc((c >> 16) & 0xFF, f);
        std::fputc((c >> 8) & 0xFF, f);
        std::fputc(c & 0xFF, f);
    }

    // --- LZW (constant code size: Clear/reset before it would have to grow). ---
    int clearCode = 1 << minCode;
    int endCode = clearCode + 1;
    int codeSize = minCode + 1;
    gif_detail::BitWriter bw;
    bw.put(clearCode, codeSize);
    int next = clearCode + 2;
    for (int i = 0; i < npx; ++i) {
        bw.put(idx[i], codeSize);
        if (++next == (1 << codeSize)) {
            bw.put(clearCode, codeSize);
            next = clearCode + 2;
        }
    }
    bw.put(endCode, codeSize);
    bw.flush();

    std::fputc(minCode, f);  // LZW minimum code size
    size_t off = 0;
    while (off < bw.bytes.size()) {
        size_t n = bw.bytes.size() - off;
        if (n > 255) n = 255;
        std::fputc(static_cast<int>(n), f);
        std::fwrite(bw.bytes.data() + off, 1, n, f);
        off += n;
    }
    std::fputc(0x00, f);  // image data block terminator
    return true;
}

inline bool GifEnd(GifWriter* w) {
    if (!w->file) return false;
    std::fputc(0x3B, w->file);  // trailer
    std::fclose(w->file);
    w->file = nullptr;
    return true;
}

#endif  // GIF_H
