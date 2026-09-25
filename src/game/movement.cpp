#include "game/movement.h"

namespace {

TraceResult hull(const CollisionWorld& w, vec3 a, vec3 b, bool ducked) {
    return w.trace(a, b, hullMins(), hullMaxs(ducked), MASK_PLAYER);
}

vec3 clipVelocity(vec3 in, vec3 n, float overbounce) {
    vec3 out = in - n * (dot(in, n) * overbounce);
    float adjust = dot(out, n);
    if (adjust < 0) out -= n * adjust;
    return out;
}

void categorize(MoveState& s, const CollisionWorld& w) {
    if (s.velocity.z > 140.0f) {
        s.onGround = false;
        return;
    }
    TraceResult tr = hull(w, s.origin, s.origin - vec3(0, 0, 2), s.ducked);
    s.onGround = tr.fraction < 1.0f && tr.normal.z >= 0.7f && !tr.allSolid;
    if (s.onGround && !tr.startSolid && tr.fraction > 0) s.origin = tr.endpos;
}

void friction(MoveState& s, float dt) {
    float speed = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y);
    if (speed < 0.1f) {
        s.velocity.x = s.velocity.y = 0;
        return;
    }
    float control = speed < mv::kStopSpeed ? mv::kStopSpeed : speed;
    float ns = std::max(0.0f, speed - control * mv::kFriction * dt) / speed;
    s.velocity.x *= ns;
    s.velocity.y *= ns;
}

void accelerate(vec3& vel, vec3 wishdir, float wishspeed, float accel, float dt) {
    float add = wishspeed - dot(vel, wishdir);
    if (add <= 0) return;
    float acc = std::min(accel * dt * wishspeed, add);
    vel += wishdir * acc;
}

void airAccelerate(vec3& vel, vec3 wishdir, float wishspeed, float dt) {
    float capped = std::min(wishspeed, mv::kAirWishCap);
    float add = capped - dot(vel, wishdir);
    if (add <= 0) return;
    float acc = std::min(mv::kAirAccelerate * wishspeed * dt, add);
    vel += wishdir * acc;
}

void flyMove(MoveState& s, float dt, const CollisionWorld& w) {
    vec3 planes[5];
    int numPlanes = 0;
    vec3 original = s.velocity, primal = s.velocity;
    float timeLeft = dt, allFraction = 0;
    for (int bump = 0; bump < 4; bump++) {
        if (length2(s.velocity) < 1e-6f) break;
        TraceResult tr = hull(w, s.origin, s.origin + s.velocity * timeLeft, s.ducked);
        allFraction += tr.fraction;
        if (tr.allSolid) {
            s.velocity = vec3(0);
            return;
        }
        if (tr.fraction > 0) {
            s.origin = tr.endpos;
            original = s.velocity;
            numPlanes = 0;
        }
        if (tr.fraction >= 1.0f) break;
        timeLeft -= timeLeft * tr.fraction;
        if (numPlanes >= 5) {
            s.velocity = vec3(0);
            break;
        }
        planes[numPlanes++] = tr.normal;
        if (numPlanes == 1 && !s.onGround) {
            s.velocity = clipVelocity(original, planes[0], 1.0f);
            original = s.velocity;
        } else {
            int i;
            for (i = 0; i < numPlanes; i++) {
                s.velocity = clipVelocity(original, planes[i], 1.0f);
                int j;
                for (j = 0; j < numPlanes; j++)
                    if (j != i && dot(s.velocity, planes[j]) < 0) break;
                if (j == numPlanes) break;
            }
            if (i == numPlanes) {
                if (numPlanes != 2) {
                    s.velocity = vec3(0);
                    break;
                }
                vec3 dir = normalize(cross(planes[0], planes[1]));
                s.velocity = dir * dot(dir, s.velocity);
            }
            if (dot(s.velocity, primal) <= 0) {
                s.velocity = vec3(0);
                break;
            }
        }
    }
    if (allFraction == 0) s.velocity = vec3(0);
}

void stayOnGround(MoveState& s, const CollisionWorld& w) {
    TraceResult up = hull(w, s.origin, s.origin + vec3(0, 0, 2), s.ducked);
    TraceResult tr = hull(w, up.endpos, s.origin - vec3(0, 0, mv::kStepSize), s.ducked);
    if (tr.fraction > 0 && tr.fraction < 1.0f && !tr.startSolid && tr.normal.z >= 0.7f)
        if (std::fabs(s.origin.z - tr.endpos.z) > 0.05f) s.origin = tr.endpos;
}

void stepMove(MoveState& s, float dt, const CollisionWorld& w) {
    vec3 startO = s.origin, startV = s.velocity;
    flyMove(s, dt, w);
    vec3 downO = s.origin, downV = s.velocity;
    s.origin = startO;
    s.velocity = startV;
    TraceResult tr = hull(w, startO, startO + vec3(0, 0, mv::kStepSize), s.ducked);
    if (!tr.startSolid && !tr.allSolid) s.origin = tr.endpos;
    flyMove(s, dt, w);
    tr = hull(w, s.origin, s.origin - vec3(0, 0, mv::kStepSize), s.ducked);
    if (!tr.startSolid && !tr.allSolid) s.origin = tr.endpos;
    if (tr.fraction < 1.0f && tr.normal.z < 0.7f) {
        s.origin = downO;
        s.velocity = downV;
        return;
    }
    float downDist = (downO.x - startO.x) * (downO.x - startO.x) + (downO.y - startO.y) * (downO.y - startO.y);
    float upDist = (s.origin.x - startO.x) * (s.origin.x - startO.x) + (s.origin.y - startO.y) * (s.origin.y - startO.y);
    if (downDist > upDist) {
        s.origin = downO;
        s.velocity = downV;
    } else {
        s.velocity.z = downV.z;
    }
}

void walkMove(MoveState& s, float dt, const CollisionWorld& w) {
    if (s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y < 1.0f) {
        s.velocity.x = s.velocity.y = 0;
        return;
    }
    vec3 dest = s.origin + vec3(s.velocity.x, s.velocity.y, 0) * dt;
    TraceResult tr = hull(w, s.origin, dest, s.ducked);
    if (tr.fraction >= 1.0f) s.origin = tr.endpos;
    else stepMove(s, dt, w);
    stayOnGround(s, w);
}

void handleDuck(MoveState& s, bool duck, float dt, const CollisionWorld& w) {
    if (duck) {
        if (!s.ducked) {
            if (s.onGround) {
                s.ducked = true;
            } else {
                vec3 raised = s.origin + vec3(0, 0, mv::kStandHeight - mv::kDuckHeight);
                if (!hull(w, raised, raised, true).startSolid) s.origin = raised;
                s.ducked = true;
                s.duckAmount = 1.0f;
            }
        }
        s.duckAmount = std::min(1.0f, s.duckAmount + dt * 7.0f);
    } else {
        if (s.ducked) {
            if (s.onGround) {
                if (!hull(w, s.origin, s.origin, false).startSolid) s.ducked = false;
            } else {
                vec3 lowered = s.origin - vec3(0, 0, mv::kStandHeight - mv::kDuckHeight);
                if (!hull(w, lowered, lowered, false).startSolid) {
                    s.origin = lowered;
                    s.ducked = false;
                    s.duckAmount = 0.0f;
                } else if (!hull(w, s.origin, s.origin, false).startSolid) {
                    s.ducked = false;
                }
            }
        }
        if (!s.ducked) s.duckAmount = std::max(0.0f, s.duckAmount - dt * 7.0f);
    }
}

}  // namespace

void playerMove(MoveState& s, const MoveInput& in, float dt, const CollisionWorld& w) {
    s.justJumped = s.justLanded = false;
    handleDuck(s, in.duck, dt, w);
    categorize(s, w);

    vec3 fwd(std::cos(in.yaw * kDeg), std::sin(in.yaw * kDeg), 0);
    vec3 right(fwd.y, -fwd.x, 0);
    vec3 wish = in.frozen ? vec3(0) : fwd * in.forward + right * in.side;
    float wl = length(wish);
    vec3 wishdir = wl > 1e-4f ? wish / wl : vec3(0);
    float maxs = in.maxSpeed;
    if (s.ducked && s.onGround) maxs *= mv::kDuckSpeedMul;
    else if (in.walk) maxs *= mv::kWalkSpeedMul;
    float wishspeed = maxs * std::min(1.0f, wl);

    if (in.jump && !in.frozen) {
        if (s.onGround && !s.jumpHeld) {
            s.velocity.z = mv::kJumpImpulse;
            s.onGround = false;
            s.justJumped = true;
        }
        s.jumpHeld = true;
    } else {
        s.jumpHeld = false;
    }

    float fallSpeed = -s.velocity.z;
    bool wasGround = s.onGround;
    vec3 before = s.origin;
    if (s.onGround) {
        s.velocity.z = 0;
        friction(s, dt);
        accelerate(s.velocity, wishdir, wishspeed, mv::kAccelerate, dt);
        float sp = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y);
        if (sp > maxs && sp > 0) {
            s.velocity.x *= maxs / sp;
            s.velocity.y *= maxs / sp;
        }
        walkMove(s, dt, w);
    } else {
        airAccelerate(s.velocity, wishdir, wishspeed, dt);
        s.velocity.z -= mv::kGravity * dt * 0.5f;
        flyMove(s, dt, w);
        s.velocity.z -= mv::kGravity * dt * 0.5f;
    }
    categorize(s, w);
    if (s.onGround) {
        if (!wasGround) {
            s.justLanded = true;
            s.landSpeed = std::max(fallSpeed, 0.0f);
        }
        s.velocity.z = 0;
        s.stepDistance += length(vec2(s.origin.x - before.x, s.origin.y - before.y));
    }
}
