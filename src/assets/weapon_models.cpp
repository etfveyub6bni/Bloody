#include "assets/models.h"

namespace {

const Mat kGunmetal{{0.30f, 0.30f, 0.30f}, 0.6f, 0.2f, 5};
const Mat kDarkSteel{{0.22f, 0.22f, 0.22f}, 0.58f, 0.25f, 5};
const Mat kSatin{{0.62f, 0.62f, 0.63f}, 0.30f, 0.95f, 5};
const Mat kBlade{{0.74f, 0.74f, 0.76f}, 0.22f, 1.0f, 5};
const Mat kWood{{0.50f, 0.29f, 0.16f}, 0.55f, 0.0f, 1};
const Mat kGripBrown{{0.24f, 0.15f, 0.09f}, 0.62f, 0.0f, 4};
const Mat kPolymer{{0.11f, 0.11f, 0.115f}, 0.62f, 0.0f, 4};
const Mat kPolySmooth{{0.10f, 0.10f, 0.105f}, 0.50f, 0.0f, 0};
const Mat kAnodized{{0.2f, 0.2f, 0.2f}, 0.6f, 0.15f, 5};
const Mat kAwpGreen{{0.30f, 0.38f, 0.24f}, 0.62f, 0.0f, 4};
const Mat kLens{{0.05f, 0.08f, 0.10f}, 0.05f, 0.0f, 9};
const Mat kRubber{{0.07f, 0.07f, 0.07f}, 0.85f, 0.0f, 4};

Xform gripFrame(vec3 axisPoint, vec3 fingerDir, vec3 dorsal, vec3 localAxisPoint) {
    Xform h = xformFromBasis(vec3(0), fingerDir, dorsal);
    h.p = axisPoint - rotate(h.q, localAxisPoint);
    return h;
}

FingerPose poseFrom(std::initializer_list<float> v) {
    FingerPose p;
    auto it = v.begin();
    for (int f = 0; f < 5; f++)
        for (int j = 0; j < 3; j++) p.curl[f][j] = *it++;
    for (int f = 0; f < 5 && it != v.end(); f++) p.spread[f] = *it++;
    if (it != v.end()) p.thumbTwist = *it++;
    return p;
}

// Right hand wrapped around a pistol grip, index finger on the trigger.
FingerPose poseTriggerGrip() {
    return poseFrom({0.25f, 0.95f, 0.55f,  1.35f, 1.45f, 0.85f,  1.40f, 1.45f, 0.85f,  1.40f, 1.40f, 0.85f,
                     0.55f, 0.55f, 0.45f,  0.02f, 0, -0.03f, -0.07f, 0, 0.35f});
}
// Support hand wrapped around a handguard.
FingerPose poseHandguard() {
    return poseFrom({1.05f, 1.15f, 0.75f,  1.10f, 1.20f, 0.75f,  1.12f, 1.15f, 0.75f,  1.12f, 1.10f, 0.70f,
                     0.35f, 0.25f, 0.20f,  0.05f, 0, -0.04f, -0.09f, 0, 0.2f});
}
FingerPose poseSupportPistol() {
    return poseFrom({1.15f, 1.25f, 0.8f,  1.25f, 1.30f, 0.8f,  1.30f, 1.30f, 0.8f,  1.30f, 1.25f, 0.75f,
                     0.25f, 0.15f, 0.10f,  0.0f, 0, -0.03f, -0.07f, 0, 0.1f});
}
FingerPose poseFist() {
    return poseFrom({1.45f, 1.50f, 0.95f,  1.50f, 1.55f, 0.95f,  1.52f, 1.52f, 0.95f,  1.50f, 1.48f, 0.90f,
                     0.75f, 0.70f, 0.55f,  0.0f, 0, -0.03f, -0.06f, 0, 0.5f});
}
FingerPose poseMag() {
    return poseFrom({0.85f, 0.95f, 0.6f,  0.95f, 1.05f, 0.65f,  1.0f, 1.05f, 0.65f,  1.0f, 1.0f, 0.6f,
                     0.3f, 0.3f, 0.25f,  0.05f, 0, -0.05f, -0.1f, 0, 0.2f});
}

// Banana/straight magazine outline in XZ: centerline starts at `top` pointing down and bends forward.
std::vector<vec2> magOutline(vec2 top, float R, float arcLen, float depthTop, float depthBot, float topExtra, int n = 10) {
    std::vector<vec2> front, back;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n, phi = (arcLen / R) * t;
        vec2 c(top.x + R - R * std::cos(phi), top.y - R * std::sin(phi));
        vec2 nr(std::cos(phi), std::sin(phi));
        float d = lerpf(depthTop, depthBot, t) * 0.5f;
        front.push_back(c + nr * d);
        back.push_back(c - nr * d);
    }
    std::vector<vec2> out;
    out.push_back(back[0] + vec2(0, topExtra));
    for (auto& p : back) out.push_back(p);
    for (int i = n; i >= 0; i--) out.push_back(front[i]);
    out.push_back(front[0] + vec2(0, topExtra));
    return out;
}

void railTeeth(MeshBuilder& m, float x0, float x1, float z, float hw) {
    for (float x = x0; x < x1; x += 0.55f) m.box({x + 0.14f, 0, z}, {0.14f, hw, 0.07f}, 0.02f);
}

void buildAK(MeshBuilder& m, WeaponModel& w) {
    const float bore = 1.0f;
    m.bone = WB_BODY;
    m.mat = kGunmetal;
    m.box({-1.2f, 0, 0.2f}, {5.3f, 0.57f, 1.05f}, 0.07f);
    m.box({-1.45f, 0, 1.42f}, {5.0f, 0.52f, 0.22f}, 0.18f);
    m.box({-6.55f, 0, 1.35f}, {0.15f, 0.2f, 0.12f}, 0.04f);
    m.box({5.1f, 0, 0.75f}, {1.1f, 0.5f, 0.75f}, 0.12f);
    m.box({5.2f, 0, 1.58f}, {0.65f, 0.33f, 0.12f}, 0.05f);
    m.box({5.75f, 0, 1.73f}, {0.08f, 0.2f, 0.06f}, 0.02f);
    for (float s : {-1.0f, 1.0f})
        for (float rx : {-5.2f, -3.9f, 2.6f, 3.4f}) m.sphere({rx, s * 0.575f, -0.45f}, {0.09f, 0.05f, 0.09f}, 8, 5);
    m.mat = kDarkSteel;
    m.cylinder({6.2f, 0, bore}, {21.8f, 0, bore}, 0.36f, 0.33f, 16);
    m.lathe({21.6f, 0, bore}, {1, 0, 0}, {{0, 0}, {0, 0.44f}, {0.2f, 0.48f}, {2.0f, 0.48f}, {2.3f, 0.40f}, {2.3f, 0}}, 16);
    m.cylinder({20.2f, 0, bore}, {21.1f, 0, bore}, 0.52f, 0.52f, 16, true, 0.06f);
    m.box({20.65f, 0, bore + 0.85f}, {0.22f, 0.13f, 0.55f}, 0.04f);
    for (float s : {-1.0f, 1.0f}) m.box({20.65f, s * 0.32f, bore + 0.9f}, {0.25f, 0.06f, 0.5f}, 0.02f);
    m.box({15.4f, 0, bore + 0.45f}, {0.55f, 0.42f, 0.75f}, 0.1f);
    m.cylinder({6.8f, 0, bore + 0.95f}, {15.0f, 0, bore + 0.95f}, 0.34f, 0.34f, 12);
    m.cylinder({14.8f, 0, bore - 0.5f}, {20.3f, 0, bore - 0.5f}, 0.1f, 0.1f, 8);
    m.mat = kGunmetal;
    m.box({6.45f, 0, bore - 0.05f}, {0.22f, 0.83f, 0.85f}, 0.08f);
    m.box({14.55f, 0, bore - 0.05f}, {0.2f, 0.78f, 0.78f}, 0.08f);
    m.mat = kWood;
    {
        std::vector<Ring> r;
        const float xs[] = {6.65f, 7.2f, 10.5f, 13.9f, 14.35f}, ws[] = {0.74f, 0.81f, 0.83f, 0.79f, 0.72f}, hs[] = {0.72f, 0.79f, 0.81f, 0.77f, 0.70f};
        for (int i = 0; i < 5; i++) r.push_back(ringRoundRect({xs[i], 0, bore - 0.22f}, {0, 1, 0}, {0, 0, 1}, ws[i], hs[i], 0.42f, 4));
        m.loft(r);
    }
    {
        std::vector<Ring> r;
        const float xs[] = {7.0f, 7.4f, 14.2f, 14.6f}, ws[] = {0.50f, 0.58f, 0.58f, 0.50f}, hs[] = {0.40f, 0.45f, 0.45f, 0.40f};
        for (int i = 0; i < 4; i++) r.push_back(ringRoundRect({xs[i], 0, bore + 0.98f}, {0, 1, 0}, {0, 0, 1}, ws[i], hs[i], 0.35f, 4));
        m.loft(r);
    }
    m.extrude({{-6.4f, 1.05f}, {-6.4f, -0.7f}, {-8.5f, -1.45f}, {-11.5f, -2.5f}, {-14.5f, -3.45f}, {-16.1f, -3.95f},
               {-16.1f, 0.05f}, {-12.0f, 0.42f}, {-8.5f, 0.82f}}, 0.62f, 0.24f);
    m.mat = kDarkSteel;
    m.box({-16.25f, 0, -1.95f}, {0.15f, 0.6f, 2.05f}, 0.08f);
    m.mat = kGripBrown;
    m.extrude({{-1.35f, -0.75f}, {0.05f, -0.75f}, {-0.35f, -2.2f}, {-0.9f, -3.7f}, {-1.35f, -4.7f}, {-1.6f, -5.05f},
               {-2.2f, -5.2f}, {-2.75f, -5.05f}, {-2.9f, -4.7f}, {-2.6f, -3.6f}, {-2.1f, -2.1f}, {-1.7f, -1.2f}}, 0.5f, 0.2f, 35);
    m.mat = kDarkSteel;
    m.box({0.3f, 0, -2.25f}, {0.85f, 0.14f, 0.07f}, 0.03f);
    m.box({1.1f, 0, -1.55f}, {0.08f, 0.14f, 0.75f}, 0.03f);
    m.box({0.25f, 0, -1.3f}, {0.07f, 0.1f, 0.45f}, 0.03f);
    m.box({-2.6f, -0.6f, 0.55f}, {2.6f, 0.04f, 0.16f}, 0.03f);
    // Magazine.
    m.bone = WB_MAG;
    m.mat = kGunmetal;
    m.mat.color = {0.16f, 0.16f, 0.17f};
    m.extrude(magOutline({2.4f, -0.75f}, 15.0f, 9.0f, 2.8f, 3.3f, 0.5f), 0.5f, 0.12f);
    m.box({3.85f, 0, -1.0f}, {0.2f, 0.35f, 0.18f}, 0.05f);
    // Charging handle (bolt carrier).
    m.bone = WB_BOLT;
    m.mat = kDarkSteel;
    m.cylinder({2.3f, -0.56f, 0.62f}, {2.3f, -1.05f, 0.62f}, 0.12f, 0.15f, 10);
    m.sphere({2.3f, -1.08f, 0.62f}, {0.2f, 0.16f, 0.2f}, 10, 6);

    w.muzzle = {23.9f, 0, bore};
    w.eject = {1.2f, -0.6f, 1.0f};
    w.ejectDir = normalize(vec3(0.1f, -1.0f, 0.5f));
    w.magPivot = {3.9f, 0, -0.9f};
    w.magDrop = normalize(vec3(0.35f, 0, -1.0f));
    w.boltTravel = 3.6f;
    vec3 up = normalize(vec3(1.6f, 0, 4.2f));
    w.rightGrip = gripFrame(vec3(-0.65f, 0, -0.8f) - up * 1.75f, normalize(vec3(0.93f, 0.08f, -0.36f)), {0, -1, 0.15f}, {2.55f, 0.0f, -1.25f});
    w.leftGrip = gripFrame({10.3f, 0, bore - 0.22f}, normalize(vec3(0.22f, -1.0f, 0.12f)), {0, 0.2f, -1.0f}, {2.35f, 0.1f, -1.38f});
    w.leftMagGrip = gripFrame({3.6f, 0.0f, -4.0f}, normalize(vec3(0.3f, -1.0f, -0.35f)), {-0.2f, 0.4f, -0.9f}, {2.3f, 0.2f, -1.2f});
    w.rightPose = poseTriggerGrip();
    w.leftPose = poseHandguard();
    w.leftMagPose = poseMag();
    w.viewHold = {{11.2f, -6.2f, -5.4f}, quatFromEuler(0.5f, 3.0f, 16.0f)};
}

void buildM4(MeshBuilder& m, WeaponModel& w) {
    const float bore = 1.05f;
    m.bone = WB_BODY;
    m.mat = kAnodized;
    m.box({-0.3f, 0, 1.05f}, {4.4f, 0.5f, 0.62f}, 0.1f);
    m.box({-0.4f, 0, 1.8f}, {4.2f, 0.4f, 0.14f}, 0.03f);
    railTeeth(m, -4.5f, 3.8f, 1.99f, 0.42f);
    m.box({0.6f, -0.52f, 1.0f}, {1.1f, 0.03f, 0.28f}, 0.02f);
    m.cylinder({-2.6f, -0.5f, 1.2f}, {-2.6f, -0.85f, 1.25f}, 0.22f, 0.22f, 12, true, 0.04f);
    m.box({-0.9f, 0, 0.05f}, {3.9f, 0.48f, 0.42f}, 0.08f);
    m.box({2.1f, 0, -0.9f}, {1.25f, 0.52f, 0.65f}, 0.08f);
    m.box({-0.1f, 0, -1.3f}, {0.95f, 0.14f, 0.06f}, 0.03f);
    m.box({0.15f, 0, -0.85f}, {0.06f, 0.09f, 0.35f}, 0.03f);
    m.box({-3.6f, 0, 2.35f}, {0.4f, 0.36f, 0.33f}, 0.08f);
    m.cylinder({-4.7f, 0, 0.95f}, {-10.6f, 0, 0.95f}, 0.58f, 0.58f, 16);
    m.mat = kPolymer;
    m.extrude({{-1.2f, -0.35f}, {0.0f, -0.35f}, {-0.25f, -1.6f}, {-0.75f, -3.2f}, {-1.1f, -4.1f}, {-1.7f, -4.3f},
               {-2.35f, -4.15f}, {-2.45f, -3.8f}, {-2.0f, -2.4f}, {-1.55f, -1.0f}}, 0.5f, 0.18f, 35);
    m.extrude({{-7.6f, 1.5f}, {-7.6f, -0.3f}, {-9.0f, -1.4f}, {-11.7f, -2.2f}, {-12.3f, -2.3f}, {-12.3f, 1.7f},
               {-11.2f, 1.8f}, {-8.6f, 1.7f}}, 0.7f, 0.25f);
    m.mat = kRubber;
    m.box({-12.42f, 0, -0.3f}, {0.12f, 0.68f, 2.0f}, 0.06f);
    m.mat = kPolySmooth;
    m.box({8.0f, 0, bore}, {3.6f, 0.74f, 0.74f}, 0.3f);
    m.mat = kAnodized;
    m.box({8.0f, 0, bore + 0.82f}, {3.5f, 0.38f, 0.1f}, 0.02f);
    railTeeth(m, 4.6f, 11.4f, bore + 0.95f, 0.4f);
    for (float s : {-1.0f, 1.0f}) m.box({8.0f, s * 0.82f, bore}, {3.5f, 0.1f, 0.36f}, 0.02f);
    m.cylinder({4.25f, 0, bore}, {4.6f, 0, bore}, 0.86f, 0.86f, 18, true, 0.05f);
    m.box({11.9f, 0, bore + 0.4f}, {0.35f, 0.35f, 0.95f}, 0.08f);
    m.box({11.9f, 0, bore + 1.45f}, {0.08f, 0.08f, 0.2f}, 0.02f);
    m.mat = kDarkSteel;
    m.cylinder({11.6f, 0, bore}, {18.8f, 0, bore}, 0.33f, 0.31f, 14);
    m.lathe({18.8f, 0, bore}, {1, 0, 0}, {{0, 0}, {0, 0.42f}, {1.9f, 0.42f}, {2.0f, 0.35f}, {2.0f, 0}}, 14);
    m.bone = WB_MAG;
    m.mat = kAnodized;
    m.extrude(magOutline({2.1f, -1.0f}, 40.0f, 6.8f, 2.3f, 2.45f, 0.6f), 0.45f, 0.1f);
    m.box({2.4f, 0, -7.95f}, {1.25f, 0.5f, 0.12f}, 0.05f);
    m.bone = WB_BOLT;
    m.mat = kAnodized;
    m.box({-4.6f, 0, 1.5f}, {0.25f, 0.55f, 0.12f}, 0.04f);

    w.muzzle = {20.9f, 0, bore};
    w.eject = {0.6f, -0.55f, 1.05f};
    w.ejectDir = normalize(vec3(0.1f, -1.0f, 0.5f));
    w.magPivot = {2.1f, 0, -1.0f};
    w.magDrop = {0.05f, 0, -1.0f};
    w.boltDir = {-1, 0, 0};
    w.boltTravel = 2.5f;
    vec3 up = normalize(vec3(1.2f, 0, 3.85f));
    w.rightGrip = gripFrame(vec3(-0.6f, 0, -0.35f) - up * 1.7f, normalize(vec3(0.95f, 0.08f, -0.3f)), {0, -1, 0.15f}, {2.55f, 0.0f, -1.25f});
    w.leftGrip = gripFrame({8.9f, 0, bore}, normalize(vec3(0.22f, -1.0f, 0.12f)), {0, 0.2f, -1.0f}, {2.35f, 0.1f, -1.35f});
    w.leftMagGrip = gripFrame({2.3f, 0.0f, -4.0f}, normalize(vec3(0.25f, -1.0f, -0.3f)), {-0.2f, 0.4f, -0.9f}, {2.3f, 0.2f, -1.15f});
    w.rightPose = poseTriggerGrip();
    w.leftPose = poseHandguard();
    w.leftMagPose = poseMag();
    w.viewHold = {{11.2f, -6.1f, -5.5f}, quatFromEuler(0.5f, 3.0f, 16.0f)};
}

void buildAWP(MeshBuilder& m, WeaponModel& w) {
    const float bore = 1.3f;
    m.bone = WB_BODY;
    m.mat = kAwpGreen;
    m.extrude({{-18.4f, 1.0f}, {-18.4f, -4.2f}, {-17.6f, -4.4f}, {-12.5f, -2.6f}, {-7.5f, -1.4f}, {-3.6f, -1.6f},
               {-2.6f, -4.8f}, {-1.3f, -5.0f}, {-0.9f, -4.6f}, {-1.6f, -1.5f}, {-0.5f, -0.4f}, {12.5f, -0.3f},
               {12.9f, 0.35f}, {12.5f, 1.0f}, {-3.5f, 1.0f}, {-9.0f, 1.35f}, {-16.0f, 1.6f}, {-17.6f, 1.6f}}, 0.78f, 0.3f, 32);
    m.mat = kRubber;
    m.box({-18.55f, 0, -1.6f}, {0.16f, 0.72f, 2.7f}, 0.08f);
    m.mat = kDarkSteel;
    m.cylinder({-4.8f, 0, bore}, {6.0f, 0, bore}, 0.72f, 0.72f, 18, true, 0.08f);
    m.cylinder({6.0f, 0, bore}, {30.0f, 0, bore}, 0.52f, 0.40f, 16);
    m.cylinder({30.0f, 0, bore}, {32.6f, 0, bore}, 0.62f, 0.62f, 16, true, 0.08f);
    for (float x : {30.6f, 31.3f, 32.0f})
        for (float s : {-1.0f, 1.0f}) m.box({x, s * 0.58f, bore}, {0.18f, 0.08f, 0.3f}, 0.02f);
    m.box({0.5f, 0, -1.9f}, {1.0f, 0.14f, 0.07f}, 0.03f);
    m.box({1.45f, 0, -1.2f}, {0.08f, 0.14f, 0.7f}, 0.03f);
    m.box({0.1f, 0, -1.0f}, {0.07f, 0.1f, 0.4f}, 0.03f);
    // Scope with rings and turrets.
    m.mat = kAnodized;
    for (float x : {-1.8f, 4.2f}) m.box({x, 0, bore + 1.05f}, {0.32f, 0.5f, 0.78f}, 0.1f);
    m.cylinder({-4.0f, 0, bore + 2.1f}, {7.0f, 0, bore + 2.1f}, 0.55f, 0.55f, 20);
    m.lathe({7.0f, 0, bore + 2.1f}, {1, 0, 0}, {{0, 0.55f}, {1.6f, 0.98f}, {4.0f, 0.98f}, {4.1f, 0.9f}, {4.1f, 0}}, 20);
    m.lathe({-4.0f, 0, bore + 2.1f}, {-1, 0, 0}, {{0, 0.55f}, {1.3f, 0.82f}, {3.2f, 0.82f}, {3.3f, 0.74f}, {3.3f, 0}}, 20);
    m.cylinder({1.5f, 0, bore + 2.5f}, {1.5f, 0, bore + 3.3f}, 0.42f, 0.42f, 14, true, 0.05f);
    m.cylinder({1.5f, -0.5f, bore + 2.1f}, {1.5f, -1.25f, bore + 2.1f}, 0.4f, 0.4f, 14, true, 0.05f);
    m.mat = kLens;
    m.cylinder({11.08f, 0, bore + 2.1f}, {11.15f, 0, bore + 2.1f}, 0.86f, 0.86f, 20);
    m.cylinder({-7.35f, 0, bore + 2.1f}, {-7.4f, 0, bore + 2.1f}, 0.7f, 0.7f, 20);
    m.bone = WB_MAG;
    m.mat = kDarkSteel;
    m.box({1.9f, 0, -0.85f}, {1.4f, 0.45f, 0.95f}, 0.08f);
    m.bone = WB_BOLT;
    m.mat = kDarkSteel;
    m.cylinder({-3.0f, -0.6f, bore}, {-3.0f, -1.9f, bore - 0.3f}, 0.14f, 0.14f, 10);
    m.sphere({-3.0f, -2.02f, bore - 0.33f}, {0.34f, 0.34f, 0.34f}, 12, 8);

    w.muzzle = {32.8f, 0, bore};
    w.eject = {-1.5f, -0.7f, bore + 0.2f};
    w.ejectDir = normalize(vec3(0.0f, -1.0f, 0.6f));
    w.magPivot = {1.9f, 0, -0.2f};
    w.magDrop = {0, 0, -1};
    w.boltTravel = 3.2f;
    vec3 up = normalize(vec3(0.85f, 0, 3.9f));
    w.rightGrip = gripFrame(vec3(-1.1f, 0, -1.0f) - up * 1.55f, normalize(vec3(0.97f, 0.08f, -0.21f)), {0, -1, 0.15f}, {2.6f, 0.0f, -1.32f});
    w.leftGrip = gripFrame({8.8f, 0, 0.35f}, normalize(vec3(0.2f, -1.0f, 0.15f)), {0, 0.25f, -1.0f}, {2.35f, 0.1f, -1.55f});
    w.leftMagGrip = gripFrame({1.9f, 0, -2.2f}, normalize(vec3(0.2f, -1.0f, -0.3f)), {-0.1f, 0.4f, -0.9f}, {2.3f, 0.2f, -1.15f});
    w.rightBoltGrip = gripFrame({-3.0f, -2.02f, bore - 0.33f}, normalize(vec3(0.3f, 0.2f, 1.0f)), {0.1f, -1.0f, 0.1f}, {2.4f, 0.3f, -1.0f});
    w.rightPose = poseTriggerGrip();
    w.leftPose = poseHandguard();
    w.leftMagPose = poseMag();
    w.viewHold = {{11.0f, -6.2f, -6.4f}, quatFromEuler(0.5f, 2.5f, 12.0f)};
}

struct PistolSpec {
    float slideLen, slideFront, slideTop, slideHW, slideBevel;
    Mat slideMat, frameMat;
    bool deagle, suppressor;
};

void buildPistol(MeshBuilder& m, WeaponModel& w, const PistolSpec& s) {
    m.bone = WB_BODY;
    m.mat = s.frameMat;
    m.box({0.5f, 0, 0.32f}, {3.4f, 0.44f, 0.34f}, 0.08f);
    m.box({2.7f, 0, 0.02f}, {0.95f, 0.34f, 0.12f}, 0.04f);
    m.box({0.7f, 0, -0.95f}, {1.05f, 0.13f, 0.07f}, 0.03f);
    m.box({1.72f, 0, -0.45f}, {0.07f, 0.13f, 0.52f}, 0.03f);
    m.box({0.3f, 0, -0.42f}, {0.07f, 0.1f, 0.32f}, 0.03f);
    m.extrude({{-3.1f, 0.05f}, {-0.6f, 0.05f}, {-0.95f, -1.2f}, {-1.55f, -3.2f}, {-1.7f, -3.95f}, {-3.1f, -4.1f},
               {-3.5f, -3.8f}, {-3.42f, -2.4f}, {-3.4f, -0.6f}}, 0.56f, 0.2f, 35);
    if (s.deagle) {
        m.mat = kSatin;
        m.box({-3.95f, 0, 0.95f}, {0.2f, 0.18f, 0.3f}, 0.05f);
    }
    m.bone = WB_MAG;
    m.mat = kDarkSteel;
    m.box({-2.35f, 0, -4.15f}, {0.78f, 0.45f, 0.1f}, 0.04f);
    m.box({-2.05f, 0, -2.2f}, {0.6f, 0.36f, 1.95f}, 0.05f);
    m.bone = WB_BOLT;
    m.mat = s.slideMat;
    float x0 = s.slideFront - s.slideLen;
    m.box({(x0 + s.slideFront) * 0.5f, 0, s.slideTop - 0.5f}, {s.slideLen * 0.5f, s.slideHW, 0.5f}, s.slideBevel);
    for (int i = 0; i < 6; i++)
        for (float sd : {-1.0f, 1.0f}) m.box({x0 + 0.3f + i * 0.2f, sd * (s.slideHW + 0.005f), s.slideTop - 0.5f}, {0.05f, 0.02f, 0.38f}, 0.0f);
    m.box({s.slideFront - 0.25f, 0, s.slideTop + 0.08f}, {0.1f, 0.06f, 0.1f}, 0.02f);
    m.box({x0 + 0.3f, 0, s.slideTop + 0.1f}, {0.15f, 0.28f, 0.12f}, 0.03f);
    if (s.deagle) {
        m.box({s.slideFront + 0.5f, 0, s.slideTop - 0.42f}, {1.2f, 0.42f, 0.42f}, 0.12f);
        m.mat = kDarkSteel;
        m.cylinder({s.slideFront + 1.65f, 0, s.slideTop - 0.42f}, {s.slideFront + 1.72f, 0, s.slideTop - 0.42f}, 0.22f, 0.22f, 12);
    }
    if (s.suppressor) {
        m.bone = WB_BODY;
        m.mat = kDarkSteel;
        m.cylinder({s.slideFront - 0.1f, 0, s.slideTop - 0.55f}, {s.slideFront + 0.25f, 0, s.slideTop - 0.55f}, 0.3f, 0.3f, 12);
        m.mat = kGunmetal;
        m.cylinder({s.slideFront + 0.2f, 0, s.slideTop - 0.55f}, {s.slideFront + 7.2f, 0, s.slideTop - 0.55f}, 0.62f, 0.62f, 20, true, 0.1f);
    }
    float muzzleX = s.suppressor ? s.slideFront + 7.25f : s.deagle ? s.slideFront + 1.75f : s.slideFront + 0.05f;
    w.muzzle = {muzzleX, 0, s.slideTop - (s.deagle ? 0.42f : 0.55f)};
    w.eject = {0.6f, -0.5f, s.slideTop - 0.2f};
    w.ejectDir = normalize(vec3(0.0f, -1.0f, 0.7f));
    w.magPivot = {-2.2f, 0, -0.2f};
    w.magDrop = normalize(vec3(-0.18f, 0, -1.0f));
    w.boltTravel = 1.1f;
    vec3 up = normalize(vec3(0.72f, 0, 3.8f));
    vec3 gp = vec3(-1.62f, 0, 0.0f) - up * 1.62f;
    w.rightGrip = gripFrame(gp, normalize(vec3(0.98f, 0.08f, -0.19f)), {0, -1, 0.12f}, {2.5f, 0.0f, -1.28f});
    w.leftGrip = gripFrame(gp + vec3(0.25f, 0.0f, -0.55f), normalize(vec3(0.45f, -0.85f, -0.25f)), {0.25f, 0.9f, -0.35f}, {2.25f, -0.2f, -1.65f});
    w.leftMagGrip = gripFrame({-2.3f, 0, -4.4f}, normalize(vec3(0.2f, -0.6f, -1.0f)), {-0.3f, 0.8f, -0.3f}, {2.0f, 0.2f, -1.0f});
    w.rightPose = poseTriggerGrip();
    w.leftPose = poseSupportPistol();
    w.leftMagPose = poseMag();
    w.viewHold = {{13.2f, -4.7f, -4.3f}, quatFromEuler(1.0f, 4.0f, 10.0f)};
    w.shoulderR = {-6.0f, -7.5f, -10.5f};
    w.shoulderL = {-5.0f, 6.5f, -10.5f};
    w.poleL = {0.0f, 0.8f, -1.0f};
}

void buildKnife(MeshBuilder& m, WeaponModel& w) {
    m.bone = WB_BODY;
    // Blade: flat-ground with a clip point. Built as strips along X.
    m.mat = kBlade;
    const int N = 14;
    auto topZ = [](float t) { return t < 0.72f ? 0.42f : lerpf(0.42f, 0.02f, (t - 0.72f) / 0.28f); };
    auto botZ = [](float t) { return lerpf(-0.55f, 0.02f, std::pow(t, 2.2f)); };
    const float x0 = 2.05f, x1 = 7.4f, spine = 0.11f;
    std::vector<vec3> top, mid, edge;
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N;
        float x = lerpf(x0, x1, t);
        float zt = topZ(t), zb = botZ(t);
        float zm = lerpf(zb, zt, 0.5f);
        float th = spine * (1.0f - 0.85f * std::pow(t, 3.0f));
        top.push_back({x, th, zt});
        mid.push_back({x, th * 0.92f, zm});
        edge.push_back({x, 0.004f, zb});
    }
    for (float sd : {-1.0f, 1.0f}) {
        for (int i = 0; i < N; i++) {
            auto S = [&](vec3 p) { return vec3(p.x, p.y * sd, p.z); };
            vec3 a = S(top[i]), b = S(top[i + 1]), c = S(mid[i + 1]), d = S(mid[i]);
            vec3 n = vec3(0, sd, 0);
            m.quad(m.vtx(a, n), m.vtx(b, n), m.vtx(c, n), m.vtx(d, n));
            vec3 e = S(edge[i]), f = S(edge[i + 1]);
            vec3 gn = normalize(vec3(0, sd * (d.z - e.z), (d.y - e.y) * sd * sd * 1.0f));
            gn = normalize(vec3(0, sd, -0.12f / std::max(0.05f, d.z - e.z)));
            m.quad(m.vtx(d, gn), m.vtx(c, gn), m.vtx(f, gn), m.vtx(e, gn));
        }
    }
    for (int i = 0; i < N; i++) {
        vec3 a = top[i], b = top[i + 1];
        vec3 n(0, 0, 1);
        if (b.z < a.z - 0.01f) n = normalize(vec3(a.z - b.z, 0, b.x - a.x));
        m.quad(m.vtx({a.x, a.y, a.z}, n), m.vtx({b.x, b.y, b.z}, n), m.vtx({b.x, -b.y, b.z}, n), m.vtx({a.x, -a.y, a.z}, n));
    }
    m.mat = kDarkSteel;
    m.box({1.9f, 0, -0.05f}, {0.14f, 0.3f, 0.78f}, 0.05f);
    m.mat = kRubber;
    m.extrude({{-3.2f, -0.52f}, {1.78f, -0.5f}, {1.78f, 0.5f}, {-0.2f, 0.58f}, {-3.2f, 0.58f}, {-3.55f, 0.35f}, {-3.55f, -0.3f}}, 0.42f, 0.26f, 40);
    for (int i = 0; i < 3; i++) m.box({-1.6f + i * 0.95f, 0, -0.56f}, {0.2f, 0.36f, 0.06f}, 0.04f);
    m.mat = kDarkSteel;
    m.box({-3.62f, 0, 0.02f}, {0.1f, 0.34f, 0.46f}, 0.05f);

    w.muzzle = {7.4f, 0, 0};
    w.leftOnWeapon = false;
    w.rightGrip = gripFrame({-0.7f, 0, 0.0f}, normalize(vec3(0.0f, 0.35f, -1.0f)), {0.0f, -1.0f, -0.25f}, {2.45f, 0.1f, -1.2f});
    w.leftGrip = w.rightGrip;
    w.rightPose = poseFist();
    w.leftPose = poseRelaxed();
    w.viewHold = {{12.5f, -5.8f, -4.6f}, quatFromEuler(12.0f, 18.0f, 10.0f)};
    w.shoulderR = {-6.0f, -8.0f, -10.0f};
    w.shoulderL = {-3.0f, 8.0f, -10.0f};
}

void buildGrenade(MeshBuilder& m, WeaponModel& w, int id) {
    m.bone = WB_BODY;
    if (id == W_HE) {
        m.mat = {{0.28f, 0.33f, 0.20f}, 0.6f, 0.1f, 4};
        m.lathe({0, 0, -1.2f}, {0, 0, 1}, {{0, 0}, {0.1f, 0.6f}, {0.5f, 1.0f}, {1.2f, 1.12f}, {1.9f, 1.0f}, {2.3f, 0.6f}, {2.4f, 0.36f}, {2.4f, 0}}, 18);
    } else if (id == W_SMOKE) {
        m.mat = {{0.42f, 0.44f, 0.46f}, 0.5f, 0.4f, 5};
        m.cylinder({0, 0, -1.8f}, {0, 0, 1.6f}, 0.95f, 0.95f, 18, true, 0.12f);
        m.mat = {{0.75f, 0.60f, 0.15f}, 0.5f, 0.2f, 0};
        m.cylinder({0, 0, -0.3f}, {0, 0, 0.3f}, 0.97f, 0.97f, 18, false);
    } else {
        m.mat = {{0.30f, 0.32f, 0.34f}, 0.4f, 0.7f, 5};
        m.cylinder({0, 0, -1.6f}, {0, 0, 1.5f}, 0.8f, 0.8f, 16, true, 0.1f);
        m.mat = {{0.08f, 0.08f, 0.08f}, 0.6f, 0.0f, 0};
        for (int i = 0; i < 6; i++) {
            float a = i * kTwoPi / 6;
            m.box({std::cos(a) * 0.79f, std::sin(a) * 0.79f, 0.3f}, {0.08f, 0.08f, 0.35f}, 0.02f);
        }
    }
    m.mat = kGunmetal;
    m.cylinder({0, 0, 1.2f}, {0, 0, 2.0f}, 0.38f, 0.34f, 12, true, 0.05f);
    m.box({-0.45f, 0, 0.9f}, {0.08f, 0.35f, 1.3f}, 0.03f);
    m.cylinder({0.25f, 0.42f, 1.8f}, {0.25f, 0.5f, 1.8f}, 0.5f, 0.5f, 12, false);
    w.muzzle = {0, 0, 0};
    w.leftOnWeapon = false;
    w.rightGrip = gripFrame({0, 0, 0}, normalize(vec3(0.3f, 0.2f, -1.0f)), {-0.3f, -1.0f, 0.0f}, {2.2f, 0.2f, -1.55f});
    w.leftGrip = w.rightGrip;
    w.rightPose = poseFist();
    w.leftPose = poseRelaxed();
    w.viewHold = {{12.5f, -5.4f, -4.9f}, quatFromEuler(10.0f, 12.0f, 6.0f)};
    w.shoulderR = {-6.0f, -8.0f, -10.0f};
    w.shoulderL = {-3.0f, 8.0f, -10.0f};
}

void buildC4(MeshBuilder& m, WeaponModel& w) {
    m.bone = WB_BODY;
    m.mat = {{0.62f, 0.56f, 0.40f}, 0.8f, 0.0f, 2};
    for (int i = 0; i < 3; i++) m.box({0, -1.1f + i * 1.1f, 0}, {3.0f, 0.52f, 0.8f}, 0.18f);
    m.mat = {{0.12f, 0.12f, 0.12f}, 0.5f, 0.0f, 0};
    m.box({0.4f, 0, 0.95f}, {2.0f, 1.3f, 0.2f}, 0.06f);
    m.mat = {{0.9f, 0.1f, 0.05f}, 0.3f, 0.0f, 8};
    m.box({-0.5f, 0, 1.16f}, {0.7f, 0.45f, 0.02f});
    m.mat = {{0.5f, 0.5f, 0.5f}, 0.4f, 0.3f, 0};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) m.box({1.0f + c * 0.42f, -0.42f + r * 0.42f, 1.17f}, {0.15f, 0.15f, 0.03f}, 0.02f);
    m.mat = {{0.7f, 0.1f, 0.1f}, 0.5f, 0.0f, 0};
    m.cylinder({-3.0f, -1.1f, 0.5f}, {3.0f, -1.3f, 0.9f}, 0.08f, 0.08f, 6);
    m.mat = {{0.1f, 0.2f, 0.7f}, 0.5f, 0.0f, 0};
    m.cylinder({-3.0f, 1.1f, 0.5f}, {3.0f, 1.3f, 0.9f}, 0.08f, 0.08f, 6);
    w.leftOnWeapon = true;
    w.rightGrip = gripFrame({-1.0f, -1.6f, 0}, normalize(vec3(0.2f, 1.0f, 0.0f)), {0, 0, 1}, {2.3f, 0, -1.2f});
    w.leftGrip = gripFrame({-1.0f, 1.6f, 0}, normalize(vec3(0.2f, -1.0f, 0.0f)), {0, 0, 1}, {2.3f, 0, -1.2f});
    w.rightPose = poseMag();
    w.leftPose = poseMag();
    w.viewHold = {{14.0f, -1.0f, -9.0f}, quatFromEuler(25.0f, 0.0f, 0.0f)};
}

}  // namespace

FingerPose FingerPose::lerp(const FingerPose& a, const FingerPose& b, float t) {
    FingerPose r;
    for (int f = 0; f < 5; f++) {
        for (int j = 0; j < 3; j++) r.curl[f][j] = lerpf(a.curl[f][j], b.curl[f][j], t);
        r.spread[f] = lerpf(a.spread[f], b.spread[f], t);
    }
    r.thumbTwist = lerpf(a.thumbTwist, b.thumbTwist, t);
    return r;
}

FingerPose poseRelaxed() {
    return poseFrom({0.35f, 0.45f, 0.3f,  0.4f, 0.5f, 0.35f,  0.45f, 0.55f, 0.35f,  0.5f, 0.55f, 0.35f,
                     0.15f, 0.2f, 0.15f,  0.08f, 0.0f, -0.06f, -0.12f, 0.0f, 0.0f});
}

void buildWeaponModel(int id, Renderer& r, WeaponModel& w) {
    MeshBuilder m;
    w.id = id;
    switch (id) {
        case W_AK47: buildAK(m, w); break;
        case W_M4A4: buildM4(m, w); break;
        case W_AWP: buildAWP(m, w); break;
        case W_DEAGLE: buildPistol(m, w, {8.6f, 4.8f, 1.78f, 0.5f, 0.1f, kSatin, kPolymer, true, false}); break;
        case W_GLOCK: buildPistol(m, w, {7.0f, 3.75f, 1.68f, 0.46f, 0.06f, {{0.17f, 0.17f, 0.18f}, 0.42f, 0.6f, 5}, kPolymer, false, false}); break;
        case W_USP: buildPistol(m, w, {6.9f, 3.6f, 1.66f, 0.47f, 0.16f, {{0.12f, 0.12f, 0.13f}, 0.5f, 0.3f, 5}, kPolymer, false, true}); break;
        case W_HE: case W_SMOKE: case W_FLASH: buildGrenade(m, w, id); break;
        case W_C4: buildC4(m, w); break;
        default: buildKnife(m, w); break;
    }
    for (auto& v : m.verts) w.bounds.add(v.pos);
    w.mesh = m.upload(r);
}
