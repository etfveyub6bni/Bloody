// CPU mesh construction toolkit for procedural models (weapons, hands, agents, props).
#pragma once
#include <vector>

#include "core/common.h"
#include "gfx/renderer.h"

struct Mat {
    vec3 color{0.5f, 0.5f, 0.5f};  // sRGB albedo
    float rough = 0.5f;
    float metal = 0.0f;
    int pattern = 0;  // see model shader: 1 wood, 2 fabric, 3 camo, 4 grip, 5 worn metal, 6 leather, 7 skin, 8 emissive, 9 glass
};

using Ring = std::vector<vec3>;

class MeshBuilder {
public:
    std::vector<ModelVertex> verts;
    std::vector<uint32_t> idx;
    int bone = 0;
    Mat mat;

    void setXform(const mat4& m);
    void resetXform() { setXform(mat4()); }
    const mat4& xform() const { return m_xf; }

    uint32_t vtx(vec3 p, vec3 n);
    void tri(uint32_t a, uint32_t b, uint32_t c) { idx.insert(idx.end(), {a, b, c}); }
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { idx.insert(idx.end(), {a, b, c, a, c, d}); }

    // Box with chamfered edges.
    void box(vec3 center, vec3 half, float bevel = 0.0f);
    void boxMinMax(vec3 mn, vec3 mx, float bevel = 0.0f) { box((mn + mx) * 0.5f, (mx - mn) * 0.5f, bevel); }
    // Surface of revolution: profile points are (distance along axis, radius).
    void lathe(vec3 origin, vec3 axis, const std::vector<vec2>& profile, int seg, float smoothDeg = 40.0f, float startAngle = 0.0f);
    void cylinder(vec3 a, vec3 b, float ra, float rb, int seg, bool caps = true, float bevel = 0.0f);
    void capsule(vec3 a, vec3 b, float ra, float rb, int seg = 12);
    void sphere(vec3 c, vec3 radii, int seg = 16, int rings = 10);
    // Side-view outline in the XZ plane, extruded symmetrically along Y with rounded (beveled) sides.
    void extrude(const std::vector<vec2>& outlineXZ, float halfWidth, float bevel, float smoothDeg = 30.0f, float yOffset = 0.0f);
    // Connects closed rings of equal size; caps are fans around ring centroids.
    void loft(const std::vector<Ring>& rings, bool capStart = true, bool capEnd = true);

    void append(const MeshBuilder& o);
    GpuMesh upload(Renderer& r) const { return r.createModelMesh(verts, idx); }

private:
    mat4 m_xf, m_xfN;
};

// Ring helpers for loft(). u/v are the in-plane axes.
Ring ringEllipse(vec3 c, vec3 u, vec3 v, float ru, float rv, int n);
Ring ringRoundRect(vec3 c, vec3 u, vec3 v, float hu, float hv, float r, int cornerSeg = 3);
