#include "assets/meshbuilder.h"

void MeshBuilder::setXform(const mat4& m) {
    m_xf = m;
    m_xfN = transpose(inverse(m));
}

uint32_t MeshBuilder::vtx(vec3 p, vec3 n) {
    ModelVertex v;
    v.pos = xformPoint(m_xf, p);
    v.normal = normalize(xformDir(m_xfN, n));
    v.color = packRGBA(mat.color.x, mat.color.y, mat.color.z, 1.0f);
    v.mat = (uint32_t)(saturate(mat.rough) * 255.0f + 0.5f) | ((uint32_t)(saturate(mat.metal) * 255.0f + 0.5f) << 8) |
            ((uint32_t)(mat.pattern & 255) << 16) | ((uint32_t)(bone & 255) << 24);
    verts.push_back(v);
    return (uint32_t)verts.size() - 1;
}

void MeshBuilder::box(vec3 c, vec3 h, float b) {
    b = std::min(b, std::min(h.x, std::min(h.y, h.z)) * 0.95f);
    const vec3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    auto P = [&](int a, float sa, int u, float su, int v, float sv, int onFace) {
        // onFace: 0 = point lies on face a, 1 = on face u, 2 = on face v (the other two coordinates are inset)
        vec3 p;
        float ia = h[a] - b, iu = h[u] - b, iv = h[v] - b;
        p[a] = sa * (onFace == 0 ? h[a] : ia);
        p[u] = su * (onFace == 1 ? h[u] : iu);
        p[v] = sv * (onFace == 2 ? h[v] : iv);
        return c + p;
    };
    // Faces.
    for (int a = 0; a < 3; a++) {
        int u = (a + 1) % 3, v = (a + 2) % 3;
        for (float s : {-1.0f, 1.0f}) {
            vec3 n = ax[a] * s;
            uint32_t i0 = vtx(P(a, s, u, -1, v, -1, 0), n), i1 = vtx(P(a, s, u, 1, v, -1, 0), n);
            uint32_t i2 = vtx(P(a, s, u, 1, v, 1, 0), n), i3 = vtx(P(a, s, u, -1, v, 1, 0), n);
            quad(i0, i1, i2, i3);
        }
    }
    if (b <= 0.0f) return;
    // Edge chamfers.
    for (int a = 0; a < 3; a++) {
        int u = (a + 1) % 3, v = (a + 2) % 3;  // edge runs along v, between faces a and u
        for (float sa : {-1.0f, 1.0f})
            for (float su : {-1.0f, 1.0f}) {
                vec3 n = normalize(ax[a] * sa + ax[u] * su);
                uint32_t i0 = vtx(P(a, sa, u, su, v, -1, 0), n), i1 = vtx(P(a, sa, u, su, v, 1, 0), n);
                uint32_t i2 = vtx(P(a, sa, u, su, v, 1, 1), n), i3 = vtx(P(a, sa, u, su, v, -1, 1), n);
                quad(i0, i1, i2, i3);
            }
    }
    // Corners.
    for (float sx : {-1.0f, 1.0f})
        for (float sy : {-1.0f, 1.0f})
            for (float sz : {-1.0f, 1.0f}) {
                vec3 n = normalize(vec3(sx, sy, sz));
                uint32_t i0 = vtx(P(0, sx, 1, sy, 2, sz, 0), n);
                uint32_t i1 = vtx(P(0, sx, 1, sy, 2, sz, 1), n);
                uint32_t i2 = vtx(P(0, sx, 1, sy, 2, sz, 2), n);
                tri(i0, i1, i2);
            }
}

void MeshBuilder::lathe(vec3 origin, vec3 axis, const std::vector<vec2>& prof, int seg, float smoothDeg, float startAngle) {
    if (prof.size() < 2) return;
    axis = normalize(axis);
    vec3 u = anyPerp(axis), v = cross(axis, u);
    size_t np = prof.size();
    std::vector<vec2> segN(np - 1);
    for (size_t i = 0; i + 1 < np; i++) {
        vec2 d = prof[i + 1] - prof[i];
        segN[i] = normalize(vec2(-d.y, d.x));
    }
    float cosT = std::cos(smoothDeg * kDeg);
    auto normalAt = [&](size_t pi, size_t si) {
        vec2 n = segN[si];
        size_t other = (pi == si) ? (si > 0 ? si - 1 : si) : (si + 1 < segN.size() ? si + 1 : si);
        if (other != si && dot(segN[other], n) > cosT) n = normalize(n + segN[other]);
        return n;
    };
    for (size_t s = 0; s + 1 < np; s++) {
        uint32_t base = (uint32_t)verts.size();
        for (int e = 0; e < 2; e++) {
            size_t pi = s + e;
            vec2 n2 = normalAt(pi, s);
            for (int k = 0; k <= seg; k++) {
                float a = startAngle + kTwoPi * k / seg;
                vec3 radial = u * std::cos(a) + v * std::sin(a);
                vtx(origin + axis * prof[pi].x + radial * prof[pi].y, axis * n2.x + radial * n2.y);
            }
        }
        for (int k = 0; k < seg; k++) {
            uint32_t a0 = base + k, a1 = base + k + 1, b0 = base + seg + 1 + k, b1 = base + seg + 2 + k;
            quad(a0, a1, b1, b0);
        }
    }
}

void MeshBuilder::cylinder(vec3 a, vec3 b, float ra, float rb, int seg, bool caps, float bevel) {
    vec3 d = b - a;
    float len = length(d);
    if (len < 1e-5f) return;
    std::vector<vec2> p;
    if (caps) p.push_back({0, 0});
    if (caps && bevel > 0) { p.push_back({0, ra - bevel}); p.push_back({bevel, ra}); }
    else p.push_back({0, ra});
    if (caps && bevel > 0) { p.push_back({len - bevel, rb}); p.push_back({len, rb - bevel}); }
    else p.push_back({len, rb});
    if (caps) p.push_back({len, 0});
    lathe(a, d / len, p, seg, 30.0f);
}

void MeshBuilder::capsule(vec3 a, vec3 b, float ra, float rb, int seg) {
    vec3 d = b - a;
    float len = length(d);
    vec3 axis = len > 1e-5f ? d / len : vec3(1, 0, 0);
    std::vector<vec2> p;
    const int q = 5;
    for (int i = 0; i <= q; i++) {
        float t = -kPi * 0.5f + (kPi * 0.5f) * i / q;
        p.push_back({std::sin(t) * ra, std::cos(t) * ra});
    }
    for (int i = 0; i <= q; i++) {
        float t = (kPi * 0.5f) * i / q;
        p.push_back({len + std::sin(t) * rb, std::cos(t) * rb});
    }
    lathe(a, axis, p, seg, 60.0f);
}

void MeshBuilder::sphere(vec3 c, vec3 r, int seg, int rings) {
    uint32_t base = (uint32_t)verts.size();
    for (int j = 0; j <= rings; j++) {
        float th = kPi * j / rings;
        for (int i = 0; i <= seg; i++) {
            float ph = kTwoPi * i / seg;
            vec3 s(std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), std::cos(th));
            vtx(c + s * r, normalize(s / r));
        }
    }
    for (int j = 0; j < rings; j++)
        for (int i = 0; i < seg; i++) {
            uint32_t a = base + j * (seg + 1) + i, b2 = a + seg + 1;
            quad(a, a + 1, b2 + 1, b2);
        }
}

namespace {

float signedArea(const std::vector<vec2>& p) {
    float a = 0;
    for (size_t i = 0; i < p.size(); i++) a += cross2(p[i], p[(i + 1) % p.size()]);
    return a * 0.5f;
}

bool pointInTri(vec2 p, vec2 a, vec2 b, vec2 c) {
    float d1 = cross2(b - a, p - a), d2 = cross2(c - b, p - b), d3 = cross2(a - c, p - c);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// Ear clipping for a CCW simple polygon.
std::vector<int> triangulate(const std::vector<vec2>& p) {
    std::vector<int> out, rem;
    for (int i = 0; i < (int)p.size(); i++) rem.push_back(i);
    int guard = 0;
    while (rem.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (size_t i = 0; i < rem.size(); i++) {
            int ia = rem[(i + rem.size() - 1) % rem.size()], ib = rem[i], ic = rem[(i + 1) % rem.size()];
            vec2 a = p[ia], b = p[ib], c = p[ic];
            if (cross2(b - a, c - b) <= 1e-7f) continue;
            bool inside = false;
            for (int k : rem) {
                if (k == ia || k == ib || k == ic) continue;
                if (pointInTri(p[k], a, b, c)) { inside = true; break; }
            }
            if (inside) continue;
            out.insert(out.end(), {ia, ib, ic});
            rem.erase(rem.begin() + (long)i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (rem.size() == 3) out.insert(out.end(), {rem[0], rem[1], rem[2]});
    return out;
}

}  // namespace

void MeshBuilder::extrude(const std::vector<vec2>& outlineIn, float hw, float bevel, float smoothDeg, float yOff) {
    std::vector<vec2> ol = outlineIn;
    if (ol.size() < 3) return;
    if (signedArea(ol) < 0) std::reverse(ol.begin(), ol.end());
    size_t n = ol.size();
    bevel = std::min(bevel, hw * 0.9f);
    std::vector<vec2> en(n);  // outward edge normals
    for (size_t i = 0; i < n; i++) {
        vec2 d = normalize(ol[(i + 1) % n] - ol[i]);
        en[i] = vec2(d.y, -d.x);
    }
    float cosT = std::cos(smoothDeg * kDeg);
    std::vector<bool> smoothV(n);
    std::vector<vec2> vn(n);
    std::vector<vec2> inset(n);
    for (size_t i = 0; i < n; i++) {
        vec2 e0 = en[(i + n - 1) % n], e1 = en[i];
        smoothV[i] = dot(e0, e1) > cosT;
        vn[i] = normalize(e0 + e1);
        float k = std::max(dot(vn[i], e1), 0.35f);
        inset[i] = ol[i] - vn[i] * (bevel / k);
    }
    auto sideN = [&](size_t v, size_t e) { return smoothV[v] ? vn[v] : en[e]; };
    auto P = [&](vec2 q, float y) { return vec3(q.x, y + yOff, q.y); };
    float yIn = hw - bevel;
    for (size_t e = 0; e < n; e++) {
        size_t a = e, b = (e + 1) % n;
        vec2 na = sideN(a, e), nb = sideN(b, e);
        vec3 Na(na.x, 0, na.y), Nb(nb.x, 0, nb.y);
        // Main side wall.
        uint32_t i0 = vtx(P(ol[a], -yIn), Na), i1 = vtx(P(ol[b], -yIn), Nb), i2 = vtx(P(ol[b], yIn), Nb), i3 = vtx(P(ol[a], yIn), Na);
        quad(i0, i1, i2, i3);
        if (bevel > 0) {
            for (float s : {-1.0f, 1.0f}) {
                vec3 Ba = normalize(Na + vec3(0, s, 0)), Bb = normalize(Nb + vec3(0, s, 0));
                uint32_t j0 = vtx(P(ol[a], s * yIn), Ba), j1 = vtx(P(ol[b], s * yIn), Bb);
                uint32_t j2 = vtx(P(inset[b], s * hw), Bb), j3 = vtx(P(inset[a], s * hw), Ba);
                quad(j0, j1, j2, j3);
            }
        }
    }
    std::vector<vec2> cap = bevel > 0 ? inset : ol;
    if (signedArea(cap) < 0) cap = ol;
    std::vector<int> tris = triangulate(cap);
    for (float s : {-1.0f, 1.0f}) {
        uint32_t base = (uint32_t)verts.size();
        for (auto& q : cap) vtx(P(q, s * hw), vec3(0, s, 0));
        for (size_t t = 0; t + 2 < tris.size(); t += 3) tri(base + tris[t], base + tris[t + 1], base + tris[t + 2]);
    }
}

void MeshBuilder::loft(const std::vector<Ring>& rings, bool capStart, bool capEnd) {
    if (rings.size() < 2) return;
    size_t m = rings[0].size(), nr = rings.size();
    uint32_t base = (uint32_t)verts.size();
    for (size_t j = 0; j < nr; j++)
        for (size_t i = 0; i < m; i++) {
            vec3 tRing = rings[j][(i + 1) % m] - rings[j][(i + m - 1) % m];
            vec3 tLen = rings[std::min(nr - 1, j + 1)][i] - rings[j > 0 ? j - 1 : 0][i];
            vec3 n = normalize(cross(tLen, tRing));
            vec3 c(0);
            for (auto& p : rings[j]) c += p;
            c /= (float)m;
            if (dot(n, rings[j][i] - c) < 0) n = -n;
            vtx(rings[j][i], n);
        }
    for (size_t j = 0; j + 1 < nr; j++)
        for (size_t i = 0; i < m; i++) {
            uint32_t a = base + (uint32_t)(j * m + i), b = base + (uint32_t)(j * m + (i + 1) % m);
            quad(a, b, b + (uint32_t)m, a + (uint32_t)m);
        }
    auto cap = [&](size_t j, bool start) {
        vec3 c(0);
        for (auto& p : rings[j]) c += p;
        c /= (float)m;
        vec3 axis = normalize(start ? rings[0][0] - rings[1][0] : rings[nr - 1][0] - rings[nr - 2][0]);
        vec3 cn(0);
        for (size_t i = 0; i < m; i++) cn += cross(rings[j][i] - c, rings[j][(i + 1) % m] - c);
        vec3 n = normalize(cn);
        if (dot(n, axis) < 0) n = -n;
        uint32_t ci = vtx(c, n);
        uint32_t b0 = (uint32_t)verts.size();
        for (size_t i = 0; i < m; i++) vtx(rings[j][i], n);
        for (size_t i = 0; i < m; i++) tri(ci, b0 + (uint32_t)i, b0 + (uint32_t)((i + 1) % m));
    };
    if (capStart) cap(0, true);
    if (capEnd) cap(nr - 1, false);
}

void MeshBuilder::append(const MeshBuilder& o) {
    uint32_t base = (uint32_t)verts.size();
    verts.insert(verts.end(), o.verts.begin(), o.verts.end());
    for (uint32_t i : o.idx) idx.push_back(base + i);
}

Ring ringEllipse(vec3 c, vec3 u, vec3 v, float ru, float rv, int n) {
    Ring r;
    for (int i = 0; i < n; i++) {
        float a = kTwoPi * i / n;
        r.push_back(c + u * (std::cos(a) * ru) + v * (std::sin(a) * rv));
    }
    return r;
}

Ring ringRoundRect(vec3 c, vec3 u, vec3 v, float hu, float hv, float rad, int cs) {
    Ring r;
    rad = std::min(rad, std::min(hu, hv) * 0.99f);
    const vec2 corners[4] = {{hu - rad, hv - rad}, {-(hu - rad), hv - rad}, {-(hu - rad), -(hv - rad)}, {hu - rad, -(hv - rad)}};
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= cs; i++) {
            float a = kPi * 0.5f * k + (kPi * 0.5f) * i / cs;
            vec2 p = corners[k] + vec2(std::cos(a), std::sin(a)) * rad;
            r.push_back(c + u * p.x + v * p.y);
        }
    return r;
}
