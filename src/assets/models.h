// Procedural viewmodel/world models: weapons, first-person arms, third-person agents.
#pragma once
#include "assets/meshbuilder.h"
#include "game/weapons.h"

struct FingerPose {
    float curl[5][3] = {};  // index, middle, ring, pinky, thumb; radians per joint
    float spread[5] = {};
    float thumbTwist = 0;
    static FingerPose lerp(const FingerPose& a, const FingerPose& b, float t);
};

enum WeaponBone { WB_BODY = 0, WB_MAG = 1, WB_BOLT = 2, WB_EXTRA = 3, WB_COUNT = 4 };

struct WeaponModel {
    GpuMesh mesh;
    int id = 0;
    Xform rightGrip;           // right wrist frame in weapon space
    Xform leftGrip;            // left wrist frame in weapon space
    Xform leftMagGrip;         // left wrist frame when holding the magazine (weapon rest space)
    Xform rightBoltGrip;       // right wrist on the bolt handle (AWP)
    bool leftOnWeapon = true;  // two-handed hold
    FingerPose rightPose, leftPose, leftMagPose;
    vec3 muzzle;
    vec3 eject, ejectDir;
    vec3 magPivot;
    vec3 magDrop{0, 0, -1};    // direction the magazine leaves the well
    vec3 boltDir{-1, 0, 0};
    float boltTravel = 0;
    Xform viewHold;            // placement in view space (Source view axes)
    // Rig stance for this weapon (view space): shoulders sit where the body would be for this grip.
    vec3 shoulderR{-8.0f, -8.0f, -10.0f}, shoulderL{3.0f, 6.5f, -11.0f};
    vec3 poleR{0.0f, -0.75f, -1.0f}, poleL{0.0f, 0.5f, -1.0f};
    AABB bounds;
};

enum ArmBone {
    AB_UPPER = 0, AB_FORE, AB_HAND,
    AB_FINGER0,  // index prox; each finger has 3 bones: index 3-5, middle 6-8, ring 9-11, pinky 12-14, thumb 15-17
    AB_PER_ARM = 18
};

struct ArmsModel {
    GpuMesh mesh;
    float upperLen = 12.0f, foreLen = 11.0f;
    vec3 patternA, patternB;
};

// Per-finger lengths and knuckle offsets of the right hand (hand space: X to fingers, Z dorsal, Y thumb side).
struct HandGeom {
    vec3 knuckle[5];
    float len[5][3];
    quat thumbBase;
};
const HandGeom& handGeom();

void buildWeaponModel(int id, Renderer& r, WeaponModel& out);
void buildArmsModel(int team, Renderer& r, ArmsModel& out);
FingerPose poseRelaxed();

// Computes finger bone transforms (15 entries: index..thumb, 3 each) given the wrist frame.
void poseFingers(const Xform& hand, const FingerPose& pose, bool left, Xform out[15]);
// Two-bone IK. Returns upper/fore frames; forearm roll follows the hand.
void solveArmIK(vec3 shoulder, const Xform& hand, vec3 pole, float L1, float L2, Xform& upper, Xform& fore, float& foreStretch);
