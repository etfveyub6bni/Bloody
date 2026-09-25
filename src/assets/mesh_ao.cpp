// Per-vertex ambient occlusion for procedural models (stored in the vertex color alpha).
#include <algorithm>
#include <cstring>
#include <map>
#include <thread>

#include "assets/meshbuilder.h"

namespace {

struct Node {
    vec3 mn, mx;
    int left = -1, first = 0, count = 0;  // leaf when count > 0
};

struct TriBvh {
    const std::vector<ModelVertex>* v = nullptr;
    const std::vector<uint32_t>* idx = nullptr;
    std::vector<int> tris;
    std::vector<Node> nodes;
    std::vector<vec3> cent;

    void build(const std::vector<ModelVertex>& verts, const std::vector<uint32_t>& ind) {
        v = &verts;
        idx = &ind;
        int n = (int)ind.size() / 3;
        tris.resize(n);
        cent.resize(n);
        for (int i = 0; i < n; i++) {
            tris[i] = i;
            cent[i] = (P(i, 0) + P(i, 1) + P(i, 2)) / 3.0f;
        }
        nodes.reserve(n * 2);
        nodes.push_back(Node());
        split(0, 0, n);
    }
    vec3 P(int t, int k) const { return (*v)[(*idx)[t * 3 + k]].pos; }
    int bone(int t) const { return (int)((*v)[(*idx)[t * 3]].mat >> 24); }

    void split(int ni, int first, int count) {
        vec3 mn(1e30f), mx(-1e30f), cmn(1e30f), cmx(-1e30f);
        for (int i = first; i < first + count; i++) {
            for (int k = 0; k < 3; k++) {
                mn = vmin(mn, P(tris[i], k));
                mx = vmax(mx, P(tris[i], k));
            }
            cmn = vmin(cmn, cent[tris[i]]);
            cmx = vmax(cmx, cent[tris[i]]);
        }
        nodes[ni].mn = mn;
        nodes[ni].mx = mx;
        vec3 ext = cmx - cmn;
        int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
        if (count <= 4 || ext[axis] < 1e-6f) {
            nodes[ni].first = first;
            nodes[ni].count = count;
            return;
        }
        int mid = first + count / 2;
        std::nth_element(tris.begin() + first, tris.begin() + mid, tris.begin() + first + count,
                         [&](int a, int b) { return cent[a][axis] < cent[b][axis]; });
        int l = (int)nodes.size();
        nodes.push_back(Node());
        nodes.push_back(Node());
        nodes[ni].left = l;
        split(l, first, mid - first);
        split(l + 1, mid, first + count - mid);
    }

    static bool slab(vec3 ro, vec3 inv, vec3 mn, vec3 mx, float tmax) {
        float t0 = 0, t1 = tmax;
        for (int a = 0; a < 3; a++) {
            float ta = (mn[a] - ro[a]) * inv[a], tb = (mx[a] - ro[a]) * inv[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        return true;
    }

    // Nearest hit distance along the ray, or tmax when nothing is hit.
    float trace(vec3 ro, vec3 rd, float tmax, int onlyBone) const {
        vec3 inv(1.0f / (std::fabs(rd.x) > 1e-8f ? rd.x : 1e-8f), 1.0f / (std::fabs(rd.y) > 1e-8f ? rd.y : 1e-8f),
                 1.0f / (std::fabs(rd.z) > 1e-8f ? rd.z : 1e-8f));
        int stack[64], sp = 0;
        stack[sp++] = 0;
        float best = tmax;
        while (sp) {
            const Node& nd = nodes[stack[--sp]];
            if (!slab(ro, inv, nd.mn, nd.mx, best)) continue;
            if (nd.count) {
                for (int i = nd.first; i < nd.first + nd.count; i++) {
                    int t = tris[i];
                    if (onlyBone >= 0 && bone(t) != onlyBone) continue;
                    vec3 a = P(t, 0), e1 = P(t, 1) - a, e2 = P(t, 2) - a;
                    vec3 pv = cross(rd, e2);
                    float det = dot(e1, pv);
                    if (std::fabs(det) < 1e-9f) continue;
                    float id = 1.0f / det;
                    vec3 tv = ro - a;
                    float u = dot(tv, pv) * id;
                    if (u < 0 || u > 1) continue;
                    vec3 qv = cross(tv, e1);
                    float w = dot(rd, qv) * id;
                    if (w < 0 || u + w > 1) continue;
                    float d = dot(e2, qv) * id;
                    if (d > 1e-4f && d < best) best = d;
                }
            } else if (sp < 62) {
                stack[sp++] = nd.left;
                stack[sp++] = nd.left + 1;
            }
        }
        return best;
    }
};

uint32_t hashU(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

}  // namespace

void MeshBuilder::tessellate(float maxEdge) {
    if (maxEdge <= 0) return;
    const float max2 = maxEdge * maxEdge;
    std::map<uint64_t, uint32_t> mids;
    auto midpoint = [&](uint32_t a, uint32_t b) {
        uint64_t key = a < b ? ((uint64_t)a << 32 | b) : ((uint64_t)b << 32 | a);
        auto it = mids.find(key);
        if (it != mids.end()) return it->second;
        ModelVertex m = verts[a];
        m.pos = (verts[a].pos + verts[b].pos) * 0.5f;
        vec3 n = verts[a].normal + verts[b].normal;
        m.normal = length2(n) > 1e-12f ? normalize(n) : verts[a].normal;
        verts.push_back(m);
        uint32_t id = (uint32_t)verts.size() - 1;
        mids[key] = id;
        return id;
    };
    std::vector<uint32_t> work = idx, out;
    out.reserve(idx.size() * 2);
    for (int pass = 0; pass < 12 && !work.empty(); pass++) {
        std::vector<uint32_t> next;
        for (size_t t = 0; t + 2 < work.size(); t += 3) {
            uint32_t i[3] = {work[t], work[t + 1], work[t + 2]};
            float e[3];
            for (int k = 0; k < 3; k++) e[k] = length2(verts[i[(k + 1) % 3]].pos - verts[i[k]].pos);
            int k = e[0] >= e[1] && e[0] >= e[2] ? 0 : (e[1] >= e[2] ? 1 : 2);
            if (e[k] <= max2) {
                out.insert(out.end(), {i[0], i[1], i[2]});
                continue;
            }
            uint32_t a = i[k], b = i[(k + 1) % 3], c = i[(k + 2) % 3];
            uint32_t m = midpoint(a, b);
            next.insert(next.end(), {a, m, c, m, b, c});
        }
        work.swap(next);
    }
    out.insert(out.end(), work.begin(), work.end());
    idx.swap(out);
}

void MeshBuilder::bakeAO(float maxDist, int rays, float maxEdge, bool sameBoneOnly) {
    tessellate(maxEdge);
    TriBvh bvh;
    bvh.build(verts, idx);
    // Cosine-weighted hemisphere directions (z up), rotated per vertex to avoid banding.
    std::vector<vec3> dirs(rays);
    for (int i = 0; i < rays; i++) {
        float u = (i + 0.5f) / rays, phi = i * 2.39996323f;
        float r = std::sqrt(u);
        dirs[i] = vec3(r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u)));
    }
    std::vector<float> ao(verts.size(), 1.0f);
    auto work = [&](size_t begin, size_t end) {
        for (size_t vi = begin; vi < end; vi++) {
            const ModelVertex& v = verts[vi];
            vec3 n = v.normal;
            vec3 t = anyPerp(n), b = cross(n, t);
            float rot = (hashU((uint32_t)vi) & 0xFFFF) / 65535.0f * kTwoPi;
            float cr = std::cos(rot), sr = std::sin(rot);
            vec3 ro = v.pos + n * 0.004f;
            int onlyBone = sameBoneOnly ? (int)(v.mat >> 24) : -1;
            float occ = 0;
            for (int i = 0; i < rays; i++) {
                vec3 d = dirs[i];
                float x = d.x * cr - d.y * sr, y = d.x * sr + d.y * cr;
                vec3 w = t * x + b * y + n * d.z;
                float hit = bvh.trace(ro, w, maxDist, onlyBone);
                if (hit < maxDist) {
                    float k = 1.0f - hit / maxDist;
                    occ += k * k * (3.0f - 2.0f * k) * 0.35f + 0.65f;
                }
            }
            ao[vi] = 1.0f - occ / rays;
        }
    };
    unsigned nt = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> th;
    size_t chunk = (verts.size() + nt - 1) / nt;
    for (unsigned k = 0; k < nt; k++) {
        size_t b0 = k * chunk, b1 = std::min(verts.size(), b0 + chunk);
        if (b0 < b1) th.emplace_back(work, b0, b1);
    }
    for (auto& t : th) t.join();
    for (size_t i = 0; i < verts.size(); i++) {
        uint32_t a = (uint32_t)(saturate(ao[i]) * 255.0f + 0.5f);
        verts[i].color = (verts[i].color & 0x00FFFFFFu) | (a << 24);
    }
}
