#include "world/map_builder.h"

#include "assets/textures.h"

namespace {
constexpr float kBase = -64.0f;
}

MapBuilder::MapBuilder(GameMap& map, float x0, float y0, float x1, float y1, float cell)
    : m_map(map), m_x0(x0), m_y0(y0), m_cell(cell) {
    m_w = (int)std::ceil((x1 - x0) / cell);
    m_h = (int)std::ceil((y1 - y0) / cell);
    m_cells.assign((size_t)m_w * m_h, Cell());
    for (auto& c : m_cells) {
        c.wallMat = MAT_PLASTER;
        c.lowerMat = MAT_SANDSTONE;
        c.band = 80;
    }
    map.world.clear();
    map.spawns.clear();
    map.sites.clear();
}

uint32_t MapBuilder::rgb(float r, float g, float b) { return packRGBA(r, g, b, 1.0f); }

uint32_t MapBuilder::jitterTint(uint32_t tint) {
    float k = m_rng.range(0.93f, 1.0f);
    auto ch = [&](int s) { return ((tint >> s) & 255) / 255.0f * k; };
    return packRGBA(ch(0), ch(8), ch(16), 1.0f);
}

void MapBuilder::carve(float x0, float y0, float x1, float y1, float z, int mat) {
    for (int j = 0; j < m_h; j++)
        for (int i = 0; i < m_w; i++) {
            float cx = m_x0 + (i + 0.5f) * m_cell, cy = m_y0 + (j + 0.5f) * m_cell;
            if (cx < x0 || cx > x1 || cy < y0 || cy > y1) continue;
            Cell& c = m_cells[(size_t)j * m_w + i];
            c.walk = true;
            c.floorZ = z;
            c.floorMat = mat;
        }
}

void MapBuilder::roof(float x0, float y0, float x1, float y1, float z, int mat) {
    for (int j = 0; j < m_h; j++)
        for (int i = 0; i < m_w; i++) {
            float cx = m_x0 + (i + 0.5f) * m_cell, cy = m_y0 + (j + 0.5f) * m_cell;
            if (cx < x0 || cx > x1 || cy < y0 || cy > y1) continue;
            Cell& c = m_cells[(size_t)j * m_w + i];
            c.roofed = true;
            c.ceilZ = z;
            c.ceilMat = mat;
        }
}

void MapBuilder::fill(float x0, float y0, float x1, float y1) {
    for (int j = 0; j < m_h; j++)
        for (int i = 0; i < m_w; i++) {
            float cx = m_x0 + (i + 0.5f) * m_cell, cy = m_y0 + (j + 0.5f) * m_cell;
            if (cx < x0 || cx > x1 || cy < y0 || cy > y1) continue;
            Cell& c = m_cells[(size_t)j * m_w + i];
            c.walk = false;
            c.roofed = false;
        }
}

void MapBuilder::wallStyle(float x0, float y0, float x1, float y1, float top, int mat, int lowerMat, float band) {
    for (int j = 0; j < m_h; j++)
        for (int i = 0; i < m_w; i++) {
            float cx = m_x0 + (i + 0.5f) * m_cell, cy = m_y0 + (j + 0.5f) * m_cell;
            if (cx < x0 || cx > x1 || cy < y0 || cy > y1) continue;
            Cell& c = m_cells[(size_t)j * m_w + i];
            c.top = top;
            c.wallMat = mat;
            c.lowerMat = lowerMat;
            c.band = band;
        }
}

Brush& MapBuilder::add(Brush b, int mat, uint32_t tint) {
    b.material = mat;
    b.tint = jitterTint(tint);
    b.surface = materialInfo(mat).surface;
    b.penetrationScale = b.surface == SURF_WOOD ? 0.45f : b.surface == SURF_METAL ? 1.6f : 1.0f;
    m_map.world.brushes.push_back(std::move(b));
    return m_map.world.brushes.back();
}

Brush& MapBuilder::box(vec3 mn, vec3 mx, int mat, uint32_t tint) { return add(makeBoxBrush(mn, mx), mat, tint); }

Brush& MapBuilder::crate(float x, float y, float z, float size, float yaw) {
    float h = size * 0.5f;
    Brush b = yaw == 0 ? makeBoxBrush({x - h, y - h, z}, {x + h, y + h, z + size})
                       : makeOrientedBoxBrush({x, y, z + h}, {h, h, h}, yaw);
    b.fitUV = true;
    return add(std::move(b), MAT_CRATE, 0xFFFFFFFFu);
}

Brush& MapBuilder::wedge(vec3 mn, vec3 mx, int dir, int mat) { return add(makeWedgeBrush(mn, mx, dir), mat, 0xFFFFFFFFu); }

void MapBuilder::stairs(float x0, float y0, float x1, float y1, float z0, int steps, float rise, int dir, int mat) {
    for (int k = 0; k < steps; k++) {
        float t0 = (float)k / steps;
        vec3 mn(x0, y0, kBase), mx(x1, y1, z0 + (k + 1) * rise);
        if (dir == 0) mn.x = x0 + (x1 - x0) * t0;
        else if (dir == 1) mx.x = x1 - (x1 - x0) * t0;
        else if (dir == 2) mn.y = y0 + (y1 - y0) * t0;
        else mx.y = y1 - (y1 - y0) * t0;
        box(mn, mx, mat);
    }
}

Brush& MapBuilder::prism(float x, float y, float r, int sides, float z0, float z1, int mat, float rot) {
    return add(makePrismBrush({x, y}, r, sides, z0, z1, rot), mat, 0xFFFFFFFFu);
}

Brush& MapBuilder::obox(vec3 c, vec3 half, float yaw, int mat, uint32_t tint) {
    return add(makeOrientedBoxBrush(c, half, yaw), mat, tint);
}

void MapBuilder::clipBox(vec3 mn, vec3 mx) {
    Brush b = makeBoxBrush(mn, mx);
    b.flags = BF_PLAYERCLIP | BF_NODRAW;
    m_map.world.brushes.push_back(std::move(b));
}

void MapBuilder::spawn(int team, float x, float y, float z, float yaw) { m_map.spawns.push_back({{x, y, z}, yaw, team}); }

void MapBuilder::site(char letter, float x0, float y0, float x1, float y1) {
    BombSite s;
    s.letter = letter;
    s.box = AABB({x0, y0, -32}, {x1, y1, 256});
    m_map.sites.push_back(s);
}

template <class Elig, class Same, class Emit>
void MapBuilder::greedy(Elig elig, Same same, Emit emit) {
    std::vector<uint8_t> used((size_t)m_w * m_h, 0);
    for (int j = 0; j < m_h; j++)
        for (int i = 0; i < m_w; i++) {
            size_t idx = (size_t)j * m_w + i;
            if (used[idx] || !elig(m_cells[idx])) continue;
            const Cell& ref = m_cells[idx];
            int i1 = i;
            while (i1 + 1 < m_w) {
                size_t k = (size_t)j * m_w + i1 + 1;
                if (used[k] || !elig(m_cells[k]) || !same(ref, m_cells[k])) break;
                i1++;
            }
            int j1 = j;
            for (;;) {
                if (j1 + 1 >= m_h) break;
                bool ok = true;
                for (int k = i; k <= i1 && ok; k++) {
                    size_t q = (size_t)(j1 + 1) * m_w + k;
                    if (used[q] || !elig(m_cells[q]) || !same(ref, m_cells[q])) ok = false;
                }
                if (!ok) break;
                j1++;
            }
            for (int jj = j; jj <= j1; jj++)
                for (int ii = i; ii <= i1; ii++) used[(size_t)jj * m_w + ii] = 1;
            emit(ref, m_x0 + i * m_cell, m_y0 + j * m_cell, m_x0 + (i1 + 1) * m_cell, m_y0 + (j1 + 1) * m_cell);
        }
}

void MapBuilder::trims() {
    auto cellAt = [&](int i, int j) -> const Cell* {
        if (i < 0 || j < 0 || i >= m_w || j >= m_h) return nullptr;
        return &m_cells[(size_t)j * m_w + i];
    };
    // For each of 4 directions, walk rows/columns and merge runs of edges between walkable and solid cells.
    for (int dir = 0; dir < 4; dir++) {
        bool alongX = dir >= 2;  // dir 2/3: neighbour in -Y/+Y, edge runs along X
        int outer = alongX ? m_h : m_w, inner = alongX ? m_w : m_h;
        for (int o = 0; o < outer; o++) {
            int runStart = -1;
            float runZ = 0, runTop = 0;
            bool runCornice = false;
            auto flush = [&](int endExclusive) {
                if (runStart < 0) return;
                float a0 = (alongX ? m_x0 : m_y0) + runStart * m_cell;
                float a1 = (alongX ? m_x0 : m_y0) + endExclusive * m_cell;
                float edge;
                float sgn;
                if (dir == 0) { edge = m_x0 + (o + 1) * m_cell; sgn = -1; }       // solid at +X
                else if (dir == 1) { edge = m_x0 + o * m_cell; sgn = 1; }         // solid at -X
                else if (dir == 2) { edge = m_y0 + (o + 1) * m_cell; sgn = -1; }  // solid at +Y
                else { edge = m_y0 + o * m_cell; sgn = 1; }                       // solid at -Y
                auto mkBox = [&](float depth, float z0, float z1) {
                    float e0 = edge, e1 = edge + sgn * depth;
                    if (e0 > e1) std::swap(e0, e1);
                    vec3 mn = alongX ? vec3(a0, e0, z0) : vec3(e0, a0, z0);
                    vec3 mx = alongX ? vec3(a1, e1, z1) : vec3(e1, a1, z1);
                    box(mn, mx, MAT_TRIM);
                };
                mkBox(3.0f, runZ, runZ + 10.0f);
                if (runCornice) mkBox(6.0f, runTop - 14.0f, runTop);
                runStart = -1;
            };
            for (int k = 0; k < inner; k++) {
                int i = alongX ? k : o, j = alongX ? o : k;
                const Cell* c = cellAt(i, j);
                int ni = i + (dir == 0 ? 1 : dir == 1 ? -1 : 0), nj = j + (dir == 2 ? 1 : dir == 3 ? -1 : 0);
                const Cell* nb = cellAt(ni, nj);
                bool edge = c && c->walk && nb && !nb->walk;
                bool cornice = edge && !c->roofed && nb->top - c->floorZ >= 128.0f;
                if (edge && runStart >= 0 && (c->floorZ != runZ || cornice != runCornice || (cornice && nb->top != runTop))) flush(k);
                if (!edge) { flush(k); continue; }
                if (runStart < 0) {
                    runStart = k;
                    runZ = c->floorZ;
                    runCornice = cornice;
                    runTop = nb->top;
                }
            }
            flush(inner);
        }
    }
}

void MapBuilder::finish() {
    greedy([](const Cell& c) { return c.walk; },
           [](const Cell& a, const Cell& b) { return a.floorZ == b.floorZ && a.floorMat == b.floorMat; },
           [&](const Cell& c, float x0, float y0, float x1, float y1) { box({x0, y0, kBase}, {x1, y1, c.floorZ}, c.floorMat); });
    greedy([](const Cell& c) { return !c.walk; },
           [](const Cell& a, const Cell& b) {
               return a.top == b.top && a.wallMat == b.wallMat && a.lowerMat == b.lowerMat && a.band == b.band;
           },
           [&](const Cell& c, float x0, float y0, float x1, float y1) {
               if (c.lowerMat >= 0 && c.band > kBase && c.band < c.top) {
                   box({x0, y0, kBase}, {x1, y1, c.band}, c.lowerMat);
                   box({x0, y0, c.band}, {x1, y1, c.top}, c.wallMat);
               } else {
                   box({x0, y0, kBase}, {x1, y1, c.top}, c.wallMat);
               }
           });
    greedy([](const Cell& c) { return c.walk && c.roofed; },
           [](const Cell& a, const Cell& b) { return a.ceilZ == b.ceilZ && a.ceilMat == b.ceilMat; },
           [&](const Cell& c, float x0, float y0, float x1, float y1) { box({x0, y0, c.ceilZ}, {x1, y1, c.ceilZ + 24}, c.ceilMat); });
    trims();

    m_map.layoutW = m_w;
    m_map.layoutH = m_h;
    m_map.layoutCell = m_cell;
    m_map.layoutOrigin = {m_x0, m_y0};
    m_map.layoutFloor.assign((size_t)m_w * m_h, NAN);
    m_map.layoutCeil.assign((size_t)m_w * m_h, NAN);
    for (size_t i = 0; i < m_cells.size(); i++) {
        if (m_cells[i].walk) m_map.layoutFloor[i] = m_cells[i].floorZ;
        if (m_cells[i].walk && m_cells[i].roofed) m_map.layoutCeil[i] = m_cells[i].ceilZ;
    }
    m_map.world.build();
}
