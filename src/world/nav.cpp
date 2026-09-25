#include "world/nav.h"

#include <queue>

#include "world/collision.h"

const int kNavDX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
const int kNavDY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

bool NavGrid::walkable(int x, int y) const {
    return x >= 0 && y >= 0 && x < w && y < h && !std::isnan(z[(size_t)y * w + x]);
}

bool NavGrid::toCell(vec3 p, int& cx, int& cy) const {
    cx = (int)std::floor((p.x - origin.x) / cell);
    cy = (int)std::floor((p.y - origin.y) / cell);
    return cx >= 0 && cy >= 0 && cx < w && cy < h;
}

vec3 NavGrid::center(int idx) const {
    int x = idx % w, y = idx / w;
    return {origin.x + (x + 0.5f) * cell, origin.y + (y + 0.5f) * cell, z[idx]};
}

int NavGrid::nearest(vec3 p, int maxRadius) const {
    int cx, cy;
    toCell(p, cx, cy);
    int best = -1;
    float bestD = 1e30f;
    for (int r = 0; r <= maxRadius; r++) {
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++) {
                if (std::max(std::abs(x - cx), std::abs(y - cy)) != r || !walkable(x, y)) continue;
                int i = y * w + x;
                vec3 c = center(i);
                float d = length2(vec3(c.x - p.x, c.y - p.y, (c.z - p.z) * 2.0f));
                if (d < bestD) { bestD = d; best = i; }
            }
        if (best >= 0) return best;
    }
    return best;
}

bool NavGrid::findPath(vec3 from, vec3 to, std::vector<vec3>& out) const {
    out.clear();
    int s = nearest(from), g = nearest(to);
    if (s < 0 || g < 0) return false;
    size_t n = (size_t)w * h;
    std::vector<float> gcost(n, 1e30f);
    std::vector<int> parent(n, -1);
    std::vector<uint8_t> closed(n, 0);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    int gx = g % w, gy = g / w;
    auto heur = [&](int i) {
        int dx = std::abs(i % w - gx), dy = std::abs(i / w - gy);
        return (float)std::max(dx, dy) + 0.4142f * (float)std::min(dx, dy);
    };
    gcost[s] = 0;
    open.push({heur(s), s});
    int iter = 0;
    bool found = false;
    while (!open.empty() && iter++ < 60000) {
        int cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == g) { found = true; break; }
        int cx = cur % w, cy = cur / w;
        for (int d = 0; d < 8; d++) {
            if (!(links[cur] & (1 << d))) continue;
            int ni = (cy + kNavDY[d]) * w + (cx + kNavDX[d]);
            if (closed[ni]) continue;
            float c = gcost[cur] + ((d & 1) ? 1.4142f : 1.0f);
            if (c < gcost[ni]) {
                gcost[ni] = c;
                parent[ni] = cur;
                open.push({c + heur(ni), ni});
            }
        }
    }
    if (!found) return false;
    std::vector<int> cells;
    for (int c = g; c != -1; c = parent[c]) cells.push_back(c);
    for (auto it = cells.rbegin(); it != cells.rend(); ++it) out.push_back(center(*it));
    return true;
}

void NavGrid::smoothPath(std::vector<vec3>& path, const CollisionWorld& world) const {
    if (path.size() < 3) return;
    std::vector<vec3> out;
    out.push_back(path[0]);
    size_t i = 0;
    const vec3 mins(-15, -15, 18), maxs(15, 15, 50);
    while (i < path.size() - 1) {
        size_t best = i + 1;
        for (size_t j = std::min(path.size() - 1, i + 24); j > i + 1; j--) {
            if (std::fabs(path[j].z - path[i].z) > 40) continue;
            // Require level ground along the shortcut, otherwise stairs/ramps get skipped badly.
            bool level = true;
            for (size_t k = i + 1; k < j; k++)
                if (std::fabs(path[k].z - lerpf(path[i].z, path[j].z, (float)(k - i) / (float)(j - i))) > 10) { level = false; break; }
            if (!level) continue;
            vec3 a = path[i], b = path[j];
            float zz = std::max(a.z, b.z);
            TraceResult tr = world.trace(vec3(a.x, a.y, zz), vec3(b.x, b.y, zz), mins, maxs, MASK_PLAYER);
            if (tr.fraction >= 1.0f && !tr.startSolid) { best = j; break; }
        }
        out.push_back(path[best]);
        i = best;
    }
    path.swap(out);
}

bool NavGrid::randomPoint(const AABB& area, Rng& rng, vec3& out) const {
    for (int tries = 0; tries < 64; tries++) {
        vec3 p(rng.range(area.mn.x, area.mx.x), rng.range(area.mn.y, area.mx.y), 0);
        int cx, cy;
        if (!toCell(p, cx, cy) || !walkable(cx, cy)) continue;
        int i = cy * w + cx;
        if (links[i] != 0xFF) continue;  // stay away from walls
        out = center(i);
        if (out.z < area.mn.z - 1 || out.z > area.mx.z + 1) continue;
        return true;
    }
    return false;
}

int NavGrid::countWalkable() const {
    int c = 0;
    for (float v : z) c += std::isnan(v) ? 0 : 1;
    return c;
}
