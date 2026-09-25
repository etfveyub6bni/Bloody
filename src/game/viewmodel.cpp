#include "game/viewmodel.h"


namespace {

struct Pose {
    vec3 pos;  // view space offset
    vec3 rot;  // pitch, yaw, roll degrees (about the weapon origin)
    void add(const Pose& o, float w) { pos += o.pos * w; rot += o.rot * w; }
};

Xform blend(const Xform& a, const Xform& b, float t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    return lerp(a, b, t);
}

float kick(float t, float rise, float decay) { return t < 0 ? 0 : t < rise ? t / rise : std::exp(-(t - rise) * decay); }

mat4 aboutPivot(vec3 pivot, quat q, vec3 translation) {
    return translate(pivot + translation) * toMat4(q) * translate(-pivot);
}

}  // namespace

void animateViewmodel(const WeaponModel& wm, const ViewmodelParams& p, ViewmodelState& st, ViewmodelPose& out) {
    const WeaponDef& def = weaponDef(p.weapon);
    const bool pistol = def.cls == WC_PISTOL, rifle = def.cls == WC_RIFLE, sniper = def.cls == WC_SNIPER;
    const bool knife = def.cls == WC_KNIFE, grenade = def.cls == WC_GRENADE;
    const float dt = std::max(p.dt, 1e-4f);
    Pose pose;

    // Idle breathing.
    pose.pos.z += std::sin(p.time * 1.6f) * 0.04f;
    pose.rot.x += std::sin(p.time * 1.1f) * 0.15f;
    pose.rot.z += std::sin(p.time * 0.8f) * 0.2f;

    // Movement bob (figure eight).
    float spd = saturate(p.speed / 250.0f) * (p.onGround ? 1.0f : 0.25f);
    st.bobAmp = damp(st.bobAmp, spd, 8.0f, dt);
    st.bobPhase += dt * p.speed / 34.0f;
    pose.pos.y += std::sin(st.bobPhase) * 0.42f * st.bobAmp;
    pose.pos.z += (std::cos(st.bobPhase * 2.0f) - 1.0f) * 0.16f * st.bobAmp - 0.25f * st.bobAmp;
    pose.rot.z += std::sin(st.bobPhase) * 1.1f * st.bobAmp;
    pose.rot.x += (std::cos(st.bobPhase * 2.0f)) * 0.35f * st.bobAmp;

    // Landing spring.
    if (p.onGround && !st.wasOnGround) st.landVel -= 14.0f;
    st.wasOnGround = p.onGround;
    st.landVel += (-90.0f * st.land - 11.0f * st.landVel) * dt;
    st.land += st.landVel * dt;
    pose.pos.z += st.land * 0.35f;
    pose.rot.x -= st.land * 1.2f;

    // Crouch.
    pose.pos.z -= 0.35f * p.crouch;
    pose.rot.z += 2.0f * p.crouch;

    // Mouse sway: the weapon lags behind view rotation.
    vec3 target(clampf(-p.pitchDelta * 0.9f, -4.0f, 4.0f), clampf(-p.yawDelta * 0.9f, -5.0f, 5.0f), 0);
    target.z = target.y * 0.6f;
    st.sway = damp(st.sway, target, 9.0f, dt);
    pose.rot.x += st.sway.x;
    pose.rot.y += st.sway.y;
    pose.rot.z += st.sway.z;
    pose.pos.y += st.sway.y * 0.06f;
    pose.pos.z += st.sway.x * 0.05f;

    for (int i = 0; i < WB_COUNT; i++) out.weaponBones[i] = mat4();
    Xform rightTarget = wm.rightGrip, leftTarget = wm.leftGrip;  // weapon space for now
    FingerPose leftFingers = wm.leftPose, rightFingers = wm.rightPose;
    float leftAway = wm.leftOnWeapon ? 0.0f : 1.0f;  // 1 = support hand parked out of view
    Xform leftAwayView{{6.0f, 9.0f, -24.0f}, quatFromEuler(60, -20, 0)};
    bool leftInView = true;

    // Deploy.
    if (p.deployT < p.deployDur) {
        float u = saturate(p.deployT / p.deployDur);
        float w = 1.0f - smooth01(std::min(1.0f, u * 1.25f));
        w = w * w * (1.0f + 0.3f * (1.0f - w));
        Pose d{{-3.0f, 1.0f, -9.0f}, {-38.0f, 8.0f, 28.0f}};
        if (knife) d = Pose{{-2.0f, -2.0f, -8.0f}, {-30.0f, -30.0f, -60.0f}};
        pose.add(d, w);
        if ((rifle || pistol) && u > 0.5f) {
            float pull = bump(u, 0.55f, 0.66f, 0.72f, 0.84f);
            out.weaponBones[WB_BOLT] = translate(wm.boltDir * (wm.boltTravel * pull));
        }
    }

    // Firing.
    if (!knife && !grenade) {
        float k = kick(p.fireT, 0.018f, pistol ? 13.0f : 17.0f);
        float rs = std::sin(p.shots * 12.9898f) * 0.5f;
        if (sniper) pose.add({{-2.6f, 0.2f, 0.4f}, {7.0f, 0.5f, 2.0f}}, k);
        else if (pistol) pose.add({{-1.0f, 0.0f, 0.25f}, {5.5f, rs * 1.2f, rs * 2.0f}}, k);
        else pose.add({{-1.15f, 0.05f, 0.12f}, {2.0f, rs * 0.8f, rs * 2.2f}}, k);
        if (!sniper && p.fireT < 0.12f) {
            float travel = p.fireT < 0.025f ? p.fireT / 0.025f : std::max(0.0f, 1.0f - (p.fireT - 0.025f) / 0.07f);
            out.weaponBones[WB_BOLT] = translate(wm.boltDir * (wm.boltTravel * travel));
        }
    }
    if (pistol && p.slideLocked && p.reloadT < 0) out.weaponBones[WB_BOLT] = translate(wm.boltDir * wm.boltTravel);

    // AWP bolt cycle.
    if (sniper && p.boltT >= 0 && p.reloadT < 0) {
        float b = p.boltT;
        float toBolt = ease(b, 0.12f, 0.3f) * (1.0f - ease(b, 0.95f, 1.2f));
        float rotUp = ease(b, 0.3f, 0.42f) * (1.0f - ease(b, 0.8f, 0.92f));
        float back = ease(b, 0.42f, 0.56f) * (1.0f - ease(b, 0.62f, 0.78f));
        vec3 axisPt(0, 0, 1.3f);
        out.weaponBones[WB_BOLT] = aboutPivot(axisPt, qaxis({1, 0, 0}, -rotUp * 65.0f * kDeg), vec3(-wm.boltTravel * back, 0, 0));
        pose.add({{-0.5f, 0.8f, 0.6f}, {3.0f, 4.0f, 12.0f}}, bump(b, 0.1f, 0.3f, 0.9f, 1.2f));
        Xform boltHand = Xform{{0, 0, 0}, quat()};
        (void)boltHand;
        mat4 bm = out.weaponBones[WB_BOLT];
        Xform onBolt = wm.rightBoltGrip;
        onBolt.p = xformPoint(bm, onBolt.p);
        onBolt.q = qnormalize(quatFromBasis(normalize(xformDir(bm, rotate(onBolt.q, {1, 0, 0}))),
                                            normalize(xformDir(bm, rotate(onBolt.q, {0, 1, 0}))),
                                            normalize(xformDir(bm, rotate(onBolt.q, {0, 0, 1})))));
        rightTarget = blend(wm.rightGrip, onBolt, toBolt);
        rightFingers = FingerPose::lerp(wm.rightPose, wm.leftMagPose, toBolt);
    }

    // Reload.
    if (p.reloadT >= 0 && (rifle || sniper || pistol)) {
        float u = saturate(p.reloadT / p.reloadDur);
        float magDist = 0, magRot = 0, toMag = 0, away = 0;
        if (pistol) {
            pose.add({{-0.6f, 1.2f, 1.1f}, {12.0f, 6.0f, 20.0f}}, bump(u, 0.0f, 0.15f, 0.78f, 0.96f));
            magDist = 13.0f * ease(u, 0.1f, 0.26f) * (1.0f - ease(u, 0.42f, 0.62f));
            away = ease(u, 0.04f, 0.2f);
            toMag = ease(u, 0.3f, 0.42f) * (1.0f - ease(u, 0.62f, 0.8f));
            away *= 1.0f - ease(u, 0.28f, 0.42f);
            pose.pos.z += 0.35f * bump(u, 0.6f, 0.63f, 0.63f, 0.7f);
            if (p.slideLocked) {
                float rel = ease(u, 0.72f, 0.76f);
                out.weaponBones[WB_BOLT] = translate(wm.boltDir * (wm.boltTravel * (1.0f - rel)));
            }
        } else {
            pose.add({{-0.8f, 1.6f, 1.3f}, {8.0f, 6.0f, 28.0f}}, bump(u, 0.02f, 0.18f, 0.78f, 0.95f));
            magRot = ease(u, 0.2f, 0.28f) * (1.0f - ease(u, 0.62f, 0.69f));
            magDist = 15.0f * ease(u, 0.26f, 0.42f) * (1.0f - ease(u, 0.5f, 0.64f));
            toMag = ease(u, 0.05f, 0.2f) * (1.0f - ease(u, 0.69f, 0.84f));
            pose.pos.z += 0.4f * bump(u, 0.66f, 0.69f, 0.69f, 0.75f);
            if (u > 0.78f && rifle) {
                float pull = bump(u, 0.8f, 0.86f, 0.88f, 0.93f);
                out.weaponBones[WB_BOLT] = translate(wm.boltDir * (wm.boltTravel * pull));
            }
        }
        quat mq = qaxis({0, 1, 0}, -magRot * 18.0f * kDeg);
        mat4 magM = aboutPivot(wm.magPivot, mq, wm.magDrop * magDist);
        out.weaponBones[WB_MAG] = magM;
        Xform onMag = wm.leftMagGrip;
        onMag.p = xformPoint(magM, onMag.p);
        onMag.q = qnormalize(mq * onMag.q);
        leftTarget = blend(wm.leftGrip, onMag, toMag);
        leftFingers = FingerPose::lerp(wm.leftPose, wm.leftMagPose, toMag);
        leftAway = std::max(leftAway, away);
    }

    // Knife attacks.
    if (knife && p.attackT >= 0) {
        if (p.attackKind == 2) {
            float t = p.attackT;
            pose.add({{-3.0f, 1.5f, 1.5f}, {10.0f, 10.0f, 20.0f}}, bump(t, 0.0f, 0.22f, 0.25f, 0.32f));
            pose.add({{9.0f, 3.5f, 1.0f}, {-8.0f, 18.0f, 10.0f}}, bump(t, 0.25f, 0.36f, 0.5f, 0.85f));
        } else {
            float t = p.attackT;
            float s = p.attackKind == 0 ? 1.0f : -1.0f;
            pose.add({{-1.0f, -2.5f * s, 2.0f}, {8.0f, -25.0f * s, -20.0f * s}}, bump(t, 0.0f, 0.1f, 0.1f, 0.18f));
            pose.add({{3.5f, 6.0f * s, 0.5f}, {-5.0f, 55.0f * s, 35.0f * s}}, bump(t, 0.1f, 0.24f, 0.28f, 0.5f));
        }
    }

    // Grenade throw.
    if (grenade && p.throwT >= 0) {
        float t = p.throwT;
        pose.add({{-4.0f, 0.0f, 5.0f}, {35.0f, 0.0f, 0.0f}}, bump(t, 0.0f, 0.25f, 0.3f, 0.36f));
        pose.add({{8.0f, 2.0f, 2.0f}, {-40.0f, 8.0f, 0.0f}}, bump(t, 0.3f, 0.4f, 0.45f, 0.6f));
        out.weaponVisible = t < 0.42f;
    } else {
        out.weaponVisible = true;
    }

    // Inspect.
    if (p.inspectT >= 0) {
        float u = p.inspectT;
        if (knife) {
            float a = ease(u, 0.15f, 0.7f);
            pose.add({{-2.0f, 3.0f, 2.0f}, {10.0f, 35.0f, 0.0f}}, bump(u, 0.05f, 0.35f, 1.9f, 2.4f));
            pose.rot.z += 360.0f * a;
            pose.add({{0.0f, 0.0f, 0.0f}, {0.0f, -20.0f, -40.0f}}, bump(u, 1.0f, 1.3f, 1.7f, 2.2f));
        } else if (pistol) {
            pose.add({{-2.5f, 2.6f, 1.6f}, {5.0f, 28.0f, 72.0f}}, bump(u, 0.05f, 0.5f, 1.2f, 1.55f));
            pose.add({{-1.5f, 1.6f, 1.0f}, {-2.0f, -26.0f, -60.0f}}, bump(u, 1.45f, 1.85f, 2.4f, 2.9f));
            leftAway = std::max(leftAway, bump(u, 0.05f, 0.4f, 2.5f, 2.9f));
        } else if (!grenade) {
            pose.add({{-1.8f, 2.8f, 1.6f}, {10.0f, 22.0f, 62.0f}}, bump(u, 0.08f, 0.8f, 1.6f, 2.1f));
            pose.add({{-0.6f, 1.2f, 1.0f}, {-4.0f, -18.0f, -32.0f}}, bump(u, 1.9f, 2.4f, 3.0f, 3.55f));
        }
    }

    // Compose weapon transform in view space.
    Xform base = wm.viewHold;
    base.p += p.userOffset;
    quat animQ = quatFromEuler(pose.rot.x, pose.rot.y, pose.rot.z);
    out.weapon = Xform(base.p + pose.pos, qnormalize(base.q * animQ));
    out.muzzle = apply(out.weapon, wm.muzzle);

    out.rightHand = out.weapon * rightTarget;
    Xform leftOnWeapon = out.weapon * leftTarget;
    out.leftHand = blend(leftOnWeapon, leftAwayView, saturate(leftAway));
    out.rightFingers = rightFingers;
    out.leftFingers = FingerPose::lerp(leftFingers, poseRelaxed(), saturate(leftAway));
    (void)leftInView;
}

void buildArmBones(const ArmsModel& arms, const WeaponModel& wm, const ViewmodelPose& pose, const Xform& eye, mat4 bones[AB_PER_ARM * 2]) {
    for (int side = 0; side < 2; side++) {
        bool left = side == 1;
        const Xform& hand = left ? pose.leftHand : pose.rightHand;
        Xform upper, fore;
        float stretch;
        solveArmIK(left ? wm.shoulderL : wm.shoulderR, hand, normalize(left ? wm.poleL : wm.poleR), arms.upperLen, arms.foreLen, upper, fore, stretch);
        int b = left ? AB_PER_ARM : 0;
        bones[b + AB_UPPER] = toMat4(eye * upper);
        bones[b + AB_FORE] = toMat4(eye * fore) * scale(vec3(stretch, 1, 1));
        bones[b + AB_HAND] = toMat4(eye * hand);
        Xform fingers[15];
        poseFingers(hand, left ? pose.leftFingers : pose.rightFingers, left, fingers);
        for (int i = 0; i < 15; i++) bones[b + AB_FINGER0 + i] = toMat4(eye * fingers[i]);
    }
}
