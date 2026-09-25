#include "world/collision.h"

#include <algorithm>

namespace {

constexpr float kSurfEps = 0.125f;

struct DV {
    double x, y, z;
};

std::vector<DV> clipPoly(const std::vector<DV>& poly, const Plane& p) {
    std::vector<DV> out;
    size_t n = poly.size();
    out.reserve(n + 2);
    for (size_t i = 0; i < n; i++) {
        const DV& a = poly[i];
        const DV& b = poly[(i + 1) % n];
        double da = a.x * p.n.x + a.y * p.n.y + a.z * p.n.z - p.d;
        double db = b.x * p.n.x + b.y * p.n.y + b.z * p.n.z - p.d;
        bool ina = da <= 1e-5, inb = db <= 1e-5;
        if (ina) out.push_back(a);
        if (ina != inb) {
            double t = da / (da - db);
            out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
        }
    }
    return out;
}

Plane mkPlane(vec3 n, vec3 p) {
    n = normalize(n);
    return {n, dot(n, p)};
}

}  // namespace

std::vector<vec3> brushFacePolygon(const Brush& b, int pi) {
    const Plane& pl = b.planes[pi];
    vec3 n = pl.n;
    vec3 u = anyPerp(n);
    vec3 v = cross(n, u);
    vec3 c = n * pl.d;
    const double S = 32768.0;
    std::vector<DV> poly;
    auto P = [&](double su, double sv) {
        return DV{c.x + (u.x * su + v.x * sv), c.y + (u.y * su + v.y * sv), c.z + (u.z * su + v.z * sv)};
    };
    poly.push_back(P(-S, -S));
    poly.push_back(P(S, -S));
    poly.push_back(P(S, S));
    poly.push_back(P(-S, S));
    for (size_t j = 0; j < b.planes.size() && poly.size() >= 3; j++) {
        if ((int)j == pi) continue;
        poly = clipPoly(poly, b.planes[j]);
    }
    std::vector<vec3> out;
    for (const DV& d : poly) {
        vec3 p((float)d.x, (float)d.y, (float)d.z);
        if (!out.empty() && length2(out.back() - p) < 1e-4f) continue;
        out.push_back(p);
    }
    while (out.size() > 1 && length2(out.front() - out.back()) < 1e-4f) out.pop_back();
    if (out.size() < 3) out.clear();
    return out;
}

void finalizeBrush(Brush& b) {
    b.numFacePlanes = (int)b.planes.size();
    b.bounds = AABB();
    for (int i = 0; i < b.numFacePlanes; i++)
        for (const vec3& p : brushFacePolygon(b, i)) b.bounds.add(p);
    // Axial bevels keep box traces from snagging on slanted faces.
    const vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int a = 0; a < 6; a++) {
        bool have = false;
        for (int i = 0; i < b.numFacePlanes; i++)
            if (dot(b.planes[i].n, axes[a]) > 0.9999f) have = true;
        if (have) continue;
        float d = a == 0 ? b.bounds.mx.x : a == 1 ? -b.bounds.mn.x : a == 2 ? b.bounds.mx.y : a == 3 ? -b.bounds.mn.y
                : a == 4 ? b.bounds.mx.z : -b.bounds.mn.z;
        b.planes.push_back({axes[a], d});
    }
}

Brush makeBoxBrush(vec3 mn, vec3 mx) {
    Brush b;
    b.planes = {{{1, 0, 0}, mx.x}, {{-1, 0, 0}, -mn.x}, {{0, 1, 0}, mx.y}, {{0, -1, 0}, -mn.y}, {{0, 0, 1}, mx.z}, {{0, 0, -1}, -mn.z}};
    finalizeBrush(b);
    return b;
}

Brush makeWedgeBrush(vec3 mn, vec3 mx, int dir) {
    Brush b;
    float h = mx.z - mn.z;
    b.planes.push_back({{0, 0, -1}, -mn.z});
    if (dir == 0 || dir == 1) {
        b.planes.push_back({{0, 1, 0}, mx.y});
        b.planes.push_back({{0, -1, 0}, -mn.y});
        float len = mx.x - mn.x;
        if (dir == 0) {
            b.planes.push_back({{1, 0, 0}, mx.x});
            b.planes.push_back(mkPlane({-h, 0, len}, {mn.x, 0, mn.z}));
        } else {
            b.planes.push_back({{-1, 0, 0}, -mn.x});
            b.planes.push_back(mkPlane({h, 0, len}, {mx.x, 0, mn.z}));
        }
    } else {
        b.planes.push_back({{1, 0, 0}, mx.x});
        b.planes.push_back({{-1, 0, 0}, -mn.x});
        float len = mx.y - mn.y;
        if (dir == 2) {
            b.planes.push_back({{0, 1, 0}, mx.y});
            b.planes.push_back(mkPlane({0, -h, len}, {0, mn.y, mn.z}));
        } else {
            b.planes.push_back({{0, -1, 0}, -mn.y});
            b.planes.push_back(mkPlane({0, h, len}, {0, mx.y, mn.z}));
        }
    }
    finalizeBrush(b);
    return b;
}

Brush makePrismBrush(vec2 center, float radius, int sides, float z0, float z1, float rotDeg) {
    Brush b;
    b.planes.push_back({{0, 0, 1}, z1});
    b.planes.push_back({{0, 0, -1}, -z0});
    float apothem = radius * std::cos(kPi / sides);
    for (int i = 0; i < sides; i++) {
        float a = (rotDeg * kDeg) + (i + 0.5f) * kTwoPi / sides;
        vec3 n(std::cos(a), std::sin(a), 0);
        b.planes.push_back({n, dot(n, vec3(center, 0)) + apothem});
    }
    finalizeBrush(b);
    return b;
}

Brush makeOrientedBoxBrush(vec3 c, vec3 half, float yawDeg) {
    Brush b;
    vec3 fx(std::cos(yawDeg * kDeg), std::sin(yawDeg * kDeg), 0), fy(-fx.y, fx.x, 0);
    b.planes = {{fx, dot(fx, c) + half.x}, {-fx, -dot(fx, c) + half.x}, {fy, dot(fy, c) + half.y},
                {-fy, -dot(fy, c) + half.y}, {{0, 0, 1}, c.z + half.z}, {{0, 0, -1}, -(c.z - half.z)}};
    finalizeBrush(b);
    return b;
}

void CollisionWorld::clear() {
    brushes.clear();
    m_nodes.clear();
    m_items.clear();
}

void CollisionWorld::build() {
    m_nodes.clear();
    m_items.resize(brushes.size());
    for (size_t i = 0; i < brushes.size(); i++) m_items[i] = (int)i;
    if (!brushes.empty()) buildNode(0, (int)brushes.size(), 0);
}

int CollisionWorld::buildNode(int first, int count, int depth) {
    Node n;
    for (int i = 0; i < count; i++) n.box.add(brushes[m_items[first + i]].bounds);
    int idx = (int)m_nodes.size();
    m_nodes.push_back(n);
    if (count <= 4 || depth > 30) {
        m_nodes[idx].first = first;
        m_nodes[idx].count = count;
        return idx;
    }
    vec3 s = n.box.size();
    int axis = s.x > s.y ? (s.x > s.z ? 0 : 2) : (s.y > s.z ? 1 : 2);
    int mid = count / 2;
    std::nth_element(m_items.begin() + first, m_items.begin() + first + mid, m_items.begin() + first + count,
                     [&](int a, int b) { return brushes[a].bounds.center()[axis] < brushes[b].bounds.center()[axis]; });
    int l = buildNode(first, mid, depth + 1);
    int r = buildNode(first + mid, count - mid, depth + 1);
    m_nodes[idx].left = l;
    m_nodes[idx].right = r;
    m_nodes[idx].count = 0;
    return idx;
}

void CollisionWorld::clipBrush(int bi, vec3 start, vec3 end, vec3 mins, vec3 maxs, bool isPoint, TraceResult& tr) const {
    const Brush& b = brushes[bi];
    float enterFrac = -1.0f, leaveFrac = 1.0f;
    const Plane* clipPlane = nullptr;
    bool getOut = false, startOut = false;
    for (const Plane& p : b.planes) {
        float dist = p.d;
        if (!isPoint) {
            vec3 ofs(p.n.x < 0 ? maxs.x : mins.x, p.n.y < 0 ? maxs.y : mins.y, p.n.z < 0 ? maxs.z : mins.z);
            dist -= dot(p.n, ofs);
        }
        float d1 = dot(start, p.n) - dist;
        float d2 = dot(end, p.n) - dist;
        if (d2 > 0) getOut = true;
        if (d1 > 0) startOut = true;
        if (d1 > 0 && (d2 >= kSurfEps || d2 >= d1)) return;
        if (d1 <= 0 && d2 <= 0) continue;
        if (d1 > d2) {
            float f = std::max(0.0f, (d1 - kSurfEps) / (d1 - d2));
            if (f > enterFrac) {
                enterFrac = f;
                clipPlane = &p;
            }
        } else {
            float f = std::min(1.0f, (d1 + kSurfEps) / (d1 - d2));
            if (f < leaveFrac) leaveFrac = f;
        }
    }
    if (!startOut) {
        tr.startSolid = true;
        tr.brush = bi;
        if (!getOut) {
            tr.allSolid = true;
            tr.fraction = 0;
        }
        return;
    }
    if (enterFrac < leaveFrac && enterFrac > -1.0f && enterFrac < tr.fraction && clipPlane) {
        tr.fraction = std::max(0.0f, enterFrac);
        tr.normal = clipPlane->n;
        tr.brush = bi;
    }
}

TraceResult CollisionWorld::trace(vec3 start, vec3 end, vec3 mins, vec3 maxs, int mask) const {
    TraceResult tr;
    tr.endpos = end;
    if (m_nodes.empty()) return tr;
    bool isPoint = mins == vec3(0) && maxs == vec3(0);
    vec3 dir = end - start;
    int stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& n = m_nodes[stack[--sp]];
        vec3 bmn = n.box.mn - maxs - vec3(1.0f), bmx = n.box.mx - mins + vec3(1.0f);
        float t0 = 0.0f, t1 = tr.fraction;
        bool hit = true;
        for (int a = 0; a < 3 && hit; a++) {
            if (std::fabs(dir[a]) < 1e-9f) {
                if (start[a] < bmn[a] || start[a] > bmx[a]) hit = false;
            } else {
                float inv = 1.0f / dir[a];
                float ta = (bmn[a] - start[a]) * inv, tb = (bmx[a] - start[a]) * inv;
                if (ta > tb) std::swap(ta, tb);
                t0 = std::max(t0, ta);
                t1 = std::min(t1, tb);
                if (t0 > t1) hit = false;
            }
        }
        if (!hit) continue;
        if (n.count > 0) {
            for (int i = 0; i < n.count; i++) {
                int bi = m_items[n.first + i];
                if (!(brushes[bi].flags & mask)) continue;
                clipBrush(bi, start, end, mins, maxs, isPoint, tr);
                if (tr.allSolid) {
                    tr.endpos = start;
                    return tr;
                }
            }
        } else if (sp + 2 <= 96) {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }
    tr.endpos = tr.fraction < 1.0f ? start + dir * tr.fraction : end;
    return tr;
}

bool CollisionWorld::pointSolid(vec3 p, int mask) const {
    if (m_nodes.empty()) return false;
    int stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& n = m_nodes[stack[--sp]];
        if (!n.box.contains(p)) continue;
        if (n.count > 0) {
            for (int i = 0; i < n.count; i++) {
                const Brush& b = brushes[m_items[n.first + i]];
                if (!(b.flags & mask)) continue;
                bool inside = true;
                for (const Plane& pl : b.planes)
                    if (dot(pl.n, p) - pl.d > 0) { inside = false; break; }
                if (inside) return true;
            }
        } else if (sp + 2 <= 96) {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }
    return false;
}

bool CollisionWorld::pointInsideVisible(vec3 p, int ignore) const {
    if (m_nodes.empty()) return false;
    int stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& n = m_nodes[stack[--sp]];
        if (!n.box.contains(p)) continue;
        if (n.count > 0) {
            for (int i = 0; i < n.count; i++) {
                int bi = m_items[n.first + i];
                const Brush& b = brushes[bi];
                if (bi == ignore || !(b.flags & BF_SOLID) || (b.flags & BF_NODRAW)) continue;
                bool inside = true;
                for (const Plane& pl : b.planes)
                    if (dot(pl.n, p) - pl.d > -0.01f) { inside = false; break; }
                if (inside) return true;
            }
        } else if (sp + 2 <= 96) {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }
    return false;
}

float CollisionWorld::solidThickness(vec3 start, vec3 dir, float maxDist, vec3* exitPos) const {
    // March forward in small steps until we leave solid, then refine.
    const float step = 4.0f;
    float t = 0.5f;
    while (t < maxDist) {
        vec3 p = start + dir * t;
        if (!pointSolid(p, MASK_SHOT)) {
            float lo = t - step, hi = t;
            for (int i = 0; i < 6; i++) {
                float mid = (lo + hi) * 0.5f;
                if (pointSolid(start + dir * mid, MASK_SHOT)) lo = mid;
                else hi = mid;
            }
            if (exitPos) *exitPos = start + dir * hi;
            return hi;
        }
        t += step;
    }
    return 1e9f;
}
