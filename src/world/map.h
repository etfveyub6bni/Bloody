#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "gfx/renderer.h"
#include "world/collision.h"
#include "world/nav.h"

enum Team { TEAM_NONE = 0, TEAM_T = 1, TEAM_CT = 2 };

struct SpawnPoint {
    vec3 pos;
    float yaw = 0;
    int team = TEAM_NONE;
};

struct BombSite {
    AABB box;
    char letter = 'A';
};

struct MapInfo {
    std::string id, title, subtitle;
    Environment env;
    vec3 lobbyCamPos;
    float lobbyCamPitch = 0, lobbyCamYaw = 0;
    vec3 thumbPos;
    float thumbPitch = 0, thumbYaw = 0;
};

struct ProbeGrid {
    int w = 0, h = 0;
    float cell = 32;
    vec2 origin;
    std::vector<vec3> up, down;
    std::vector<float> z;
};

class GameMap {
public:
    MapInfo info;
    CollisionWorld world;
    std::vector<SpawnPoint> spawns;
    std::vector<BombSite> sites;
    AABB bounds;

    // Layout grid written by MapBuilder (drives navigation).
    int layoutW = 0, layoutH = 0;
    float layoutCell = 32;
    vec2 layoutOrigin;
    std::vector<float> layoutFloor;  // NAN where solid
    std::vector<float> layoutCeil;   // NAN where open sky

    std::vector<WorldVertex> verts;
    std::vector<uint32_t> indices;
    std::vector<WorldChunk> chunks;
    NavGrid nav;
    ProbeGrid probes;

    std::atomic<float> progress{0};

    void buildRenderGeometry();
    void bakeLighting(int raysPerVertex);
    void buildNavigation();
    void bakeProbes();
    void sampleAmbient(vec3 p, vec3& up, vec3& down) const;
    const SpawnPoint* randomSpawn(int team, Rng& rng) const;

private:
    // sx/sy seed the secondary (second-bounce) ray at high bake quality.
    vec3 traceRadiance(vec3 origin, vec3 dir, float* hitDist, int sx = 0, int sy = 0) const;
};

// Builds the collision world, layout and metadata for a map id. Returns false if unknown.
bool buildMapById(const std::string& id, GameMap& out);

// Quality of light baking for maps baked afterwards: 0 low (fewer rays), 1 normal,
// 2 high (path-traced second bounce), 3 max (second bounce and more rays).
void setLightBakeQuality(int q);

struct MapListEntry {
    const char* id;
    const char* title;
    const char* subtitle;
};
const std::vector<MapListEntry>& mapList();
