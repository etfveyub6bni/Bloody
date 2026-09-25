#include "assets/textures.h"

#include <atomic>
#include <thread>

#include "core/noise.h"

namespace {

struct Texel {
    float h = 0.5f;
    vec3 col{0.5f, 0.5f, 0.5f};
    float rough = 0.8f;
    float metal = 0.0f;
};

using GenFn = void (*)(float u, float v, Texel& o);

// Fast tileable gradient noise: gradients come from a table instead of per-corner trig.
struct GradTable {
    float gx[256], gy[256];
    GradTable() {
        for (int i = 0; i < 256; i++) {
            float a = (i + 0.5f) * (kTwoPi / 256.0f);
            gx[i] = std::cos(a);
            gy[i] = std::sin(a);
        }
    }
};
const GradTable kGrad;

inline uint32_t h2(int x, int y, uint32_t seed) {
    return noise::hash32((uint32_t)x * 0x8da6b343u ^ noise::hash32((uint32_t)y * 0xd8163841u ^ seed));
}
inline float h2f(int x, int y, uint32_t seed) { return (h2(x, y, seed) & 0xFFFFFFu) * (1.0f / 16777215.0f); }

// Gradient noise in about [-1, 1], periodic with px x py lattice cells.
float gnoise(float x, float y, int px, int py, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    int x0 = noise::wrapi(xi, px), x1 = noise::wrapi(xi + 1, px), y0 = noise::wrapi(yi, py), y1 = noise::wrapi(yi + 1, py);
    auto g = [&](int ix, int iy, float dx, float dy) {
        uint32_t h = h2(ix, iy, seed) & 255u;
        return kGrad.gx[h] * dx + kGrad.gy[h] * dy;
    };
    float n00 = g(x0, y0, fx, fy), n10 = g(x1, y0, fx - 1, fy), n01 = g(x0, y1, fx, fy - 1), n11 = g(x1, y1, fx - 1, fy - 1);
    float u = noise::fade(fx), v = noise::fade(fy);
    return lerpf(lerpf(n00, n10, u), lerpf(n01, n11, u), v) * 1.414f;
}

// Tileable fBm over the unit square; fx/fy are the base lattice periods.
float fbmA(float u, float v, int fx, int fy, int oct, uint32_t seed, float gain = 0.5f) {
    float s = 0, a = 1, n = 0;
    for (int i = 0; i < oct; i++) {
        s += gnoise(u * fx, v * fy, fx, fy, seed + i * 131u) * a;
        n += a;
        a *= gain;
        fx *= 2;
        fy *= 2;
    }
    return s / n;
}
float fbm(float u, float v, int f, int oct, uint32_t seed, float gain = 0.5f) { return fbmA(u, v, f, f, oct, seed, gain); }
// Ridged fBm in [0, 1]: sharp creases (cracks, scratches).
float ridged(float u, float v, int f, int oct, uint32_t seed) {
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; i++) {
        float r = 1.0f - std::fabs(gnoise(u * f, v * f, f, f, seed + i * 71u));
        s += r * r * a;
        n += a;
        a *= 0.5f;
        f *= 2;
    }
    return s / n;
}

struct Cell {
    float f1, f2;
    uint32_t id;
};
// Tileable Worley noise over a p x p grid (3x3 neighbourhood).
Cell worley(float x, float y, int p, uint32_t seed, float jitter = 0.8f) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    Cell c{1e9f, 1e9f, 0};
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            int cx = xi + i, cy = yi + j;
            uint32_t h = h2(noise::wrapi(cx, p), noise::wrapi(cy, p), seed);
            float px = cx + 0.5f + (((h & 0xFFFF) / 65535.0f) - 0.5f) * jitter;
            float py = cy + 0.5f + (((h >> 16) / 65535.0f) - 0.5f) * jitter;
            float d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
            if (d < c.f1) {
                c.f2 = c.f1;
                c.f1 = d;
                c.id = noise::hash32(h ^ 0x9e3779b9u);
            } else if (d < c.f2) {
                c.f2 = d;
            }
        }
    return c;
}

vec3 mix3(vec3 a, vec3 b, float t) { return lerp(a, b, saturate(t)); }
float sstep(float a, float b, float x) { return smooth01((x - a) / (b - a)); }
float idf(uint32_t id, int shift) { return ((id >> shift) & 255u) / 255.0f; }
vec3 desat(vec3 c, float k) { float l = (c.x + c.y + c.z) / 3.0f; return lerp(c, vec3(l), k); }

// Sand: packed desert ground with wind ripples, fine grain and sparse irregular pebbles. Tile = 256 units.
void genSand(float u, float v, Texel& o) {
    float big = fbm(u, v, 3, 4, 11);
    float mid = fbm(u, v, 12, 3, 12);
    float warp = fbm(u, v, 4, 2, 13);
    float ripple = std::sin((v * 22.0f + u * 5.0f + warp * 2.6f) * kTwoPi);
    float rippleMask = sstep(-0.1f, 0.3f, fbm(u, v, 2, 3, 14));
    float grain = gnoise(u * 256, v * 256, 256, 256, 15) * 0.6f + gnoise(u * 512, v * 512, 512, 512, 16) * 0.4f;
    float trodden = sstep(0.05f, 0.35f, fbm(u, v, 5, 3, 17));  // compacted darker patches
    float h = 0.45f + 0.10f * big + 0.05f * mid + 0.035f * ripple * rippleMask * (1 - trodden) + 0.03f * grain;

    vec3 light{0.86f, 0.73f, 0.54f}, base{0.79f, 0.65f, 0.46f}, dark{0.66f, 0.53f, 0.38f};
    vec3 col = mix3(base, light, 0.5f + big * 1.4f);
    col = mix3(col, dark, trodden * 0.35f + std::max(0.0f, -mid) * 0.6f);
    col = mix3(col, desat(col, 0.6f) * 0.95f, sstep(0.2f, 0.5f, fbm(u, v, 6, 3, 18)) * 0.3f);
    col = col * (0.95f + 0.08f * grain);
    float rough = 0.93f + 0.05f * grain;

    // Two scattered pebble layers; only a fraction of cells hold one and shapes are distorted by noise.
    for (int layer = 0; layer < 2; layer++) {
        int freq = layer == 0 ? 18 : 44;
        float dist = fbm(u, v, freq * 2, 2, 20 + layer) * 0.18f;
        Cell c = worley(u * freq + dist, v * freq - dist, freq, 21 + layer, 0.9f);
        float occupancy = layer == 0 ? 0.035f : 0.06f;
        if (idf(c.id, 0) > occupancy) continue;
        float r = (layer == 0 ? 0.20f : 0.22f) * (0.5f + idf(c.id, 8));
        float d = c.f1 / r;
        if (d >= 1.0f) {
            col = col * (1.0f - sstep(1.25f, 1.0f, d) * 0.12f);  // contact shadow in the sand
            continue;
        }
        float dome = std::sqrt(std::max(0.0f, 1 - d * d));
        vec3 pc = mix3({0.62f, 0.57f, 0.50f}, {0.80f, 0.73f, 0.62f}, idf(c.id, 16));
        if (idf(c.id, 24) < 0.3f) pc = mix3(pc, {0.60f, 0.47f, 0.36f}, 0.5f);
        pc = pc * (0.88f + 0.2f * gnoise(u * 400, v * 400, 400, 400, 22 + layer)) * (0.9f + 0.12f * dome);
        float edge = sstep(1.0f, 0.88f, d);
        col = mix3(col, pc, edge);
        h += dome * (layer == 0 ? 0.30f : 0.16f);
        rough = lerpf(rough, 0.68f, edge);
    }
    o.col = col;
    o.h = h;
    o.rough = rough;
}

// Sandstone blocks with recessed sandy mortar, pillowed eroded faces and strata. Tile = 128 units.
void genSandstone(float u, float v, Texel& o) {
    const int rows = 4;
    float nEdge = fbm(u, v, 16, 3, 23);
    float fr = v * rows;
    int row = (int)std::floor(fr);
    float rv = fr - row;
    int rowW = noise::wrapi(row, rows);
    int nb = 2 + (int)(h2(rowW, 0, 21) % 2u);
    float off = h2f(rowW, 1, 22);
    float fb = u * nb + off;
    int bi = (int)std::floor(fb);
    float bu = fb - bi;
    int bidx = noise::wrapi(bi, nb);
    uint32_t bh = h2(bidx * 7 + rowW, 3, 24);
    float ex = std::min(bu, 1 - bu) / nb, ey = std::min(rv, 1 - rv) / rows;
    float e = std::min(ex, ey) + nEdge * 0.007f;
    float chipN = fbm(u, v, 24, 3, 25);
    float mortarW = 0.006f + 0.004f * std::max(0.0f, chipN);
    float face = sstep(mortarW, mortarW + 0.012f, e);
    float pillow = sstep(0.0f, 0.06f, e);

    float strata = std::sin((v * rows * 3.0f + fbm(u, v, 4, 3, 26) * 0.5f + idf(bh, 0) * 6.0f) * kTwoPi);
    float erosion = fbm(u, v, 10, 4, 27);
    Cell pits = worley(u * 36, v * 36, 36, 28, 0.9f);
    float pit = (idf(pits.id, 0) < 0.08f) ? sstep(0.22f, 0.05f, pits.f1) * 0.5f : 0.0f;
    float fine = gnoise(u * 256, v * 256, 256, 256, 29);

    const vec3 tones[4] = {{0.84f, 0.70f, 0.51f}, {0.82f, 0.67f, 0.48f}, {0.85f, 0.72f, 0.53f}, {0.80f, 0.68f, 0.52f}};
    vec3 stone = mix3(tones[bh & 3u], tones[(bh >> 2) & 3u], idf(bh, 8));
    stone = stone * (0.95f + 0.07f * idf(bh, 16));
    // Weathering crosses block boundaries, so the wall reads as one surface rather than a checkerboard.
    float weather = fbm(u, v, 3, 4, 31);
    stone = stone * (0.98f + 0.02f * strata) * (0.93f + 0.12f * erosion) * (0.97f + 0.05f * fine) * (0.94f + 0.12f * weather);
    stone = mix3(stone, stone * vec3(0.92f, 0.86f, 0.80f), sstep(0.1f, 0.5f, fbm(u, v, 5, 3, 30)) * 0.5f);
    stone = stone * (1.0f - pit * 0.18f);
    vec3 mortar = vec3(0.70f, 0.61f, 0.49f) * (0.9f + 0.15f * fine);

    o.col = mix3(mortar, stone, face);
    o.h = 0.2f + face * (0.45f + 0.18f * pillow + 0.06f * erosion + 0.01f * strata - pit * 0.08f) + fine * 0.015f;
    o.rough = lerpf(0.95f, 0.84f + 0.06f * erosion, face);
}

// Lime plaster: soft trowel undulation, grain, water stains, hairline cracks. No baked-in chips:
// exposed brick patches are placed in world space by the shader. Tile = 256 units.
void genPlaster(float u, float v, Texel& o) {
    float big = fbm(u, v, 2, 4, 31);
    float trowel = fbm(u, v, 7, 3, 32);
    float fine = gnoise(u * 256, v * 256, 256, 256, 33) * 0.6f + gnoise(u * 512, v * 512, 512, 512, 34) * 0.4f;
    float blot = sstep(0.05f, 0.45f, fbm(u, v, 4, 4, 35));
    // Vertical streaks (texture v runs up the wall).
    float streak = sstep(0.0f, 0.6f, fbmA(u, v, 24, 2, 3, 36)) * sstep(0.2f, 0.9f, fbm(u, v, 3, 2, 37) + 0.3f);
    float crackMask = sstep(0.25f, 0.45f, fbm(u, v, 3, 3, 38));
    float cr = ridged(u + fbm(u, v, 5, 2, 39) * 0.03f, v, 6, 3, 40);
    float crack = sstep(0.93f, 0.985f, cr) * crackMask;
    Cell pits = worley(u * 64, v * 64, 64, 41, 0.9f);
    float pit = idf(pits.id, 0) < 0.025f ? sstep(0.16f, 0.06f, pits.f1) * 0.5f : 0.0f;
    float wash = sstep(0.28f, 0.34f, fbm(u, v, 5, 4, 42));  // worn lime-wash layer

    vec3 c = mix3({0.84f, 0.75f, 0.61f}, {0.90f, 0.83f, 0.70f}, 0.5f + big * 1.3f);
    c = mix3(c, c * vec3(0.95f, 0.92f, 0.88f), wash * 0.6f);
    c = mix3(c, desat(c, 0.35f) * vec3(0.86f, 0.82f, 0.78f), blot * 0.28f);
    c = mix3(c, c * vec3(0.80f, 0.74f, 0.68f), streak * 0.35f);
    c = c * (0.97f + 0.05f * trowel) * (0.975f + 0.04f * fine);
    c = c * (1.0f - crack * 0.35f - pit * 0.25f);
    o.col = c;
    o.h = 0.6f + trowel * 0.05f + big * 0.03f + fine * 0.012f + wash * 0.015f - crack * 0.07f - pit * 0.08f;
    o.rough = 0.9f + 0.05f * fine - wash * 0.04f;
}

// Stone floor tiles, staggered, with wear and dirty grout. Tile = 128 units.
void genTiles(float u, float v, Texel& o) {
    const int n = 4;
    float fu = u * n, fv = v * n;
    int iv = (int)std::floor(fv);
    if (iv & 1) fu += 0.5f;
    int iu = (int)std::floor(fu);
    float bu = fu - iu, bv = fv - iv;
    float nn = fbm(u, v, 12, 3, 43);
    float e = std::min(std::min(bu, 1 - bu), std::min(bv, 1 - bv)) / n + nn * 0.004f;
    float face = sstep(0.006f, 0.014f, e);
    uint32_t id = h2(noise::wrapi(iu, n), noise::wrapi(iv, n), 44);
    float fine = gnoise(u * 256, v * 256, 256, 256, 45);
    float wear = sstep(-0.2f, 0.4f, fbm(u, v, 6, 3, 46));
    vec3 stone = mix3({0.70f, 0.63f, 0.53f}, {0.82f, 0.75f, 0.64f}, idf(id, 0));
    if (idf(id, 24) < 0.2f) stone = mix3(stone, {0.66f, 0.56f, 0.44f}, 0.6f);
    stone = stone * (0.93f + 0.10f * fbm(u, v, 16, 3, 47)) * (0.97f + 0.05f * fine);
    stone = mix3(stone, stone * 1.06f, wear * 0.5f);
    float cr = ridged(u, v, 8, 3, 48);
    float crack = idf(id, 16) < 0.25f ? sstep(0.95f, 0.99f, cr) : 0.0f;
    float tu = (idf(id, 8) - 0.5f) * 0.12f, tv = (idf(id, 12) - 0.5f) * 0.12f;
    o.col = mix3(vec3(0.62f, 0.54f, 0.42f) * (0.9f + 0.1f * fine), stone * (1 - crack * 0.3f), face);
    o.h = 0.3f + face * (0.5f + tu * (bu - 0.5f) + tv * (bv - 0.5f) + nn * 0.04f - crack * 0.06f) + fine * 0.01f;
    o.rough = lerpf(0.96f, 0.80f - wear * 0.12f, face);
}

// Wood grain along `along`: returns 0..1 with growth rings and fibres; `knot` gets a knot mask.
float woodGrain(float along, float across, uint32_t seed, float& knot) {
    float warp = fbmA(along, across, 2, 4, 3, seed) * 0.8f;
    float ring = std::sin((across * 26.0f + warp * 3.0f) * kTwoPi) * 0.5f + 0.5f;
    ring = ring * ring * ring;
    float fibre = gnoise(along * 8, across * 180, 8, 180, seed + 3) * 0.5f + 0.5f;
    Cell k = worley(along * 3, across * 6, 3, seed + 7, 0.8f);
    knot = idf(k.id, 0) < 0.25f ? sstep(0.35f, 0.05f, k.f1) : 0.0f;
    return saturate(0.55f + 0.25f * ring + 0.25f * (fibre - 0.5f) + 0.2f * warp);
}

void genCrate(float u, float v, Texel& o) {
    const float b = 0.13f;
    bool frame = u < b || u > 1 - b || v < b || v > 1 - b;
    float dd = std::fabs(u - v) * 0.70710678f;
    bool brace = !frame && dd < 0.07f;
    vec3 light{0.70f, 0.53f, 0.33f}, dark{0.42f, 0.29f, 0.17f};
    vec3 wood;
    float h, knot = 0;
    float dirt = sstep(-0.1f, 0.5f, fbm(u, v, 4, 3, 50));
    if (frame) {
        bool vertical = u < b || u > 1 - b;
        float along = vertical ? v : u, across = vertical ? u : v;
        float g = woodGrain(along, across, vertical ? 51 : 52, knot);
        float a = vertical ? u : v;
        float edge = a < b ? std::min(a, b - a) : std::min(1 - a, a - (1 - b));
        h = 0.8f + 0.2f * sstep(0.0f, 0.015f, edge);
        wood = mix3(dark, light, g) * (1.0f + 0.12f * sstep(0.012f, 0.0f, edge));  // worn bright edges
    } else if (brace) {
        float g = woodGrain((u + v) * 0.7071f, (u - v) * 0.7071f, 53, knot);
        h = 0.7f + 0.2f * sstep(0.0f, 0.012f, 0.07f - dd);
        wood = mix3(dark, light, g) * 0.97f;
    } else {
        const int planks = 5;
        float pu = (u - b) / (1 - 2 * b) * planks;
        int pi = std::min((int)pu, planks - 1);
        float pf = pu - pi;
        float gap = std::min(pf, 1 - pf);
        float g = woodGrain(v + pi * 0.37f, u * 3.0f + pi, 54 + pi, knot);
        h = 0.35f + 0.25f * sstep(0.02f, 0.05f, gap);
        wood = mix3(dark, light, g) * (0.84f + 0.16f * h2f(pi, 0, 55));
        if (gap < 0.02f) wood = wood * 0.35f;
    }
    wood = mix3(wood, wood * vec3(0.55f, 0.45f, 0.36f), knot * 0.8f);
    wood = mix3(wood, desat(wood, 0.4f) * 0.75f, dirt * 0.25f);
    const float nb = b * 0.5f;
    const float nails[8][2] = {{nb, nb}, {1 - nb, nb}, {nb, 1 - nb}, {1 - nb, 1 - nb}, {0.5f, nb}, {0.5f, 1 - nb}, {nb, 0.5f}, {1 - nb, 0.5f}};
    float nail = 0, rust = 0;
    for (auto& nl : nails) {
        float d = std::hypot(u - nl[0], v - nl[1]);
        nail = std::max(nail, sstep(0.013f, 0.009f, d));
        rust = std::max(rust, sstep(0.035f, 0.012f, d) * (v < nl[1] ? 1.0f : 0.4f));
    }
    wood = mix3(wood, wood * vec3(0.7f, 0.5f, 0.35f), rust * 0.35f);
    o.col = mix3(wood, vec3(0.30f, 0.27f, 0.24f), nail);
    o.h = h + nail * 0.05f - knot * 0.03f;
    o.metal = nail * 0.8f;
    o.rough = lerpf(0.78f + 0.1f * dirt, 0.45f, nail);
}

void genDoor(float u, float v, Texel& o) {
    const int planks = 6;
    float pu = u * planks;
    int pi = (int)std::floor(pu);
    float pf = pu - pi;
    float gap = std::min(pf, 1 - pf);
    float knot;
    float g = woodGrain(v, u * planks + pi * 0.3f, 61 + noise::wrapi(pi, planks), knot);
    vec3 woodCol = mix3({0.36f, 0.26f, 0.16f}, {0.56f, 0.42f, 0.27f}, g);
    float peelN = fbm(u, v, 6, 5, 62);
    float peeled = sstep(0.16f, 0.2f, peelN);
    float peelEdge = sstep(0.12f, 0.16f, peelN) * (1 - peeled);
    vec3 paint = mix3({0.17f, 0.35f, 0.44f}, {0.26f, 0.46f, 0.55f}, 0.5f + fbm(u, v, 3, 3, 63)) * (0.95f + 0.08f * g);
    paint = mix3(paint, desat(paint, 0.4f) * 1.1f, sstep(0.0f, 0.5f, fbm(u, v, 4, 3, 65)) * 0.4f);  // sun-faded
    float h = 0.4f + 0.3f * sstep(0.015f, 0.035f, gap) + (1 - peeled) * 0.05f + peelEdge * 0.02f;
    vec3 col = mix3(paint * (1 - peelEdge * 0.25f), woodCol, peeled);
    float rough = lerpf(0.55f, 0.85f, peeled), metal = 0;
    if (gap < 0.012f) col = col * 0.35f;
    bool band = (v > 0.12f && v < 0.20f) || (v > 0.80f && v < 0.88f);
    if (band) {
        float rustN = sstep(-0.1f, 0.4f, fbm(u, v, 16, 3, 64));
        col = mix3(vec3(0.20f, 0.18f, 0.16f), vec3(0.42f, 0.24f, 0.12f), rustN * 0.8f);
        metal = lerpf(0.8f, 0.1f, rustN);
        rough = lerpf(0.5f, 0.9f, rustN);
        h = 0.85f;
        float ru = u * 12 - std::floor(u * 12);
        float bc = v < 0.5f ? 0.16f : 0.84f;
        float d = std::hypot((ru - 0.5f) / 12.0f, v - bc);
        if (d < 0.012f) {
            h = 0.85f + 0.1f * std::sqrt(1 - (d / 0.012f) * (d / 0.012f));
            col = {0.30f, 0.27f, 0.24f};
        }
    }
    o.col = col;
    o.h = h;
    o.rough = rough;
    o.metal = metal;
}

// Painted steel panels with seams, rivets, scratches to bare metal and rust streaks.
void genMetal(float u, float v, Texel& o) {
    float pu = u * 2, pv = v * 2;
    float fu = pu - std::floor(pu), fv = pv - std::floor(pv);
    float e = std::min(std::min(fu, 1 - fu), std::min(fv, 1 - fv)) * 0.5f;
    float seam = sstep(0.003f, 0.008f, e);
    float n = fbm(u, v, 4, 5, 71);
    float rv;
    {
        float ru = fu * 8 - std::floor(fu * 8), rvv = fv * 8 - std::floor(fv * 8);
        float dEdgeU = std::min(fu, 1 - fu), dEdgeV = std::min(fv, 1 - fv);
        float d1 = dEdgeU < 0.04f ? std::hypot(dEdgeU - 0.022f, (rvv - 0.5f) / 8.0f) : 1.0f;
        float d2 = dEdgeV < 0.04f ? std::hypot(dEdgeV - 0.022f, (ru - 0.5f) / 8.0f) : 1.0f;
        float d = std::min(d1, d2);
        rv = d < 0.01f ? std::sqrt(1 - (d / 0.01f) * (d / 0.01f)) : 0.0f;
    }
    float scratch = sstep(0.55f, 0.75f, ridged(u, v * 0.25f, 12, 2, 72)) * sstep(0.0f, 0.4f, fbm(u, v, 3, 2, 73));
    float drip = sstep(0.1f, 0.7f, fbmA(u, v, 20, 2, 3, 74)) * sstep(0.35f, 0.0f, fv);  // streaks under seams
    float rust = saturate(sstep(0.2f, 0.34f, fbm(u, v, 8, 4, 75)) + drip * 0.6f + (1 - seam) * 0.5f);
    vec3 paint = mix3({0.36f, 0.42f, 0.38f}, {0.46f, 0.52f, 0.46f}, 0.5f + n) * (0.96f + 0.06f * gnoise(u * 128, v * 128, 128, 128, 76));
    vec3 rustCol = mix3({0.36f, 0.19f, 0.09f}, {0.56f, 0.32f, 0.15f}, 0.5f + fbm(u, v, 32, 2, 77));
    vec3 c = mix3(paint, vec3(0.56f, 0.56f, 0.55f), scratch);
    c = mix3(c, rustCol, rust * 0.75f);
    c = c * lerpf(0.55f, 1.0f, seam);
    o.col = c;
    o.h = 0.5f + seam * 0.2f + rv * 0.12f + rust * 0.03f * fbm(u, v, 48, 2, 78) + n * 0.02f - scratch * 0.01f;
    o.rough = lerpf(lerpf(0.45f, 0.3f, scratch), 0.92f, rust);
    o.metal = lerpf(lerpf(0.05f, 1.0f, scratch), 0.0f, rust);
}

// Cast concrete: formwork seams, tie holes, pores, aggregate and water stains. Tile = 256 units.
void genConcrete(float u, float v, Texel& o) {
    float n = fbm(u, v, 3, 5, 81);
    float fine = gnoise(u * 256, v * 256, 256, 256, 82) * 0.6f + gnoise(u * 512, v * 512, 512, 512, 83) * 0.4f;
    Cell c = worley(u * 90, v * 90, 90, 84, 0.9f);
    float pore = idf(c.id, 0) < 0.10f ? sstep(0.25f, 0.1f, c.f1) : 0.0f;
    Cell ag = worley(u * 140, v * 140, 140, 85, 0.9f);
    float aggregate = idf(ag.id, 0) < 0.3f ? sstep(0.35f, 0.25f, ag.f1) * (idf(ag.id, 8) - 0.5f) : 0.0f;
    float seamV = std::min(std::fabs(v - 0.5f), std::min(v, 1 - v));
    float seamU = std::min(u, 1 - u);
    float line = 1 - sstep(0.001f, 0.004f, std::min(seamV, seamU));
    float tie = 0;
    for (float tu : {0.25f, 0.75f})
        for (float tv : {0.25f, 0.75f}) tie = std::max(tie, sstep(0.012f, 0.008f, std::hypot(u - tu, v - tv)));
    float stain = sstep(0.0f, 0.5f, fbmA(u, v, 16, 2, 3, 86)) * sstep(-0.2f, 0.4f, fbm(u, v, 2, 2, 87));
    vec3 col = mix3({0.56f, 0.55f, 0.52f}, {0.69f, 0.67f, 0.63f}, 0.5f + n * 1.2f) * (0.96f + 0.07f * fine);
    col = col * (1.0f + aggregate * 0.25f);
    col = mix3(col, col * vec3(0.82f, 0.80f, 0.77f), stain * 0.45f);
    o.col = col * (1 - pore * 0.45f) * (1 - line * 0.18f) * (1 - tie * 0.6f);
    o.h = 0.6f + fine * 0.03f + n * 0.03f - pore * 0.12f - line * 0.06f - tie * 0.15f + aggregate * 0.02f;
    o.rough = 0.88f + 0.06f * fine;
}

// Limestone trim, curbs and ceilings: chisel texture, chipped edges, dirt and water stains.
void genTrim(float u, float v, Texel& o) {
    float n = fbm(u, v, 4, 4, 91);
    float fine = gnoise(u * 256, v * 256, 256, 256, 92) * 0.6f + gnoise(u * 512, v * 512, 512, 512, 95) * 0.4f;
    float band = 0.5f + 0.5f * std::sin(v * kTwoPi * 8);
    float chisel = gnoise(u * 24, v * 96, 24, 96, 93);
    float stain = sstep(-0.1f, 0.45f, fbm(u, v, 5, 3, 94));
    float streak = sstep(0.0f, 0.6f, fbmA(u, v, 20, 2, 3, 96));
    Cell pits = worley(u * 48, v * 48, 48, 97, 0.9f);
    float pit = idf(pits.id, 0) < 0.12f ? sstep(0.25f, 0.08f, pits.f1) : 0.0f;
    vec3 c = mix3({0.54f, 0.46f, 0.36f}, {0.66f, 0.57f, 0.45f}, 0.5f + n * 1.3f) * (0.94f + 0.1f * fine);
    c = mix3(c, c * vec3(0.78f, 0.72f, 0.66f), stain * 0.45f + streak * 0.2f);
    c = c * (1.0f - pit * 0.3f);
    o.col = c;
    o.h = 0.5f + fine * 0.03f + band * 0.02f + chisel * 0.025f + n * 0.03f - pit * 0.08f;
    o.rough = 0.86f + 0.06f * stain;
}

// Clay bricks with per-brick colour, chipped edges and recessed mortar.
void genBrick(float u, float v, Texel& o) {
    const int rows = 8, cols = 4;
    float fr = v * rows;
    int row = (int)std::floor(fr);
    float rv = fr - row;
    float fc = u * cols + (row & 1) * 0.5f;
    int col = (int)std::floor(fc);
    float cu = fc - col;
    float n = fbm(u, v, 16, 3, 101);
    float e = std::min(std::min(cu, 1 - cu) / cols, std::min(rv, 1 - rv) / rows);
    float chip = std::max(0.0f, fbm(u, v, 32, 2, 104));
    float face = sstep(0.005f + chip * 0.006f, 0.010f + chip * 0.006f, e + n * 0.0015f);
    uint32_t id = h2(noise::wrapi(col, cols), noise::wrapi(row, rows), 102);
    vec3 b = mix3({0.50f, 0.23f, 0.15f}, {0.68f, 0.35f, 0.23f}, idf(id, 0));
    if (idf(id, 8) < 0.15f) b = b * 0.6f;                                  // over-fired dark brick
    else if (idf(id, 8) > 0.9f) b = mix3(b, {0.76f, 0.55f, 0.40f}, 0.5f);  // pale brick
    float fine = gnoise(u * 256, v * 256, 256, 256, 103);
    b = b * (0.9f + 0.12f * fbm(u, v, 24, 2, 105)) * (0.96f + 0.06f * fine);
    b = mix3(b, b * 0.7f, sstep(0.1f, 0.6f, fbm(u, v, 3, 3, 106)) * 0.4f);  // soot
    vec3 mortar = vec3(0.66f, 0.63f, 0.57f) * (0.88f + 0.12f * fine);
    o.col = mix3(mortar, b, face);
    o.h = 0.3f + 0.45f * face + n * 0.03f + fine * 0.015f;
    o.rough = lerpf(0.95f, 0.82f, face);
}

// Corrugated container steel (white base, tinted per brush): ribs, dents, scratches, rust.
void genCorrugated(float u, float v, Texel& o) {
    float w = u * 16;
    float f = w - std::floor(w);
    float prof = sstep(0.1f, 0.25f, f) * (1 - sstep(0.6f, 0.75f, f));
    float n = fbm(u, v, 4, 4, 111);
    float dent = fbm(u, v, 6, 3, 114) * 0.06f;
    float rustEdge = sstep(0.25f, 0.0f, v) + sstep(0.85f, 1.0f, v) * 0.5f;
    float rust = saturate(sstep(0.24f, 0.36f, fbm(u, v, 8, 5, 112)) + rustEdge * 0.6f * sstep(-0.3f, 0.3f, fbm(u, v, 12, 3, 115)));
    float scratch = sstep(0.62f, 0.8f, ridged(u, v * 0.2f, 10, 2, 113));
    float streak = sstep(0.1f, 0.8f, fbmA(u, v, 32, 2, 3, 116)) * 0.3f;
    vec3 paint = vec3(0.86f) * (0.93f + 0.08f * n) * (1 - streak * 0.25f);
    vec3 c = mix3(paint, {0.60f, 0.60f, 0.60f}, scratch * 0.7f);
    c = mix3(c, {0.42f, 0.25f, 0.14f}, rust * 0.65f);
    o.col = c;
    o.h = 0.3f + prof * 0.5f + dent;
    o.rough = lerpf(lerpf(0.5f, 0.35f, scratch), 0.9f, rust);
    o.metal = lerpf(lerpf(0.1f, 0.9f, scratch), 0.0f, rust);
}

// Rounded cobblestones bedded in sand.
void genCobble(float u, float v, Texel& o) {
    float warp = fbm(u, v, 8, 2, 120) * 0.25f;
    Cell c = worley(u * 10 + warp, v * 10 - warp, 10, 121, 0.75f);
    float gapW = 0.05f + 0.03f * fbm(u, v, 24, 2, 123);
    float face = sstep(gapW, gapW + 0.1f, c.f2 - c.f1);
    float dome = saturate(1 - c.f1 * 1.25f);
    float fine = gnoise(u * 256, v * 256, 256, 256, 124);
    vec3 stone = mix3({0.54f, 0.50f, 0.45f}, {0.74f, 0.68f, 0.58f}, idf(c.id, 0));
    if (idf(c.id, 8) < 0.2f) stone = mix3(stone, {0.62f, 0.50f, 0.40f}, 0.5f);
    stone = stone * (0.92f + 0.12f * fbm(u, v, 32, 2, 122)) * (0.96f + 0.06f * fine);
    float polish = dome * dome;
    vec3 sand = vec3(0.72f, 0.60f, 0.44f) * (0.9f + 0.15f * fine);
    o.col = mix3(sand, stone * (1.0f + polish * 0.06f), face);
    o.h = 0.25f + face * (0.3f + dome * 0.35f) + fine * 0.015f;
    o.rough = lerpf(0.96f, 0.78f - polish * 0.15f, face);
}

const GenFn kGenerators[MAT_COUNT] = {genSand, genSandstone, genPlaster, genTiles, genCrate, genDoor,
                                      genMetal, genConcrete, genTrim, genBrick, genCorrugated, genCobble};

// name, surface, bump, avgAlbedo, uvScale, antiTile, macro, grime
MaterialInfo g_info[MAT_COUNT] = {
    {"sand", SURF_SAND, 3.0f, {}, 0.5f, 1.0f, 1.0f, 0.6f},
    {"sandstone", SURF_STONE, 5.0f, {}, 1.0f, 0.0f, 0.7f, 1.0f},
    {"plaster", SURF_STONE, 3.0f, {}, 0.5f, 1.0f, 1.0f, 1.0f},
    {"tiles", SURF_STONE, 5.0f, {}, 1.0f, 0.0f, 0.8f, 0.8f},
    {"crate", SURF_WOOD, 5.0f, {}, 1.0f, 0.0f, 0.5f, 0.5f},
    {"door", SURF_WOOD, 4.0f, {}, 1.0f, 0.0f, 0.3f, 0.6f},
    {"metal", SURF_METAL, 3.0f, {}, 1.0f, 0.0f, 0.5f, 0.8f},
    {"concrete", SURF_CONCRETE, 3.0f, {}, 0.5f, 1.0f, 0.8f, 1.0f},
    {"trim", SURF_STONE, 2.5f, {}, 1.0f, 0.0f, 0.6f, 0.8f},
    {"brick", SURF_STONE, 5.0f, {}, 1.0f, 0.0f, 0.6f, 0.8f},
    {"corrugated", SURF_METAL, 6.0f, {}, 1.0f, 0.0f, 0.4f, 0.6f},
    {"cobble", SURF_STONE, 6.0f, {}, 0.75f, 0.0f, 0.8f, 0.8f},
};

struct GenJob {
    int size = 0;
    std::vector<Texel> tex;
    std::vector<float> height;
};

void generateRows(int m, GenJob& job, int y0, int y1) {
    const int size = job.size;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < size; x++) {
            Texel t;
            kGenerators[m]((x + 0.5f) / size, (y + 0.5f) / size, t);
            job.tex[(size_t)y * size + x] = t;
            job.height[(size_t)y * size + x] = t.h;
        }
}

void finishMaterial(int m, GenJob& job, std::vector<uint8_t>& alb, std::vector<uint8_t>& nrm) {
    const int size = job.size;
    const std::vector<float>& height = job.height;
    // Local average of height for the cavity term (radius scales with resolution).
    const int R = std::max(2, 3 * size / 512);
    std::vector<float> tmp(height.size()), blur(height.size());
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float s = 0;
            for (int k = -R; k <= R; k++) s += height[(size_t)y * size + ((x + k + size) % size)];
            tmp[(size_t)y * size + x] = s / (2 * R + 1);
        }
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float s = 0;
            for (int k = -R; k <= R; k++) s += tmp[(size_t)((y + k + size) % size) * size + x];
            blur[(size_t)y * size + x] = s / (2 * R + 1);
        }
    alb.resize((size_t)size * size * 4);
    nrm.resize((size_t)size * size * 4);
    const float bump = g_info[m].bump * (size / 512.0f);
    double sum[3] = {0, 0, 0};
    auto h = [&](int x, int y) { return height[(size_t)((y + size) % size) * size + ((x + size) % size)]; };
    auto b = [](float f) { return (uint8_t)(saturate(f) * 255.0f + 0.5f); };
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            size_t i = (size_t)y * size + x;
            const Texel& t = job.tex[i];
            // Sobel gradient gives smoother normals than central differences.
            float dx = (h(x + 1, y - 1) + 2 * h(x + 1, y) + h(x + 1, y + 1) - h(x - 1, y - 1) - 2 * h(x - 1, y) - h(x - 1, y + 1)) * 0.25f * bump;
            float dy = (h(x - 1, y + 1) + 2 * h(x, y + 1) + h(x + 1, y + 1) - h(x - 1, y - 1) - 2 * h(x, y - 1) - h(x + 1, y - 1)) * 0.25f * bump;
            vec3 n = normalize(vec3(-dx, -dy, 1.0f));
            float cavity = saturate(1.0f - std::max(0.0f, blur[i] - height[i]) * 3.5f);
            alb[i * 4 + 0] = b(t.col.x);
            alb[i * 4 + 1] = b(t.col.y);
            alb[i * 4 + 2] = b(t.col.z);
            alb[i * 4 + 3] = b(t.rough);
            nrm[i * 4 + 0] = b(n.x * 0.5f + 0.5f);
            nrm[i * 4 + 1] = b(n.y * 0.5f + 0.5f);
            nrm[i * 4 + 2] = b(cavity);
            nrm[i * 4 + 3] = b(t.metal);
            sum[0] += std::pow(saturate(t.col.x), 2.2f);
            sum[1] += std::pow(saturate(t.col.y), 2.2f);
            sum[2] += std::pow(saturate(t.col.z), 2.2f);
        }
    double inv = 1.0 / ((double)size * size);
    g_info[m].avgAlbedo = vec3((float)(sum[0] * inv), (float)(sum[1] * inv), (float)(sum[2] * inv));
}

}  // namespace

MaterialInfo& materialInfo(int m) { return g_info[std::max(0, std::min(m, (int)MAT_COUNT - 1))]; }

int worldTextureSize(int quality) { return quality <= 0 ? 256 : quality == 1 ? 512 : 1024; }

void generateWorldTextures(WorldTextureSet& out, int size) {
    out.size = size;
    out.albedo.assign(MAT_COUNT, {});
    out.normal.assign(MAT_COUNT, {});
    std::vector<GenJob> jobs(MAT_COUNT);
    for (auto& j : jobs) {
        j.size = size;
        j.tex.resize((size_t)size * size);
        j.height.resize((size_t)size * size);
    }
    // Row bands of all materials share one worker pool so the cost spreads over every core.
    const int band = 16;
    const int bandsPer = (size + band - 1) / band;
    const int total = bandsPer * MAT_COUNT;
    std::atomic<int> next{0};
    auto worker = [&] {
        for (;;) {
            int k = next.fetch_add(1);
            if (k >= total) break;
            int m = k / bandsPer, y0 = (k % bandsPer) * band;
            generateRows(m, jobs[m], y0, std::min(size, y0 + band));
        }
    };
    int nt = (int)std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (int t = 0; t < nt; t++) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    threads.clear();
    std::atomic<int> nextM{0};
    for (int t = 0; t < std::min(nt, (int)MAT_COUNT); t++)
        threads.emplace_back([&] {
            for (;;) {
                int m = nextM.fetch_add(1);
                if (m >= MAT_COUNT) break;
                finishMaterial(m, jobs[m], out.albedo[m], out.normal[m]);
                std::vector<Texel>().swap(jobs[m].tex);
            }
        });
    for (auto& t : threads) t.join();
}
