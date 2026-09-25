// Convex brush world with BVH acceleration and Quake/Source-style swept box traces.
#pragma once
#include <vector>

#include "core/math.h"

enum BrushFlags : int {
    BF_SOLID = 1,       // blocks players, bullets and sight
    BF_PLAYERCLIP = 2,  // blocks players only
    BF_NODRAW = 4,      // never rendered
};
enum TraceMask : int {
    MASK_SHOT = BF_SOLID,
    MASK_PLAYER = BF_SOLID | BF_PLAYERCLIP,
};

struct Brush {
    std::vector<Plane> planes;  // outward normals; point is inside when dot(n,p) <= d for all planes
    int numFacePlanes = 0;      // planes after this index are collision bevels only
    AABB bounds;
    int material = 0;
    uint32_t tint = 0xFFFFFFFFu;
    int flags = BF_SOLID;
    int surface = 0;
    bool fitUV = false;         // texture stretched once per face (crates)
    float uvScale = 1.0f;       // 1.0 = one texture repeat per 128 units
    float penetrationScale = 1; // <1 makes bullets pass through more easily (wood)
};

Brush makeBoxBrush(vec3 mn, vec3 mx);
// Ramp rising toward riseDir: 0 = +X, 1 = -X, 2 = +Y, 3 = -Y.
Brush makeWedgeBrush(vec3 mn, vec3 mx, int riseDir);
Brush makePrismBrush(vec2 center, float radius, int sides, float z0, float z1, float rotDeg);
Brush makeOrientedBoxBrush(vec3 center, vec3 half, float yawDeg);
// Computes bounds from the actual polyhedron and appends axial bevel planes (needed for box traces).
void finalizeBrush(Brush& b);
std::vector<vec3> brushFacePolygon(const Brush& b, int planeIndex);

struct TraceResult {
    float fraction = 1.0f;
    vec3 endpos;
    vec3 normal;
    bool startSolid = false;
    bool allSolid = false;
    int brush = -1;
};

class CollisionWorld {
public:
    std::vector<Brush> brushes;

    void build();
    void clear();
    TraceResult trace(vec3 start, vec3 end, vec3 mins, vec3 maxs, int mask) const;
    TraceResult traceRay(vec3 start, vec3 end, int mask) const { return trace(start, end, vec3(0), vec3(0), mask); }
    bool pointSolid(vec3 p, int mask) const;
    // True if p lies inside a rendered solid brush other than `ignore` (used to cull hidden faces).
    bool pointInsideVisible(vec3 p, int ignore) const;
    // Distance a ray travels inside solid starting at `start` (already inside or on the surface).
    float solidThickness(vec3 start, vec3 dir, float maxDist, vec3* exitPos) const;

private:
    struct Node {
        AABB box;
        int left = -1, right = -1, first = 0, count = 0;
    };
    int buildNode(int first, int count, int depth);
    void clipBrush(int bi, vec3 start, vec3 end, vec3 mins, vec3 maxs, bool isPoint, TraceResult& tr) const;
    std::vector<Node> m_nodes;
    std::vector<int> m_items;
};
