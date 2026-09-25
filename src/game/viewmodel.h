// First-person viewmodel animation: weapon placement, procedural animations, hand targets.
#pragma once
#include "assets/models.h"

struct ViewmodelParams {
    int weapon = W_KNIFE;
    float time = 0, dt = 0;
    float deployT = 10;
    float deployDur = 1.0f;
    float fireT = 10;
    int shots = 0;
    float reloadT = -1;
    float reloadDur = 2.4f;
    float inspectT = -1;
    float attackT = -1;
    int attackKind = 0;
    float boltT = -1;
    float throwT = -1;
    bool slideLocked = false;
    float speed = 0;
    bool onGround = true;
    float crouch = 0;
    float yawDelta = 0, pitchDelta = 0;
    vec3 userOffset{0, 0, 0};  // view space: forward, left, up
    float bob = 1.0f;          // walk bob scale
};

struct ViewmodelState {
    vec3 sway;
    float bobPhase = 0, bobAmp = 0;
    float land = 0, landVel = 0;
    bool wasOnGround = true;
};

struct ViewmodelPose {
    Xform weapon;
    mat4 weaponBones[WB_COUNT];
    Xform rightHand, leftHand;
    FingerPose rightFingers, leftFingers;
    bool weaponVisible = true;
    vec3 muzzle;
};

void animateViewmodel(const WeaponModel& wm, const ViewmodelParams& p, ViewmodelState& st, ViewmodelPose& out);
// Solves both arms and writes 36 skinning matrices in world space.
void buildArmBones(const ArmsModel& arms, const WeaponModel& wm, const ViewmodelPose& pose, const Xform& eye, mat4 bones[AB_PER_ARM * 2]);
