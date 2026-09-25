#pragma once
#include <vector>

#include "core/common.h"
#include "gfx/renderer.h"
#include "world/collision.h"

struct Particle {
    vec3 pos, vel;
    float life = 0, maxLife = 1;
    float size = 4, grow = 0;
    vec3 color{1, 1, 1};
    float alpha = 1;
    int shape = 0;
    bool additive = false, lit = false;
    float drag = 0, gravity = 0;
    float seed = 0;
    bool stretch = false;  // sparks: stretched along velocity
};

struct Decal {
    vec3 pos, normal;
    float size;
    int shape;
    vec3 color;
    float seed;
};

struct Tracer {
    vec3 a, b;
    float life, maxLife;
};

struct Shell {
    vec3 pos, vel;
    quat rot;
    vec3 angVel;
    float life;
    bool resting = false;
    float scale = 1;
};

struct Flash {
    vec3 pos, color;
    float radius, life, maxLife;
};

struct SmokeCloud {
    vec3 pos;
    float time = 0;
    float duration = 18.0f;
    float radius() const;  // current effective radius
};

class Effects {
public:
    std::vector<Particle> particles;
    std::vector<Decal> decals;
    std::vector<Tracer> tracers;
    std::vector<Shell> shells;
    std::vector<Flash> flashes;
    std::vector<SmokeCloud> smokes;

    void clear();
    void update(float dt, const CollisionWorld& w);
    void muzzleFlash(vec3 pos, vec3 dir, bool silenced);
    void impact(vec3 pos, vec3 normal, int surface, bool decal = true);
    void blood(vec3 pos, vec3 dir, bool headshot, const CollisionWorld& w);
    void tracer(vec3 a, vec3 b);
    void shell(vec3 pos, vec3 vel, float scale);
    void explosion(vec3 pos);
    void smoke(vec3 pos);
    void flashbang(vec3 pos);
    void dust(vec3 pos, float amount);
    void addDecal(vec3 pos, vec3 n, float size, int shape, vec3 color);
    void buildSprites(const Camera& cam, std::vector<SpriteVertex>& out, std::vector<SpriteVertex>& decalsOut, float time) const;
    void collectLights(std::vector<PointLight>& out) const;
    bool smokeBlocks(vec3 a, vec3 b) const;

private:
    Rng m_rng{777};
};

// Emits a camera-facing quad.
void pushBillboard(std::vector<SpriteVertex>& out, const Camera& cam, vec3 pos, float size, uint32_t color, vec4 params);
