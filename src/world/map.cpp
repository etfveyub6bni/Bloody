#include "world/map.h"

#include <chrono>
#include <thread>
#include <unordered_map>

#include "assets/textures.h"
#include "core/noise.h"

namespace {

constexpr float kMapBottom = -64.0f;

std::atomic<int> g_bakeQuality{1};

vec3 tintRGB(uint32_t t) { return vec3((t & 255) / 255.0f, ((t >> 8) & 255) / 255.0f, ((t >> 16) & 255) / 255.0f); }

template <class F>
void parallelFor(int count, F&& fn) {
    int nt = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<int> next{0};
    std::vector<std::thread> th;
    for (int t = 0; t < nt; t++)
        th.emplace_back([&] {
            for (;;) {
                int i = next.fetch_add(64);
                if (i >= count) break;
                for (int k = i; k < std::min(count, i + 64); k++) fn(k);
            }
        });
    for (auto& t : th) t.join();
}

std::vector<vec2> clip2(const std::vector<vec2>& poly, vec2 n, float d) {  // keep dot(n,p) <= d
    std::vector<vec2> out;
    size_t cnt = poly.size();
    for (size_t i = 0; i < cnt; i++) {
        vec2 a = poly[i], b = poly[(i + 1) % cnt];
        float da = dot(n, a) - d, db = dot(n, b) - d;
        bool ia = da <= 1e-4f, ib = db <= 1e-4f;
        if (ia) out.push_back(a);
        if (ia != ib) out.push_back(a + (b - a) * (da / (da - db)));
    }
    return out;
}

float area2(const std::vector<vec2>& p) {
    float a = 0;
    for (size_t i = 0; i < p.size(); i++) a += cross2(p[i], p[(i + 1) % p.size()]);
    return a * 0.5f;
}

}  // namespace

void setLightBakeQuality(int q) { g_bakeQuality = std::max(0, std::min(q, 3)); }

void GameMap::buildRenderGeometry() {
    verts.clear();
    indices.clear();
    chunks.clear();
    bounds = AABB();
    for (const auto& b : world.brushes)
        if (!(b.flags & BF_NODRAW)) bounds.add(b.bounds);
    if (!bounds.valid()) return;
    const float chunkSize = 512.0f;
    int cw = std::max(1, (int)std::ceil(bounds.size().x / chunkSize));
    int chh = std::max(1, (int)std::ceil(bounds.size().y / chunkSize));
    std::vector<std::vector<uint32_t>> chunkIdx((size_t)cw * chh);

    for (int bi = 0; bi < (int)world.brushes.size(); bi++) {
        const Brush& b = world.brushes[bi];
        if (b.flags & BF_NODRAW) continue;
        for (int pi = 0; pi < b.numFacePlanes; pi++) {
            std::vector<vec3> poly = brushFacePolygon(b, pi);
            if (poly.size() < 3) continue;
            vec3 n = b.planes[pi].n;
            vec3 cen(0);
            for (auto& p : poly) cen += p;
            cen /= (float)poly.size();
            if (n.z < -0.9f && cen.z <= kMapBottom + 1.0f) continue;

            int ax = std::fabs(n.x) > std::fabs(n.y) ? (std::fabs(n.x) > std::fabs(n.z) ? 0 : 2) : (std::fabs(n.y) > std::fabs(n.z) ? 1 : 2);
            vec3 uAxis, vAxis;
            if (ax == 2) { uAxis = {1, 0, 0}; vAxis = {0, 1, 0}; }
            else if (ax == 0) { uAxis = n.x > 0 ? vec3(0, 1, 0) : vec3(0, -1, 0); vAxis = {0, 0, 1}; }
            else { uAxis = n.y > 0 ? vec3(-1, 0, 0) : vec3(1, 0, 0); vAxis = {0, 0, 1}; }
            vec3 T = normalize(uAxis - n * dot(uAxis, n));
            vec3 Bexp = normalize(vAxis - n * dot(vAxis, n));
            float bsign = dot(cross(n, T), Bexp) >= 0 ? 1.0f : -1.0f;
            float uMin = 1e30f, uMax = -1e30f, vMin = 1e30f, vMax = -1e30f;
            for (auto& p : poly) {
                uMin = std::min(uMin, dot(p, uAxis)); uMax = std::max(uMax, dot(p, uAxis));
                vMin = std::min(vMin, dot(p, vAxis)); vMax = std::max(vMax, dot(p, vAxis));
            }
            float texUnits = 128.0f * b.uvScale;
            uint32_t tint = (b.tint & 0x00FFFFFFu) | ((uint32_t)b.material << 24);

            vec3 e1 = T, e2 = cross(n, e1);
            vec3 org = n * b.planes[pi].d;
            std::vector<vec2> p2;
            float a0 = 1e30f, a1 = -1e30f, c0 = 1e30f, c1 = -1e30f;
            for (auto& p : poly) {
                vec2 q(dot(p - org, e1), dot(p - org, e2));
                p2.push_back(q);
                a0 = std::min(a0, q.x); a1 = std::max(a1, q.x); c0 = std::min(c0, q.y); c1 = std::max(c1, q.y);
            }
            if (std::fabs(area2(p2)) < 0.5f) continue;
            float cell = cen.z > 420.0f ? 128.0f : 32.0f;
            int nu = std::max(1, (int)std::ceil((a1 - a0) / cell - 0.01f));
            int nv = std::max(1, (int)std::ceil((c1 - c0) / cell - 0.01f));
            float du = (a1 - a0) / nu, dv = (c1 - c0) / nv;
            std::unordered_map<uint64_t, uint32_t> vmap;
            auto vertexFor = [&](vec2 q) -> uint32_t {
                int64_t qx = (int64_t)std::llround(q.x * 16.0), qy = (int64_t)std::llround(q.y * 16.0);
                uint64_t key = ((uint64_t)(qx + (1LL << 31)) << 32) ^ (uint64_t)(qy + (1LL << 31));
                auto it = vmap.find(key);
                if (it != vmap.end()) return it->second;
                vec3 p = org + e1 * q.x + e2 * q.y;
                WorldVertex v;
                v.pos = p;
                v.normal = n;
                if (b.fitUV)
                    v.uv = vec2((dot(p, uAxis) - uMin) / std::max(uMax - uMin, 1.0f), (dot(p, vAxis) - vMin) / std::max(vMax - vMin, 1.0f));
                else
                    v.uv = vec2(dot(p, uAxis) / texUnits, dot(p, vAxis) / texUnits);
                v.tangent = vec4(T, bsign);
                v.light = vec4(0.5f, 0.5f, 0.5f, 1.0f);
                v.tint = tint;
                uint32_t idx = (uint32_t)verts.size();
                verts.push_back(v);
                vmap.emplace(key, idx);
                return idx;
            };
            for (int j = 0; j < nv; j++)
                for (int i = 0; i < nu; i++) {
                    float x0 = a0 + i * du, x1 = a0 + (i + 1) * du, y0 = c0 + j * dv, y1 = c0 + (j + 1) * dv;
                    std::vector<vec2> cp = p2;
                    cp = clip2(cp, {1, 0}, x1);
                    if (cp.size() >= 3) cp = clip2(cp, {-1, 0}, -x0);
                    if (cp.size() >= 3) cp = clip2(cp, {0, 1}, y1);
                    if (cp.size() >= 3) cp = clip2(cp, {0, -1}, -y0);
                    if (cp.size() < 3 || std::fabs(area2(cp)) < 0.25f) continue;
                    vec2 c2(0);
                    for (auto& q : cp) c2 += q;
                    c2 = c2 / (float)cp.size();
                    vec3 c3 = org + e1 * c2.x + e2 * c2.y;
                    // Drop a piece only when it is hidden everywhere: testing just the centre removed wall
                    // pieces straddling the floor behind the skirting and left sky-coloured gaps at wall bases.
                    auto hiddenAt = [&](vec2 q) { return world.pointInsideVisible(org + e1 * q.x + e2 * q.y + n * 1.0f, bi); };
                    bool hidden = hiddenAt(c2);
                    for (size_t k = 0; hidden && k < cp.size(); k++) hidden = hiddenAt(lerp(c2, cp[k], 0.85f));
                    if (hidden) continue;
                    int cx = std::min(cw - 1, std::max(0, (int)((c3.x - bounds.mn.x) / chunkSize)));
                    int cy = std::min(chh - 1, std::max(0, (int)((c3.y - bounds.mn.y) / chunkSize)));
                    auto& out = chunkIdx[(size_t)cy * cw + cx];
                    uint32_t first = vertexFor(cp[0]);
                    for (size_t k = 1; k + 1 < cp.size(); k++) {
                        out.push_back(first);
                        out.push_back(vertexFor(cp[k]));
                        out.push_back(vertexFor(cp[k + 1]));
                    }
                }
        }
    }
    for (auto& ci : chunkIdx) {
        if (ci.empty()) continue;
        WorldChunk c;
        c.first = (uint32_t)indices.size();
        c.count = (uint32_t)ci.size();
        for (uint32_t i : ci) c.bounds.add(verts[i].pos);
        indices.insert(indices.end(), ci.begin(), ci.end());
        chunks.push_back(c);
    }
}

vec3 GameMap::traceRadiance(vec3 origin, vec3 dir, float* hitDist, int sx, int sy) const {
    const Environment& env = info.env;
    const float far = 9000.0f;
    TraceResult tr = world.traceRay(origin, origin + dir * far, MASK_SHOT);
    if (tr.startSolid) {
        *hitDist = 0;
        return vec3(0);
    }
    if (tr.fraction >= 1.0f) {
        *hitDist = 1e9f;
        return env.skyRadiance(dir) * env.skyIntensity;
    }
    *hitDist = tr.fraction * far;
    const Brush& b = world.brushes[tr.brush];
    vec3 alb = materialInfo(b.material).avgAlbedo * tintRGB(b.tint);
    vec3 L = env.indoorAmbient;
    if (g_bakeQuality >= 2) {
        // One cosine-weighted sky sample from the hit point: a path-traced second bounce, so surfaces deep
        // in tunnels and corners stop receiving a constant sky term.
        vec3 n = tr.normal, t = anyPerp(n), bt = cross(n, t);
        float u1 = noise::hashf(sx, sy, 11, 131), u2 = noise::hashf(sx, sy, 13, 137);
        float r = std::sqrt(u1), phi = kTwoPi * u2;
        vec3 d2 = t * (r * std::cos(phi)) + bt * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1 - u1));
        vec3 hp = tr.endpos + n * 0.5f;
        TraceResult t2 = world.traceRay(hp, hp + d2 * far, MASK_SHOT);
        if (t2.fraction >= 1.0f && !t2.startSolid) L += env.skyRadiance(d2) * env.skyIntensity * 0.6f;
        else L += (env.skyZenith + env.skyHorizon) * 0.5f * env.skyIntensity * 0.05f;
    } else {
        L += (env.skyZenith + env.skyHorizon) * 0.5f * env.skyIntensity * 0.3f;
    }
    float ndl = dot(tr.normal, env.sunDir);
    if (ndl > 0) {
        vec3 hp = tr.endpos + tr.normal * 0.5f;
        TraceResult sh = world.traceRay(hp, hp + env.sunDir * far, MASK_SHOT);
        if (sh.fraction >= 1.0f && !sh.startSolid) L += env.sunColor * (ndl / kPi) * env.bounceScale;
    }
    return alb * L;
}

void GameMap::bakeLighting(int baseRays) {
    progress = 0;
    auto t0 = std::chrono::steady_clock::now();
    const int q = g_bakeQuality;
    const int rays = q == 0 ? std::max(16, baseRays / 2) : q == 3 ? baseRays * 3 / 2 : baseRays;
    const int count = (int)verts.size();
    std::atomic<int> done{0};
    parallelFor(count, [&](int vi) {
        WorldVertex& v = verts[vi];
        vec3 n = v.normal;
        vec3 o = v.pos + n * 1.0f;
        const vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int it = 0; it < 2; it++)
            for (const vec3& a : axes)
                if (dot(a, n) < 0.5f && world.pointSolid(o + a * 1.0f, MASK_SHOT)) o -= a * 1.0f;
        // Vertices of pieces that dip below the floor start inside solids: bake them from just above.
        if (world.pointSolid(o, MASK_SHOT))
            for (int s = 1; s <= 16; s++) {
                vec3 q = o + vec3(0, 0, 4.0f * s);
                if (!world.pointSolid(q, MASK_SHOT)) {
                    o = q;
                    break;
                }
            }
        vec3 t = anyPerp(n), bt = cross(n, t);
        float rot = noise::hashf(vi, 7, 3, 99);
        vec3 sum(0);
        float occl = 0;
        for (int k = 0; k < rays; k++) {
            float u1 = (k + noise::hashf(vi, k, 1, 17)) / rays;
            float u2 = std::fmod(k * 0.618034f + rot, 1.0f);
            float r = std::sqrt(u1), phi = kTwoPi * u2;
            vec3 d = t * (r * std::cos(phi)) + bt * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1 - u1));
            float dist;
            sum += traceRadiance(o, d, &dist, vi, k);
            if (dist < 80.0f) occl += 1.0f - dist / 80.0f;
        }
        v.light = vec4(sum / (float)rays, 1.0f - 0.55f * occl / rays);
        int d = done.fetch_add(1) + 1;
        if ((d & 255) == 0) progress = (float)d / (float)count;
    });
    progress = 1.0f;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    logInfo("Light bake: %d verts, %d rays, quality %d, %.0f ms", count, rays, q, ms);
}

void GameMap::buildNavigation() {
    nav.w = layoutW;
    nav.h = layoutH;
    nav.cell = layoutCell;
    nav.origin = layoutOrigin;
    size_t n = (size_t)nav.w * nav.h;
    nav.z.assign(n, NAN);
    nav.links.assign(n, 0);
    const vec3 hmin(-12, -12, 0), hmax(12, 12, 40);
    parallelFor((int)n, [&](int i) {
        float lf = layoutFloor[i];
        if (std::isnan(lf)) return;
        float cx = nav.origin.x + (i % nav.w + 0.5f) * nav.cell, cy = nav.origin.y + (i / nav.w + 0.5f) * nav.cell;
        float top = lf + 96.0f;
        if (!std::isnan(layoutCeil[i])) top = std::min(top, layoutCeil[i] - 42.0f);
        TraceResult tr = world.trace(vec3(cx, cy, top), vec3(cx, cy, lf - 40.0f), hmin, hmax, MASK_PLAYER);
        if (tr.startSolid || tr.fraction >= 1.0f || tr.normal.z < 0.7f) return;
        float gz = tr.endpos.z;
        TraceResult up = world.trace(vec3(cx, cy, gz + 1), vec3(cx, cy, gz + 33), hmin, hmax, MASK_PLAYER);
        if (up.fraction < 1.0f || up.startSolid) return;
        nav.z[i] = gz;
    });
    parallelFor((int)n, [&](int i) {
        if (std::isnan(nav.z[i])) return;
        int x = i % nav.w, y = i / nav.w;
        vec3 a = nav.center(i);
        uint8_t bits = 0;
        for (int d = 0; d < 8; d++) {
            int nx = x + kNavDX[d], ny = y + kNavDY[d];
            if (!nav.walkable(nx, ny)) continue;
            if ((d & 1) && (!nav.walkable(x + kNavDX[d], y) || !nav.walkable(x, y + kNavDY[d]))) continue;
            int ni = ny * nav.w + nx;
            vec3 b = nav.center(ni);
            if (std::fabs(b.z - a.z) > 20.0f) continue;
            float zz = std::max(a.z, b.z) + 20.0f;
            TraceResult tr = world.trace(vec3(a.x, a.y, zz), vec3(b.x, b.y, zz), hmin, vec3(12, 12, 30), MASK_PLAYER);
            if (tr.fraction < 1.0f || tr.startSolid) continue;
            bits |= (uint8_t)(1 << d);
        }
        nav.links[i] = bits;
    });
}

void GameMap::bakeProbes() {
    probes.w = nav.w;
    probes.h = nav.h;
    probes.cell = nav.cell;
    probes.origin = nav.origin;
    size_t n = (size_t)nav.w * nav.h;
    probes.up.assign(n, vec3(-1));
    probes.down.assign(n, vec3(-1));
    probes.z.assign(n, 0);
    const int rays = 32;
    parallelFor((int)n, [&](int i) {
        if (std::isnan(nav.z[i])) return;
        vec3 p = nav.center(i) + vec3(0, 0, 48);
        probes.z[i] = p.z;
        for (int hemi = 0; hemi < 2; hemi++) {
            vec3 nn(0, 0, hemi == 0 ? 1.0f : -1.0f);
            vec3 sum(0);
            for (int k = 0; k < rays; k++) {
                float u1 = (k + noise::hashf(i, k, hemi, 5)) / rays;
                float u2 = std::fmod(k * 0.618034f + noise::hashf(i, 3, hemi, 6), 1.0f);
                float r = std::sqrt(u1), phi = kTwoPi * u2;
                vec3 d(r * std::cos(phi), r * std::sin(phi), nn.z * std::sqrt(std::max(0.0f, 1 - u1)));
                float dist;
                sum += traceRadiance(p, d, &dist, i * 2 + hemi, k + 7919);
            }
            (hemi == 0 ? probes.up[i] : probes.down[i]) = sum / (float)rays;
        }
    });
    // Dilate into non-walkable cells so lookups near walls stay valid.
    for (int pass = 0; pass < 6; pass++) {
        std::vector<vec3> nu = probes.up, nd = probes.down;
        for (int y = 0; y < probes.h; y++)
            for (int x = 0; x < probes.w; x++) {
                size_t i = (size_t)y * probes.w + x;
                if (probes.up[i].x >= 0) continue;
                vec3 su(0), sd(0);
                int c = 0;
                for (int d = 0; d < 8; d++) {
                    int nx = x + kNavDX[d], ny = y + kNavDY[d];
                    if (nx < 0 || ny < 0 || nx >= probes.w || ny >= probes.h) continue;
                    size_t j = (size_t)ny * probes.w + nx;
                    if (probes.up[j].x < 0) continue;
                    su += probes.up[j];
                    sd += probes.down[j];
                    c++;
                }
                if (c) { nu[i] = su / (float)c; nd[i] = sd / (float)c; }
            }
        probes.up.swap(nu);
        probes.down.swap(nd);
    }
}

void GameMap::sampleAmbient(vec3 p, vec3& up, vec3& down) const {
    const Environment& env = info.env;
    vec3 defUp = (env.skyZenith + env.skyHorizon) * 0.5f, defDown = env.groundColor * 0.8f;
    if (probes.w == 0) { up = defUp; down = defDown; return; }
    float fx = (p.x - probes.origin.x) / probes.cell - 0.5f, fy = (p.y - probes.origin.y) / probes.cell - 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float tx = fx - x0, ty = fy - y0;
    vec3 su(0), sd(0);
    float wsum = 0;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            int x = x0 + i, y = y0 + j;
            if (x < 0 || y < 0 || x >= probes.w || y >= probes.h) continue;
            size_t k = (size_t)y * probes.w + x;
            if (probes.up[k].x < 0) continue;
            float w = (i ? tx : 1 - tx) * (j ? ty : 1 - ty) + 1e-4f;
            su += probes.up[k] * w;
            sd += probes.down[k] * w;
            wsum += w;
        }
    if (wsum <= 0) { up = defUp; down = defDown; return; }
    up = su / wsum;
    down = sd / wsum;
}

const SpawnPoint* GameMap::randomSpawn(int team, Rng& rng) const {
    std::vector<const SpawnPoint*> c;
    for (const auto& s : spawns)
        if (team == TEAM_NONE || s.team == team) c.push_back(&s);
    if (c.empty()) return spawns.empty() ? nullptr : &spawns[0];
    return c[rng.next() % c.size()];
}
