// Third-person agents: procedural model, skeleton animation, hitboxes and a Verlet ragdoll.
#pragma once
#include "assets/models.h"
#include "world/collision.h"

enum AgentBone {
    AG_PELVIS = 0, AG_SPINE, AG_CHEST, AG_NECK, AG_HEAD,
    AG_UARM_L, AG_FARM_L, AG_HAND_L, AG_UARM_R, AG_FARM_R, AG_HAND_R,
    AG_THIGH_L, AG_CALF_L, AG_FOOT_L, AG_THIGH_R, AG_CALF_R, AG_FOOT_R,
    // 15 finger bones per hand in poseFingers() order (index, middle, ring, pinky, thumb; 3 phalanges each).
    AG_FINGERS_L,
    AG_FINGERS_R = AG_FINGERS_L + 15,
    AG_BONES = AG_FINGERS_R + 15  // 47: must stay within the model shader's bone palette (48)
};

struct AgentModel {
    GpuMesh mesh;
    vec3 patternA, patternB;
};
void buildAgentModel(int team, Renderer& r, AgentModel& out);

struct AgentPose {
    Xform bones[AG_BONES];  // model space: feet at origin, facing +X
    Xform weapon;
    bool hasWeapon = false;
    // Gait state carried between frames by animateAgent (a fresh pose starts a new cycle).
    float gaitPhase = 0, gaitTime = -1;
};

struct AgentAnimInput {
    float time = 0;
    vec3 velLocal;      // velocity in model space (x forward, y left)
    bool onGround = true;
    float duck = 0;
    float aimPitch = 0;  // degrees, positive up
    float walkPhase = 0;
    const WeaponModel* wm = nullptr;
    int weaponClass = -1;
    float fireT = 10;
    float reloadT = -1, reloadDur = 2;
    bool planting = false;
};
void animateAgent(const AgentAnimInput& in, AgentPose& out);

enum HitGroup { HG_HEAD = 0, HG_CHEST, HG_STOMACH, HG_ARM, HG_LEG, HG_COUNT };
struct Hitbox {
    vec3 a, b;
    float r;
    int group;
};
constexpr int kMaxHitboxes = 12;
int agentHitboxes(const AgentPose& pose, const Xform& world, Hitbox out[kMaxHitboxes]);
bool rayCapsule(vec3 ro, vec3 rd, vec3 a, vec3 b, float r, float& t);

struct Ragdoll {
    static constexpr int N = 15;
    vec3 p[N], prev[N];
    struct Link { int a, b; float len; };
    std::vector<Link> links;
    bool active = false;
    float time = 0;
    bool asleep = false;
    void init(const AgentPose& pose, const Xform& world, vec3 velocity, vec3 impulse, vec3 impulsePoint);
    void step(float dt, const CollisionWorld& w);
    void toPose(AgentPose& out) const;  // world-space bones
};
