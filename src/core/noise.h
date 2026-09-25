// Tileable procedural noise used to generate textures at startup.
#pragma once
#include "core/math.h"

namespace noise {

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline uint32_t hash3(int x, int y, int z, uint32_t seed) {
    return hash32((uint32_t)x * 0x8da6b343u ^ hash32((uint32_t)y * 0xd8163841u ^ hash32((uint32_t)z * 0xcb1ab31fu ^ seed)));
}
inline float hashf(int x, int y, int z, uint32_t seed) { return (hash3(x, y, z, seed) & 0xFFFFFFu) * (1.0f / 16777215.0f); }
inline int wrapi(int v, int p) { if (p <= 0) return v; int r = v % p; return r < 0 ? r + p : r; }
inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

// 2D value noise in [0,1], periodic with period p lattice cells (p<=0: not periodic).
inline float value2(float x, float y, int p, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    float u = fade(fx), v = fade(fy);
    int x0 = wrapi(xi, p), x1 = wrapi(xi + 1, p), y0 = wrapi(yi, p), y1 = wrapi(yi + 1, p);
    float a = hashf(x0, y0, 0, seed), b = hashf(x1, y0, 0, seed), c = hashf(x0, y1, 0, seed), d = hashf(x1, y1, 0, seed);
    return lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}

// 2D gradient noise in approx [-1,1], periodic.
inline float perlin2(float x, float y, int p, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    auto grad = [&](int ix, int iy, float dx, float dy) {
        uint32_t h = hash3(wrapi(ix, p), wrapi(iy, p), 7, seed);
        float a = (h & 0xFFFF) * (kTwoPi / 65536.0f);
        return std::cos(a) * dx + std::sin(a) * dy;
    };
    float n00 = grad(xi, yi, fx, fy), n10 = grad(xi + 1, yi, fx - 1, fy);
    float n01 = grad(xi, yi + 1, fx, fy - 1), n11 = grad(xi + 1, yi + 1, fx - 1, fy - 1);
    float u = fade(fx), v = fade(fy);
    return lerpf(lerpf(n00, n10, u), lerpf(n01, n11, u), v) * 1.414f;
}

// Fractal sum of perlin noise, result roughly in [-1,1]. Base period doubles per octave.
inline float fbm2(float x, float y, int octaves, int period, uint32_t seed, float gain = 0.5f) {
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += perlin2(x, y, period, seed + i * 131u) * amp;
        norm += amp;
        amp *= gain;
        x *= 2; y *= 2;
        if (period > 0) period *= 2;
    }
    return sum / norm;
}

// Gradient noise with separate periods per axis (for stretched, still tileable patterns like wood grain).
inline float perlin2a(float x, float y, int px, int py, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    auto grad = [&](int ix, int iy, float dx, float dy) {
        uint32_t h = hash3(wrapi(ix, px), wrapi(iy, py), 9, seed);
        float a = (h & 0xFFFF) * (kTwoPi / 65536.0f);
        return std::cos(a) * dx + std::sin(a) * dy;
    };
    float n00 = grad(xi, yi, fx, fy), n10 = grad(xi + 1, yi, fx - 1, fy);
    float n01 = grad(xi, yi + 1, fx, fy - 1), n11 = grad(xi + 1, yi + 1, fx - 1, fy - 1);
    float u = fade(fx), v = fade(fy);
    return lerpf(lerpf(n00, n10, u), lerpf(n01, n11, u), v) * 1.414f;
}
inline float fbm2a(float x, float y, int octaves, int px, int py, uint32_t seed) {
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += perlin2a(x, y, px, py, seed + i * 71u) * amp;
        norm += amp;
        amp *= 0.5f;
        x *= 2; y *= 2; px *= 2; py *= 2;
    }
    return sum / norm;
}

struct Cell {
    float f1, f2;
    uint32_t id;
    vec2 center;
};
// Periodic Worley noise over a p x p cell grid (coords in cell units).
inline Cell worley2(float x, float y, int p, uint32_t seed, float jitter = 0.85f) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    Cell c{1e9f, 1e9f, 0, {}};
    for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++) {
            int cx = xi + i, cy = yi + j;
            int wx = wrapi(cx, p), wy = wrapi(cy, p);
            float ox = 0.5f + (hashf(wx, wy, 1, seed) - 0.5f) * jitter;
            float oy = 0.5f + (hashf(wx, wy, 2, seed) - 0.5f) * jitter;
            float px = cx + ox, py = cy + oy;
            float d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
            if (d < c.f1) {
                c.f2 = c.f1; c.f1 = d; c.id = hash3(wx, wy, 3, seed); c.center = {px, py};
            } else if (d < c.f2) {
                c.f2 = d;
            }
        }
    return c;
}

// Non-periodic 3D value noise in [0,1].
inline float value3(vec3 q, uint32_t seed) {
    int xi = (int)std::floor(q.x), yi = (int)std::floor(q.y), zi = (int)std::floor(q.z);
    float fx = fade(q.x - xi), fy = fade(q.y - yi), fz = fade(q.z - zi);
    auto h = [&](int a, int b, int c) { return hashf(xi + a, yi + b, zi + c, seed); };
    float x00 = lerpf(h(0, 0, 0), h(1, 0, 0), fx), x10 = lerpf(h(0, 1, 0), h(1, 1, 0), fx);
    float x01 = lerpf(h(0, 0, 1), h(1, 0, 1), fx), x11 = lerpf(h(0, 1, 1), h(1, 1, 1), fx);
    return lerpf(lerpf(x00, x10, fy), lerpf(x01, x11, fy), fz);
}

}  // namespace noise
