#include "game/agent.h"

namespace {

struct AgentStyle {
    Mat skin, shirt, pants, vest, pouch, glove, boot, helmet, head;
    bool ct;
};

const vec3 kX(1, 0, 0), kY(0, 1, 0), kZ(0, 0, 1);

// Joint layout of the neutral pose (model space).
const vec3 kPelvisPos(0, 0, 38), kSpineOff(0, 0, 6), kChestOff(0, 0, 8), kNeckOff(0, 0, 10), kHeadOff(0, 0, 4);
const float kThigh = 17.0f, kCalf = 16.5f, kUArm = 10.5f, kFArm = 9.8f;
const vec3 kHipOff(0, 4.2f, -2.0f);        // from pelvis, y mirrored for right
const vec3 kShoulderOff(0.3f, 8.0f, 7.2f);  // from chest

void buildAgent(MeshBuilder& m, const AgentStyle& s) {
    // Pelvis (pants + belt).
    m.bone = AG_PELVIS;
    m.mat = s.pants;
    m.box({0, 0, -2.0f}, {4.8f, 7.0f, 4.6f}, 2.2f);
    m.mat = s.pouch;
    m.box({0, 0, 2.2f}, {5.2f, 7.3f, 1.0f}, 0.5f);
    m.box({0.8f, -6.8f, -1.0f}, {2.2f, 1.2f, 2.6f}, 0.6f);
    // Abdomen.
    m.bone = AG_SPINE;
    m.mat = s.shirt;
    {
        std::vector<Ring> r;
        r.push_back(ringEllipse({0.2f, 0, -1.5f}, kX, kY, 4.8f, 6.8f, 16));
        r.push_back(ringEllipse({0.3f, 0, 3.0f}, kX, kY, 5.0f, 7.0f, 16));
        r.push_back(ringEllipse({0.3f, 0, 7.0f}, kX, kY, 5.2f, 7.4f, 16));
        m.loft(r);
    }
    // Chest with vest / chest rig.
    m.bone = AG_CHEST;
    m.mat = s.shirt;
    m.box({0.2f, 0, 4.0f}, {5.0f, 7.9f, 5.2f}, 2.6f);
    m.sphere({0.2f, 7.6f, 6.6f}, {3.1f, 3.0f, 3.0f}, 12, 8);
    m.sphere({0.2f, -7.6f, 6.6f}, {3.1f, 3.0f, 3.0f}, 12, 8);
    m.mat = s.vest;
    m.box({0.8f, 0, 3.2f}, {5.3f, 7.2f, 5.6f}, 1.4f);
    m.mat = s.pouch;
    for (int i = 0; i < 3; i++) m.box({5.9f, -3.6f + i * 3.6f, 1.4f}, {1.0f, 1.5f, 2.2f}, 0.4f);
    if (s.ct) {
        m.box({6.1f, 0, 6.0f}, {0.7f, 4.5f, 1.4f}, 0.4f);
        m.box({-5.6f, 0, 4.0f}, {1.4f, 5.5f, 5.0f}, 0.8f);
    } else {
        m.box({-5.0f, 0, 3.0f}, {1.0f, 4.0f, 4.0f}, 0.6f);
    }
    // Neck and head.
    m.bone = AG_NECK;
    m.mat = s.head;
    m.cylinder({0, 0, -1.0f}, {0.3f, 0, 4.2f}, 2.4f, 2.2f, 12);
    m.bone = AG_HEAD;
    m.mat = s.head;
    m.sphere({0.5f, 0, 3.6f}, {4.2f, 3.6f, 4.7f}, 16, 12);
    if (s.ct) {
        m.mat = s.skin;
        m.box({3.6f, 0, 2.6f}, {0.9f, 2.6f, 2.2f}, 0.8f);
        m.mat = s.helmet;
        m.lathe({0.3f, 0, 4.6f}, kZ, {{0, 4.9f}, {1.6f, 4.8f}, {3.2f, 4.0f}, {4.4f, 2.4f}, {5.0f, 0}}, 16);
        m.mat = {{0.08f, 0.08f, 0.09f}, 0.25f, 0.2f, 9};
        m.box({4.1f, 0, 5.3f}, {0.6f, 3.1f, 0.8f}, 0.4f);
    } else {
        m.mat = {{0.05f, 0.05f, 0.05f}, 0.4f, 0.0f, 0};
        m.box({3.95f, 0, 4.4f}, {0.5f, 2.4f, 0.55f}, 0.3f);
        m.mat = s.skin;
        m.box({4.0f, 0, 4.4f}, {0.35f, 2.0f, 0.38f}, 0.2f);
        m.mat = s.helmet;
        m.lathe({0.3f, 0, 5.2f}, kZ, {{0, 4.5f}, {1.4f, 4.3f}, {2.6f, 3.2f}, {3.3f, 0}}, 14);
    }
    // Limbs: X along the bone.
    for (int side = 0; side < 2; side++) {
        int ua = side ? AG_UARM_R : AG_UARM_L, fa = side ? AG_FARM_R : AG_FARM_L, ha = side ? AG_HAND_R : AG_HAND_L;
        int th = side ? AG_THIGH_R : AG_THIGH_L, ca = side ? AG_CALF_R : AG_CALF_L, fo = side ? AG_FOOT_R : AG_FOOT_L;
        m.bone = ua;
        m.mat = s.shirt;
        m.capsule({0, 0, 0}, {kUArm, 0, 0}, 2.7f, 2.3f, 12);
        m.bone = fa;
        m.capsule({0, 0, 0}, {kFArm - 1.0f, 0, 0}, 2.25f, 1.85f, 12);
        m.bone = ha;
        m.mat = s.glove;
        m.box({1.8f, 0, -0.2f}, {1.9f, 1.2f, 1.45f}, 0.8f);
        m.bone = th;
        m.mat = s.pants;
        m.capsule({0, 0, 0}, {kThigh, 0, 0}, 3.9f, 3.1f, 12);
        m.mat = s.pouch;
        m.box({8.0f, side ? -3.3f : 3.3f, 0.8f}, {2.6f, 0.9f, 2.0f}, 0.5f);
        m.bone = ca;
        m.mat = s.pants;
        m.capsule({0, 0, 0}, {kCalf - 2.0f, 0, 0}, 3.0f, 2.3f, 12);
        if (s.ct) {
            m.mat = s.pouch;
            m.box({0.6f, 0, 2.6f}, {1.8f, 2.1f, 0.8f}, 0.6f);
        }
        m.bone = fo;
        m.mat = s.boot;
        m.box({2.2f, 0, -1.6f}, {4.6f, 1.9f, 1.9f}, 1.0f);
        m.cylinder({0, 0, -3.5f}, {0, 0, 1.5f}, 2.3f, 2.2f, 12);
    }
}

Xform frameAlong(vec3 origin, vec3 xdir, vec3 zHint) { return xformFromBasis(origin, xdir, zHint); }

}  // namespace

void buildAgentModel(int team, Renderer& r, AgentModel& out) {
    AgentStyle s;
    s.ct = team == 2;
    s.skin = {{0.72f, 0.54f, 0.43f}, 0.6f, 0.0f, 7};
    if (s.ct) {
        s.shirt = {{0.23f, 0.27f, 0.35f}, 0.9f, 0.0f, 3};
        s.pants = {{0.25f, 0.28f, 0.34f}, 0.9f, 0.0f, 3};
        s.vest = {{0.20f, 0.24f, 0.21f}, 0.8f, 0.0f, 2};
        s.pouch = {{0.16f, 0.19f, 0.17f}, 0.8f, 0.0f, 2};
        s.glove = {{0.09f, 0.09f, 0.1f}, 0.6f, 0.0f, 6};
        s.boot = {{0.08f, 0.08f, 0.08f}, 0.55f, 0.0f, 6};
        s.helmet = {{0.22f, 0.26f, 0.22f}, 0.6f, 0.1f, 0};
        s.head = s.skin;
        out.patternA = {0.14f, 0.16f, 0.22f};
        out.patternB = {0.35f, 0.39f, 0.47f};
    } else {
        s.shirt = {{0.52f, 0.45f, 0.33f}, 0.9f, 0.0f, 3};
        s.pants = {{0.36f, 0.33f, 0.25f}, 0.9f, 0.0f, 3};
        s.vest = {{0.30f, 0.25f, 0.17f}, 0.8f, 0.0f, 2};
        s.pouch = {{0.24f, 0.20f, 0.14f}, 0.8f, 0.0f, 2};
        s.glove = {{0.30f, 0.21f, 0.14f}, 0.6f, 0.0f, 6};
        s.boot = {{0.22f, 0.16f, 0.10f}, 0.6f, 0.0f, 6};
        s.helmet = {{0.12f, 0.12f, 0.12f}, 0.9f, 0.0f, 2};
        s.head = {{0.10f, 0.10f, 0.10f}, 0.9f, 0.0f, 2};
        out.patternA = {0.40f, 0.33f, 0.24f};
        out.patternB = {0.62f, 0.56f, 0.43f};
    }
    MeshBuilder m;
    buildAgent(m, s);
    out.mesh = m.upload(r);
}

void animateAgent(const AgentAnimInput& in, AgentPose& out) {
    float speed = length(vec2(in.velLocal.x, in.velLocal.y));
    float sp = saturate(speed / 250.0f);
    float duck = in.duck;
    float ph = in.walkPhase;
    float air = in.onGround ? 0.0f : 1.0f;

    // Torso chain with aim pitch distributed over spine/chest/head.
    float bob = -std::fabs(std::sin(ph)) * 1.6f * sp;
    vec3 pelvisPos = kPelvisPos + vec3(0, 0, lerpf(0.0f, -13.0f, duck) + bob);
    float lean = 6.0f * sp + 10.0f * duck;
    float aim = clampf(in.aimPitch, -80.0f, 80.0f);
    quat qPelvis = qaxis(kZ, std::sin(ph) * 0.08f * sp);
    out.bones[AG_PELVIS] = Xform(pelvisPos, qPelvis);
    quat qSpine = qPelvis * qaxis(-kY, (lean * 0.5f + aim * 0.25f) * kDeg) * qaxis(kZ, -std::sin(ph) * 0.1f * sp);
    out.bones[AG_SPINE] = out.bones[AG_PELVIS] * Xform(kSpineOff, qconj(qPelvis) * qSpine);
    quat qChest = qaxis(-kY, (lean * 0.5f + aim * 0.3f) * kDeg);
    out.bones[AG_CHEST] = out.bones[AG_SPINE] * Xform(kChestOff, qChest);
    out.bones[AG_NECK] = out.bones[AG_CHEST] * Xform(kNeckOff, qaxis(-kY, (aim * 0.15f - lean * 0.6f) * kDeg));
    out.bones[AG_HEAD] = out.bones[AG_NECK] * Xform(kHeadOff, qaxis(-kY, aim * 0.3f * kDeg));

    // Legs.
    float moveYaw = speed > 5.0f ? std::atan2(in.velLocal.y, in.velLocal.x) : 0.0f;
    bool backwards = std::fabs(moveYaw) > kPi * 0.6f;
    if (backwards) moveYaw = moveYaw > 0 ? moveYaw - kPi : moveYaw + kPi;
    vec3 swingAxis(-std::sin(moveYaw), std::cos(moveYaw), 0);  // lateral to movement
    for (int side = 0; side < 2; side++) {
        float s = side ? -1.0f : 1.0f;
        float phase = ph + (side ? kPi : 0.0f);
        float swing = std::sin(phase) * (0.42f * sp) * (backwards ? -1.0f : 1.0f);
        float knee = std::max(0.0f, std::sin(phase + 1.3f)) * 0.95f * sp + 0.1f;
        float flex = lerpf(0.05f, 1.25f, duck) + air * 0.45f;
        knee = lerpf(knee, 2.05f, duck) + air * 0.7f;
        vec3 hip = apply(out.bones[AG_PELVIS], vec3(kHipOff.x, kHipOff.y * s, kHipOff.z));
        quat q = qaxis(swingAxis, -swing) * qaxis(kY, -flex) * qaxis(kZ, s * 0.06f);
        vec3 legDir = rotate(q, vec3(0, 0, -1));
        vec3 fwdHint = rotate(q, kX);
        Xform thigh = frameAlong(hip, legDir, fwdHint);
        Xform calf = thigh * Xform(vec3(kThigh, 0, 0), qaxis(kY, knee));
        vec3 ankle = apply(calf, vec3(kCalf, 0, 0));
        vec3 footFwd = normalize(vec3(std::cos(s * 0.12f), std::sin(s * 0.12f), 0) + vec3(0, 0, -0.15f * std::sin(phase) * sp));
        out.bones[side ? AG_THIGH_R : AG_THIGH_L] = thigh;
        out.bones[side ? AG_CALF_R : AG_CALF_L] = calf;
        out.bones[side ? AG_FOOT_R : AG_FOOT_L] = xformFromBasis(ankle, footFwd, kZ);
    }

    // Weapon and arms.
    const Xform& chest = out.bones[AG_CHEST];
    Xform wLocal;
    int cls = in.weaponClass;
    out.hasWeapon = in.wm != nullptr;
    if (cls == WC_PISTOL) wLocal = Xform({17.0f, -2.2f, 7.8f}, quat());
    else if (cls == WC_KNIFE) wLocal = Xform({12.0f, -7.0f, 1.0f}, quatFromEuler(20, 10, 0));
    else if (cls == WC_GRENADE) wLocal = Xform({10.0f, -7.5f, 4.0f}, quatFromEuler(0, 0, 0));
    else if (cls == WC_BOMB) wLocal = Xform({11.0f, 0.0f, -3.0f}, quatFromEuler(20, 0, 0));
    else wLocal = Xform({12.5f, -4.4f, 5.6f}, quatFromEuler(0, 2, 0));
    float k = in.fireT < 0.15f ? (1.0f - in.fireT / 0.15f) : 0.0f;
    wLocal.p += vec3(-1.2f * k, 0, 0.4f * k);
    if (in.reloadT >= 0 && in.reloadDur > 0) {
        float u = saturate(in.reloadT / in.reloadDur);
        float t = bump(u, 0.0f, 0.15f, 0.8f, 1.0f);
        wLocal.p += vec3(-2.0f, 2.0f, -2.0f) * t;
        wLocal.q = qnormalize(wLocal.q * quatFromEuler(10 * t, 15 * t, 30 * t));
    }
    if (in.planting) wLocal = Xform({12.0f, 0.0f, -8.0f}, quatFromEuler(10, 0, 0));
    out.weapon = chest * wLocal;
    for (int side = 0; side < 2; side++) {
        bool left = side == 0;
        float s = left ? 1.0f : -1.0f;
        vec3 shoulder = apply(chest, vec3(kShoulderOff.x, kShoulderOff.y * s, kShoulderOff.z));
        Xform hand;
        if (in.wm && (!left || in.wm->leftOnWeapon)) {
            hand = out.weapon * (left ? in.wm->leftGrip : in.wm->rightGrip);
        } else {
            vec3 hp = shoulder + rotate(chest.q, vec3(3.0f, 2.0f * s, -19.0f));
            hand = xformFromBasis(hp, rotate(chest.q, vec3(0.3f, 0, -1)), rotate(chest.q, kX));
        }
        vec3 pole = rotate(chest.q, normalize(vec3(-0.4f, s * 1.0f, -1.0f)));
        Xform upper, fore;
        float stretch;
        solveArmIK(shoulder, hand, pole, kUArm, kFArm, upper, fore, stretch);
        out.bones[left ? AG_UARM_L : AG_UARM_R] = upper;
        out.bones[left ? AG_FARM_L : AG_FARM_R] = fore;
        out.bones[left ? AG_HAND_L : AG_HAND_R] = Xform(hand.p, fore.q);
    }
}

bool rayCapsule(vec3 ro, vec3 rd, vec3 a, vec3 b, float r, float& t) {
    vec3 ba = b - a, oa = ro - a;
    float baba = dot(ba, ba), bard = dot(ba, rd), baoa = dot(ba, oa), rdoa = dot(rd, oa), oaoa = dot(oa, oa);
    float A = baba - bard * bard, B = baba * rdoa - baoa * bard, C = baba * oaoa - baoa * baoa - r * r * baba;
    float h = B * B - A * C;
    if (h >= 0.0f && A > 1e-8f) {
        float tt = (-B - std::sqrt(h)) / A;
        float y = baoa + tt * bard;
        if (y > 0.0f && y < baba && tt > 0) { t = tt; return true; }
    }
    for (int e = 0; e < 2; e++) {
        vec3 oc = e == 0 ? oa : ro - b;
        float bb = dot(rd, oc), cc = dot(oc, oc) - r * r;
        float hh = bb * bb - cc;
        if (hh > 0.0f) {
            float tt = -bb - std::sqrt(hh);
            if (tt > 0 && (e == 1 || true)) {
                vec3 p = ro + rd * tt;
                float y = dot(p - a, ba);
                if ((e == 0 && y <= 0) || (e == 1 && y >= baba)) { t = tt; return true; }
            }
        }
    }
    return false;
}

int agentHitboxes(const AgentPose& pose, const Xform& world, Hitbox out[kMaxHitboxes]) {
    int n = 0;
    auto P = [&](int bone, vec3 local) { return apply(world, apply(pose.bones[bone], local)); };
    out[n++] = {P(AG_HEAD, {0.4f, 0, 2.0f}), P(AG_HEAD, {0.6f, 0, 5.2f}), 4.3f, HG_HEAD};
    out[n++] = {P(AG_CHEST, {0.5f, 0, 1.0f}), P(AG_CHEST, {0.5f, 0, 7.5f}), 7.2f, HG_CHEST};
    out[n++] = {P(AG_PELVIS, {0, 0, -1.0f}), P(AG_SPINE, {0.3f, 0, 6.0f}), 6.8f, HG_STOMACH};
    const int arms[4] = {AG_UARM_L, AG_FARM_L, AG_UARM_R, AG_FARM_R};
    for (int b : arms) out[n++] = {P(b, {0, 0, 0}), P(b, {b == AG_UARM_L || b == AG_UARM_R ? kUArm : kFArm, 0, 0}), 2.4f, HG_ARM};
    const int legs[4] = {AG_THIGH_L, AG_CALF_L, AG_THIGH_R, AG_CALF_R};
    for (int b : legs) out[n++] = {P(b, {0, 0, 0}), P(b, {b == AG_THIGH_L || b == AG_THIGH_R ? kThigh : kCalf, 0, 0}), b == AG_THIGH_L || b == AG_THIGH_R ? 3.8f : 3.0f, HG_LEG};
    return n;
}

// Particles: 0 pelvis, 1 chest, 2 head, 3 shL, 4 elL, 5 haL, 6 shR, 7 elR, 8 haR, 9 hipL, 10 knL, 11 ftL, 12 hipR, 13 knR, 14 ftR
void Ragdoll::init(const AgentPose& pose, const Xform& world, vec3 velocity, vec3 impulse, vec3 impulsePoint) {
    const int bones[N] = {AG_PELVIS, AG_CHEST, AG_HEAD, AG_UARM_L, AG_FARM_L, AG_HAND_L, AG_UARM_R, AG_FARM_R, AG_HAND_R,
                          AG_THIGH_L, AG_CALF_L, AG_FOOT_L, AG_THIGH_R, AG_CALF_R, AG_FOOT_R};
    const float dt = 1.0f / 128.0f;
    for (int i = 0; i < N; i++) {
        vec3 local = i == 2 ? vec3(0.4f, 0, 3.5f) : vec3(0);
        p[i] = apply(world, apply(pose.bones[bones[i]], local));
        vec3 v = velocity;
        float d = length(p[i] - impulsePoint);
        v += impulse * (1.0f / (1.0f + d * 0.08f));
        prev[i] = p[i] - v * dt;
    }
    const int pairs[][2] = {{0, 1}, {1, 2}, {1, 3}, {1, 6}, {3, 6}, {3, 4}, {4, 5}, {6, 7}, {7, 8}, {0, 9}, {0, 12}, {9, 12},
                            {9, 10}, {10, 11}, {12, 13}, {13, 14}, {3, 9}, {6, 12}, {3, 12}, {6, 9}, {2, 3}, {2, 6}, {0, 3}, {0, 6},
                            {5, 1}, {8, 1}, {11, 0}, {14, 0}};
    links.clear();
    for (auto& pr : pairs) {
        float len = length(p[pr[0]] - p[pr[1]]);
        links.push_back({pr[0], pr[1], len});
    }
    // Last four are loose "muscle" limits: allow them to shrink but not stretch.
    active = true;
    asleep = false;
    time = 0;
}

void Ragdoll::step(float dt, const CollisionWorld& w) {
    if (!active || asleep) return;
    time += dt;
    const vec3 g(0, 0, -800.0f);
    const vec3 ext(2.5f);
    float maxMove = 0;
    for (int i = 0; i < N; i++) {
        vec3 v = (p[i] - prev[i]) * 0.995f;
        vec3 np = p[i] + v + g * (dt * dt);
        prev[i] = p[i];
        TraceResult tr = w.trace(p[i], np, -ext, ext, MASK_SHOT);
        if (tr.fraction < 1.0f && !tr.startSolid) {
            np = tr.endpos;
            // Friction: kill tangential motion on contact.
            vec3 vel = np - prev[i];
            vec3 tang = vel - tr.normal * dot(vel, tr.normal);
            prev[i] = np - tang * 0.4f;
        }
        p[i] = np;
        maxMove = std::max(maxMove, length(p[i] - prev[i]));
    }
    size_t nl = links.size();
    for (int it = 0; it < 8; it++) {
        for (size_t li = 0; li < nl; li++) {
            const Link& l = links[li];
            vec3 d = p[l.b] - p[l.a];
            float len = length(d);
            if (len < 1e-4f) continue;
            bool loose = li >= nl - 4;
            if (loose && len < l.len) continue;
            float diff = (len - l.len) / len * 0.5f;
            vec3 c = d * diff;
            vec3 na = p[l.a] + c, nb = p[l.b] - c;
            if (!w.pointSolid(na, MASK_SHOT)) p[l.a] = na;
            if (!w.pointSolid(nb, MASK_SHOT)) p[l.b] = nb;
        }
    }
    if (time > 1.0f && maxMove < 0.02f) asleep = true;
    if (time > 8.0f) asleep = true;
}

void Ragdoll::toPose(AgentPose& out) const {
    vec3 pelvis = p[0], chest = p[1], head = p[2];
    vec3 up = normalize(chest - pelvis);
    vec3 left = normalize(p[3] - p[6]);
    vec3 fwd = normalize(cross(left, up));
    left = cross(up, fwd);
    quat torso = quatFromBasis(fwd, left, up);
    vec3 hipLeft = normalize(p[9] - p[12]);
    vec3 pfwd = normalize(cross(hipLeft, up));
    quat pelvisQ = quatFromBasis(pfwd, cross(up, pfwd), up);
    out.bones[AG_PELVIS] = Xform(pelvis, pelvisQ);
    out.bones[AG_SPINE] = Xform(lerp(pelvis, chest, 0.43f), torso);
    out.bones[AG_CHEST] = Xform(chest, torso);
    vec3 hup = normalize(head - chest);
    vec3 hf = normalize(fwd - hup * dot(fwd, hup));
    quat headQ = quatFromBasis(hf, cross(hup, hf), hup);
    out.bones[AG_NECK] = Xform(lerp(chest, head, 0.7f), headQ);
    out.bones[AG_HEAD] = Xform(head - rotate(headQ, vec3(0.4f, 0, 3.5f)) + rotate(headQ, vec3(0, 0, 0)), headQ);
    auto limb = [&](int bone, int a, int b) { out.bones[bone] = xformFromBasis(p[a], p[b] - p[a], fwd); };
    limb(AG_UARM_L, 3, 4);
    limb(AG_FARM_L, 4, 5);
    limb(AG_UARM_R, 6, 7);
    limb(AG_FARM_R, 7, 8);
    out.bones[AG_HAND_L] = Xform(p[5], out.bones[AG_FARM_L].q);
    out.bones[AG_HAND_R] = Xform(p[8], out.bones[AG_FARM_R].q);
    limb(AG_THIGH_L, 9, 10);
    limb(AG_CALF_L, 10, 11);
    limb(AG_THIGH_R, 12, 13);
    limb(AG_CALF_R, 13, 14);
    for (int s = 0; s < 2; s++) {
        int calf = s ? AG_CALF_R : AG_CALF_L, foot = s ? AG_FOOT_R : AG_FOOT_L;
        vec3 cx = rotate(out.bones[calf].q, kX);
        vec3 ff = normalize(fwd - cx * dot(fwd, cx));
        out.bones[foot] = xformFromBasis(p[s ? 14 : 11], ff, -cx);
    }
    out.hasWeapon = false;
}
