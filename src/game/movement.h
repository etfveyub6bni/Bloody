// Source/CS2-style player movement: ground friction+acceleration, air strafing, stairs, duck-jumps.
#pragma once
#include "world/collision.h"

namespace mv {
constexpr float kGravity = 800.0f;
constexpr float kJumpImpulse = 301.993377f;
constexpr float kAccelerate = 5.5f;
constexpr float kAirAccelerate = 12.0f;
constexpr float kFriction = 5.2f;
constexpr float kStopSpeed = 80.0f;
constexpr float kAirWishCap = 30.0f;
constexpr float kStepSize = 18.0f;
constexpr float kHalfWidth = 16.0f;
constexpr float kStandHeight = 72.0f;
constexpr float kDuckHeight = 54.0f;
constexpr float kEyeStand = 64.0f;
constexpr float kEyeDuck = 46.0f;
constexpr float kDuckSpeedMul = 0.34f;
constexpr float kWalkSpeedMul = 0.52f;
}  // namespace mv

struct MoveState {
    vec3 origin;
    vec3 velocity;
    bool onGround = false;
    bool ducked = false;
    float duckAmount = 0;  // smoothed 0..1 for eye height
    bool jumpHeld = false;
    // Events of the last move.
    bool justJumped = false;
    bool justLanded = false;
    float landSpeed = 0;
    float stepDistance = 0;  // accumulated walked distance (footsteps)
};

struct MoveInput {
    float forward = 0, side = 0;  // side > 0 = right
    bool jump = false, duck = false, walk = false;
    float yaw = 0;
    float maxSpeed = 250;
    bool frozen = false;
};

void playerMove(MoveState& s, const MoveInput& in, float dt, const CollisionWorld& w);
inline vec3 hullMins() { return {-mv::kHalfWidth, -mv::kHalfWidth, 0}; }
inline vec3 hullMaxs(bool ducked) { return {mv::kHalfWidth, mv::kHalfWidth, ducked ? mv::kDuckHeight : mv::kStandHeight}; }
inline float eyeHeight(const MoveState& s) {
    // Ducking in the air raises the origin by the hull difference, so the eye stays continuous.
    return lerpf(mv::kEyeStand, mv::kEyeDuck, s.duckAmount) + (s.ducked && !s.onGround ? 0.0f : 0.0f);
}
