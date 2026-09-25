// Grid navigation (auto-generated from the map) with A* and path smoothing.
#pragma once
#include <vector>

#include "core/common.h"

class CollisionWorld;

struct NavGrid {
    int w = 0, h = 0;
    float cell = 32;
    vec2 origin;
    std::vector<float> z;        // floor height per cell, NAN = not walkable
    std::vector<uint8_t> links;  // 8-neighbour connectivity bits

    bool walkable(int x, int y) const;
    bool toCell(vec3 p, int& cx, int& cy) const;
    vec3 center(int idx) const;
    int nearest(vec3 p, int maxRadius = 8) const;
    bool findPath(vec3 from, vec3 to, std::vector<vec3>& out) const;
    void smoothPath(std::vector<vec3>& path, const CollisionWorld& world) const;
    bool randomPoint(const AABB& area, Rng& rng, vec3& out) const;
    int countWalkable() const;
};

extern const int kNavDX[8];
extern const int kNavDY[8];
