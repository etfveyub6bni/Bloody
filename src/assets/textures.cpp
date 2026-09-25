#include "assets/textures.h"

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

float fbm(float u, float v, int freq, int oct, uint32_t seed) { return noise::fbm2(u * freq, v * freq, oct, freq, seed); }
vec3 mix3(vec3 a, vec3 b, float t) { return lerp(a, b, saturate(t)); }

void genSand(float u, float v, Texel& o) {
    float n1 = fbm(u, v, 4, 5, 11), n2 = fbm(u, v, 32, 3, 12);
    float h = 0.5f + 0.18f * n1 + 0.08f * n2 + 0.03f * std::sin((u * 12 + v * 4 + n1 * 0.8f) * kTwoPi);
    noise::Cell c = noise::worley2(u * 20, v * 20, 20, 13);
    float pr = 0.16f + 0.12f * ((c.id & 255) / 255.0f);
    float pebble = ((c.id >> 8) % 3 == 0 && c.f1 < pr) ? std::sqrt(1 - (c.f1 / pr) * (c.f1 / pr)) : 0.0f;
    h += pebble * 0.3f;
    vec3 base = mix3({0.74f, 0.60f, 0.42f}, {0.86f, 0.73f, 0.55f}, 0.5f + n1 * 1.3f) * (0.94f + 0.12f * (0.5f + n2));
    vec3 pcol = mix3({0.56f, 0.50f, 0.44f}, {0.72f, 0.64f, 0.52f}, ((c.id >> 12) & 255) / 255.0f);
    o.col = pebble > 0 ? mix3(base, pcol, 0.85f) : base;
    o.h = h;
    o.rough = 0.94f;
}

void genSandstone(float u, float v, Texel& o) {
    const int rows = 4;
    float fr = v * rows;
    int row = (int)std::floor(fr);
    float rv = fr - row;
    int nb = 2 + (int)(noise::hash3(row & 3, 0, 0, 21) % 2);
    float off = noise::hashf(row & 3, 1, 0, 22);
    float fb = u * nb + off;
    int bi = (int)std::floor(fb);
    float bu = fb - bi;
    int bidx = noise::wrapi(bi, nb);
    float ex = std::min(bu, 1 - bu) / nb, ey = std::min(rv, 1 - rv) / rows;
    float n = fbm(u, v, 8, 4, 23);
    float e = std::min(ex, ey) + n * 0.006f;
    float face = smooth01((e - 0.009f) / 0.016f);
    uint32_t bh = noise::hash3(bidx, row & 3, 0, 24);
    float detail = fbm(u, v, 24, 3, 25);
    noise::Cell c = noise::worley2(u * 12, v * 12, 12, 26);
    float chip = (c.f1 < 0.22f && e < 0.035f) ? 1.0f : 0.0f;
    vec3 stone = mix3({0.76f, 0.61f, 0.42f}, {0.88f, 0.75f, 0.55f}, (bh & 255) / 255.0f);
    stone = stone * (0.92f + 0.16f * (0.5f + fbm(u, v, 4, 4, 27)));
    stone = stone * (0.96f + 0.08f * detail);
    o.col = mix3({0.68f, 0.62f, 0.53f}, stone, face) * (1 - chip * 0.12f);
    o.h = 0.25f + face * (0.6f + detail * 0.12f) - chip * face * 0.25f;
    o.rough = 0.88f;
}

void genPlaster(float u, float v, Texel& o) {
    float n = fbm(u, v, 4, 5, 31), fine = fbm(u, v, 48, 2, 32);
    float mask = fbm(u, v, 3, 4, 33);
    float chipped = smooth01((mask - 0.34f) / 0.04f);
    float fr = v * 16;
    int row = (int)std::floor(fr);
    float rv = fr - row;
    float fb = u * 8 + (row & 1) * 0.5f;
    float bu = fb - std::floor(fb);
    float be = std::min(std::min(bu, 1 - bu) / 8.0f, std::min(rv, 1 - rv) / 16.0f);
    float brickFace = smooth01((be - 0.004f) / 0.006f);
    vec3 brickCol = mix3({0.56f, 0.45f, 0.35f}, {0.66f, 0.53f, 0.41f}, brickFace);
    vec3 plaster = mix3({0.83f, 0.75f, 0.61f}, {0.92f, 0.86f, 0.73f}, 0.5f + n) * (0.96f + fine * 0.08f);
    noise::Cell c = noise::worley2(u * 6, v * 6, 6, 34);
    float crack = smooth01(1 - (c.f2 - c.f1) / 0.03f) * smooth01((fbm(u, v, 5, 3, 35) - 0.1f) / 0.1f);
    o.col = mix3(plaster * (1 - crack * 0.35f), brickCol, chipped);
    o.h = lerpf(0.75f + fine * 0.04f + n * 0.05f - crack * 0.1f, 0.35f + brickFace * 0.15f, chipped);
    o.rough = 0.92f;
}

void genTiles(float u, float v, Texel& o) {
    const int n = 4;
    float fu = u * n, fv = v * n;
    int iv = (int)std::floor(fv);
    if (iv & 1) fu += 0.5f;
    int iu = (int)std::floor(fu);
    float bu = fu - iu, bv = fv - iv;
    float nn = fbm(u, v, 8, 4, 41);
    float e = std::min(std::min(bu, 1 - bu), std::min(bv, 1 - bv)) / n + nn * 0.004f;
    float face = smooth01((e - 0.008f) / 0.012f);
    uint32_t id = noise::hash3(noise::wrapi(iu, n), noise::wrapi(iv, n), 0, 42);
    vec3 stone = mix3({0.64f, 0.58f, 0.50f}, {0.80f, 0.73f, 0.62f}, (id & 255) / 255.0f) *
                 (0.92f + 0.16f * (0.5f + fbm(u, v, 16, 3, 43)));
    float tu = (((id >> 8) & 255) / 255.0f - 0.5f) * 0.1f, tv = (((id >> 16) & 255) / 255.0f - 0.5f) * 0.1f;
    o.col = mix3({0.72f, 0.62f, 0.47f}, stone, face);
    o.h = 0.3f + face * (0.55f + tu * (bu - 0.5f) + tv * (bv - 0.5f) + nn * 0.05f);
    o.rough = lerpf(0.95f, 0.78f, face);
}

void genCrate(float u, float v, Texel& o) {
    const float b = 0.13f;
    bool frame = u < b || u > 1 - b || v < b || v > 1 - b;
    float dd = std::fabs(u - v) * 0.70710678f;
    bool brace = !frame && dd < 0.07f;
    auto grain = [](float along, float across, uint32_t seed) {
        float g = noise::fbm2(along * 1.5f, across * 30.0f, 4, 0, seed);
        float streak = noise::perlin2(along * 0.8f, across * 90.0f, 0, seed + 5);
        return 0.5f + 0.35f * g + 0.25f * streak;
    };
    vec3 light{0.66f, 0.50f, 0.31f}, dark{0.42f, 0.29f, 0.17f};
    vec3 wood;
    float h;
    if (frame) {
        bool vertical = u < b || u > 1 - b;
        float along = vertical ? v : u, across = vertical ? u : v;
        float g = grain(along, across, vertical ? 51 : 52);
        float a = vertical ? u : v;
        float edge = a < b ? std::min(a, b - a) : std::min(1 - a, a - (1 - b));
        h = 0.8f + 0.2f * smooth01(edge / 0.015f);
        wood = mix3(dark, light, g);
    } else if (brace) {
        float g = grain((u + v) * 0.7071f, (u - v) * 0.7071f, 53);
        h = 0.7f + 0.2f * smooth01((0.07f - dd) / 0.012f);
        wood = mix3(dark, light, g) * 0.96f;
    } else {
        const int planks = 5;
        float pu = (u - b) / (1 - 2 * b) * planks;
        int pi = std::min((int)pu, planks - 1);
        float pf = pu - pi;
        float gap = std::min(pf, 1 - pf);
        float g = grain(v + pi * 3.1f, u, 54 + pi);
        h = 0.35f + 0.25f * smooth01((gap - 0.02f) / 0.03f);
        wood = mix3(dark, light, g) * (0.86f + 0.12f * noise::hashf(pi, 0, 0, 55));
        if (gap < 0.02f) wood = wood * 0.4f;
    }
    const float nb = b * 0.5f;
    const float nails[8][2] = {{nb, nb}, {1 - nb, nb}, {nb, 1 - nb}, {1 - nb, 1 - nb}, {0.5f, nb}, {0.5f, 1 - nb}, {nb, 0.5f}, {1 - nb, 0.5f}};
    bool isNail = false;
    for (auto& nl : nails) isNail |= std::hypot(u - nl[0], v - nl[1]) < 0.012f;
    if (isNail) {
        o.col = {0.26f, 0.25f, 0.23f};
        o.h = h + 0.05f;
        o.metal = 0.7f;
        o.rough = 0.5f;
    } else {
        o.col = wood;
        o.h = h;
        o.rough = 0.8f;
    }
}

void genDoor(float u, float v, Texel& o) {
    const int planks = 6;
    float pu = u * planks;
    int pi = (int)std::floor(pu);
    float pf = pu - pi;
    float gap = std::min(pf, 1 - pf);
    float g = noise::fbm2a(u * 48, v * 3, 4, 48, 3, 61 + pi);
    vec3 woodCol = mix3({0.40f, 0.29f, 0.18f}, {0.58f, 0.44f, 0.28f}, 0.5f + g);
    float peeled = smooth01((fbm(u, v, 6, 5, 62) - 0.2f) / 0.05f);
    vec3 paint = mix3({0.18f, 0.36f, 0.45f}, {0.25f, 0.45f, 0.54f}, 0.5f + fbm(u, v, 3, 3, 63)) * (0.95f + 0.1f * g);
    float h = 0.4f + 0.3f * smooth01((gap - 0.015f) / 0.02f) + (1 - peeled) * 0.05f;
    vec3 col = mix3(paint, woodCol, peeled);
    float rough = lerpf(0.55f, 0.85f, peeled), metal = 0;
    if (gap < 0.012f) col = col * 0.35f;
    bool band = (v > 0.12f && v < 0.20f) || (v > 0.80f && v < 0.88f);
    if (band) {
        col = vec3(0.19f, 0.17f, 0.15f) * (0.8f + 0.4f * (0.5f + fbm(u, v, 16, 3, 64)));
        metal = 0.8f;
        rough = 0.55f;
        h = 0.85f;
        float ru = u * 12 - std::floor(u * 12);
        float bc = v < 0.5f ? 0.16f : 0.84f;
        if (std::hypot((ru - 0.5f) / 12.0f, v - bc) < 0.012f) {
            h = 0.95f;
            col = {0.30f, 0.27f, 0.24f};
        }
    }
    o.col = col;
    o.h = h;
    o.rough = rough;
    o.metal = metal;
}

void genMetal(float u, float v, Texel& o) {
    float pu = u * 2, pv = v * 2;
    float fu = pu - std::floor(pu), fv = pv - std::floor(pv);
    float e = std::min(std::min(fu, 1 - fu), std::min(fv, 1 - fv)) * 0.5f;
    float seam = smooth01((e - 0.004f) / 0.006f);
    float n = fbm(u, v, 4, 5, 71);
    float rust = smooth01((fbm(u, v, 8, 4, 72) - 0.18f) / 0.12f);
    vec3 paint = mix3({0.38f, 0.43f, 0.39f}, {0.47f, 0.52f, 0.46f}, 0.5f + n);
    vec3 rustCol = mix3({0.40f, 0.21f, 0.10f}, {0.55f, 0.32f, 0.16f}, 0.5f + fbm(u, v, 32, 2, 73));
    o.col = mix3(paint, rustCol, rust * 0.7f) * lerpf(0.6f, 1.0f, seam);
    o.h = 0.5f + seam * 0.2f + rust * 0.05f * fbm(u, v, 48, 2, 74) + n * 0.02f;
    o.rough = lerpf(0.5f, 0.9f, rust);
    o.metal = lerpf(0.3f, 0.0f, rust);
}

void genConcrete(float u, float v, Texel& o) {
    float n = fbm(u, v, 4, 5, 81), fine = fbm(u, v, 64, 2, 82);
    noise::Cell c = noise::worley2(u * 40, v * 40, 40, 83);
    float pore = (c.f1 < 0.12f && (c.id & 7) == 0) ? 1.0f : 0.0f;
    float form = std::min(std::fabs(v - 0.5f), std::min(v, 1 - v));
    float line = 1 - smooth01(form / 0.004f);
    vec3 col = mix3({0.55f, 0.55f, 0.53f}, {0.68f, 0.67f, 0.64f}, 0.5f + n) * (0.95f + 0.1f * fine);
    o.col = col * (1 - pore * 0.4f) * (1 - line * 0.15f);
    o.h = 0.6f + fine * 0.05f - pore * 0.2f - line * 0.08f;
    o.rough = 0.9f;
}

void genTrim(float u, float v, Texel& o) {
    float n = fbm(u, v, 4, 4, 91), fine = fbm(u, v, 32, 3, 92);
    float band = 0.5f + 0.5f * std::sin(v * kTwoPi * 8);
    o.col = mix3({0.52f, 0.46f, 0.37f}, {0.62f, 0.55f, 0.44f}, 0.5f + n) * (0.95f + 0.08f * fine);
    o.h = 0.5f + fine * 0.08f + band * 0.03f;
    o.rough = 0.8f;
}

void genBrick(float u, float v, Texel& o) {
    const int rows = 8, cols = 4;
    float fr = v * rows;
    int row = (int)std::floor(fr);
    float rv = fr - row;
    float fc = u * cols + (row & 1) * 0.5f;
    int col = (int)std::floor(fc);
    float cu = fc - col;
    float n = fbm(u, v, 8, 4, 101);
    float e = std::min(std::min(cu, 1 - cu) / cols, std::min(rv, 1 - rv) / rows);
    float face = smooth01((e - 0.006f + n * 0.002f) / 0.006f);
    uint32_t id = noise::hash3(noise::wrapi(col, cols), row, 0, 102);
    vec3 b = mix3({0.48f, 0.21f, 0.14f}, {0.66f, 0.32f, 0.22f}, (id & 255) / 255.0f) * (0.9f + 0.2f * (0.5f + fbm(u, v, 32, 2, 103)));
    o.col = mix3({0.70f, 0.68f, 0.62f}, b, face);
    o.h = 0.3f + 0.5f * face + n * 0.04f;
    o.rough = 0.85f;
}

void genCorrugated(float u, float v, Texel& o) {
    float w = u * 16;
    float f = w - std::floor(w);
    float prof = smooth01((f - 0.1f) / 0.15f) * (1 - smooth01((f - 0.6f) / 0.15f));
    float n = fbm(u, v, 4, 4, 111);
    float rust = smooth01((fbm(u, v, 8, 5, 112) - 0.28f) / 0.1f);
    float scratch = smooth01((noise::fbm2a(u * 4, v * 64, 2, 4, 64, 113) - 0.35f) / 0.1f);
    o.col = mix3(vec3(0.86f) * (0.92f + 0.1f * n), {0.45f, 0.27f, 0.15f}, rust * 0.6f) * (1 - scratch * 0.2f);
    o.h = 0.3f + prof * 0.5f;
    o.rough = lerpf(0.45f, 0.85f, rust);
    o.metal = 0.25f;
}

void genCobble(float u, float v, Texel& o) {
    noise::Cell c = noise::worley2(u * 10, v * 10, 10, 121, 0.7f);
    float face = smooth01((c.f2 - c.f1 - 0.04f) / 0.12f);
    float dome = saturate(1 - c.f1 * 1.3f);
    vec3 stone = mix3({0.56f, 0.52f, 0.47f}, {0.74f, 0.68f, 0.58f}, (c.id & 255) / 255.0f) *
                 (0.92f + 0.16f * (0.5f + fbm(u, v, 24, 3, 122)));
    o.col = mix3({0.72f, 0.61f, 0.45f}, stone, face);
    o.h = 0.25f + face * (0.35f + dome * 0.35f);
    o.rough = lerpf(0.95f, 0.75f, face);
}

struct MatDef {
    GenFn fn;
};

const GenFn kGenerators[MAT_COUNT] = {genSand, genSandstone, genPlaster, genTiles, genCrate, genDoor,
                                      genMetal, genConcrete, genTrim, genBrick, genCorrugated, genCobble};

MaterialInfo g_info[MAT_COUNT] = {
    {"sand", SURF_SAND, 3.0f, {}},        {"sandstone", SURF_STONE, 5.0f, {}}, {"plaster", SURF_STONE, 4.0f, {}},
    {"tiles", SURF_STONE, 5.0f, {}},      {"crate", SURF_WOOD, 5.0f, {}},      {"door", SURF_WOOD, 4.0f, {}},
    {"metal", SURF_METAL, 3.0f, {}},      {"concrete", SURF_CONCRETE, 3.0f, {}}, {"trim", SURF_STONE, 2.5f, {}},
    {"brick", SURF_STONE, 5.0f, {}},      {"corrugated", SURF_METAL, 6.0f, {}}, {"cobble", SURF_STONE, 6.0f, {}},
};

void generateOne(int m, int size, std::vector<uint8_t>& alb, std::vector<uint8_t>& nrm) {
    std::vector<float> height((size_t)size * size);
    std::vector<Texel> tex((size_t)size * size);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            Texel t;
            kGenerators[m]((x + 0.5f) / size, (y + 0.5f) / size, t);
            tex[(size_t)y * size + x] = t;
            height[(size_t)y * size + x] = t.h;
        }
    // Local average of height for a cheap cavity term.
    const int R = 3;
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
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            size_t i = (size_t)y * size + x;
            const Texel& t = tex[i];
            float dx = (h(x + 1, y) - h(x - 1, y)) * bump;
            float dy = (h(x, y + 1) - h(x, y - 1)) * bump;
            vec3 n = normalize(vec3(-dx, -dy, 1.0f));
            float cavity = saturate(1.0f - std::max(0.0f, blur[i] - height[i]) * 3.0f);
            auto b = [](float f) { return (uint8_t)(saturate(f) * 255.0f + 0.5f); };
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

void generateWorldTextures(WorldTextureSet& out, int size) {
    out.size = size;
    out.albedo.assign(MAT_COUNT, {});
    out.normal.assign(MAT_COUNT, {});
    std::vector<std::thread> threads;
    for (int m = 0; m < MAT_COUNT; m++)
        threads.emplace_back([&, m] { generateOne(m, size, out.albedo[m], out.normal[m]); });
    for (auto& t : threads) t.join();
}
