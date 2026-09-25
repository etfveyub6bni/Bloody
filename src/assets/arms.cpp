#include "assets/models.h"

namespace {

struct ArmStyle {
    Mat sleeve, cuff, glove, pad;
    vec3 camoA, camoB;
};

void buildArm(MeshBuilder& m, int base, const ArmStyle& st, float L1, float L2) {
    const vec3 Y(0, 1, 0), Z(0, 0, 1);
    // Upper arm (sleeve), bone-local X along the arm.
    m.bone = base + AB_UPPER;
    m.mat = st.sleeve;
    {
        std::vector<Ring> r;
        const float xs[] = {-1.5f, 1.5f, 6.0f, 10.0f, L1 + 0.4f};
        const float ry[] = {2.55f, 2.5f, 2.36f, 2.22f, 2.12f}, rz[] = {2.45f, 2.4f, 2.26f, 2.12f, 2.02f};
        for (int i = 0; i < 5; i++) r.push_back(ringEllipse({xs[i], 0, 0}, Y, Z, ry[i], rz[i], 18));
        m.loft(r);
    }
    // Forearm (sleeve with rolled cuff).
    m.bone = base + AB_FORE;
    m.sphere({0, 0, 0}, {1.98f, 1.95f, 1.92f}, 16, 10);
    {
        std::vector<Ring> r;
        const float xs[] = {-0.6f, 1.6f, 4.0f, 6.6f, L2 - 2.3f};
        const float ry[] = {1.95f, 2.0f, 1.86f, 1.7f, 1.6f}, rz[] = {1.88f, 1.92f, 1.8f, 1.63f, 1.53f};
        for (int i = 0; i < 5; i++) {
            Ring ring = ringEllipse({xs[i], 0, 0}, Y, Z, ry[i], rz[i], 18);
            // Subtle fabric folds.
            for (size_t k = 0; k < ring.size(); k++) {
                float a = kTwoPi * k / ring.size();
                float f = 1.0f + 0.025f * std::sin(a * 3.0f + i * 1.7f);
                ring[k] = vec3(xs[i], 0, 0) + (ring[k] - vec3(xs[i], 0, 0)) * f;
            }
            r.push_back(ring);
        }
        m.loft(r, false, false);
    }
    m.mat = st.cuff;
    {
        std::vector<Ring> r;
        r.push_back(ringEllipse({L2 - 2.35f, 0, 0}, Y, Z, 1.62f, 1.55f, 18));
        r.push_back(ringEllipse({L2 - 2.2f, 0, 0}, Y, Z, 1.76f, 1.68f, 18));
        r.push_back(ringEllipse({L2 - 1.2f, 0, 0}, Y, Z, 1.76f, 1.68f, 18));
        r.push_back(ringEllipse({L2 - 1.05f, 0, 0}, Y, Z, 1.58f, 1.5f, 18));
        r.push_back(ringEllipse({L2 - 1.2f, 0, 0}, Y, Z, 1.42f, 1.35f, 18));
        m.loft(r, false, true);
    }
    // Hand: glove cuff, palm, pads.
    m.bone = base + AB_HAND;
    m.mat = st.glove;
    {
        std::vector<Ring> r;
        r.push_back(ringEllipse({-2.4f, 0, 0.0f}, Y, Z, 1.44f, 1.2f, 16));
        r.push_back(ringEllipse({-0.8f, 0, 0.0f}, Y, Z, 1.4f, 1.13f, 16));
        r.push_back(ringEllipse({0.3f, 0, 0.02f}, Y, Z, 1.36f, 0.98f, 16));
        m.loft(r);
    }
    m.mat = st.pad;
    m.box({-0.95f, 0, 0.02f}, {0.42f, 1.52f, 1.24f}, 0.36f);
    m.mat = st.glove;
    {
        std::vector<Ring> r;
        const float xs[] = {0.1f, 1.2f, 2.5f, 3.4f, 3.9f};
        const float hu[] = {1.2f, 1.46f, 1.62f, 1.62f, 1.44f}, hv[] = {0.6f, 0.66f, 0.62f, 0.53f, 0.4f};
        for (int i = 0; i < 5; i++) r.push_back(ringRoundRect({xs[i], 0.05f, 0.05f + (i >= 3 ? 0.03f : 0.0f)}, Y, Z, hu[i], hv[i], 0.42f, 3));
        m.loft(r);
    }
    m.sphere({1.35f, 0.95f, -0.25f}, {1.05f, 0.62f, 0.62f}, 12, 8);
    m.mat = st.pad;
    m.box({3.3f, 0.02f, 0.56f}, {0.34f, 1.42f, 0.14f}, 0.1f);
    m.box({1.9f, 0.05f, 0.6f}, {0.9f, 1.0f, 0.1f}, 0.08f);
    // Fingers: capsules in each phalanx frame.
    const HandGeom& g = handGeom();
    const float rad[5][2] = {{0.40f, 0.35f}, {0.41f, 0.36f}, {0.39f, 0.34f}, {0.35f, 0.30f}, {0.50f, 0.39f}};
    for (int f = 0; f < 5; f++)
        for (int j = 0; j < 3; j++) {
            m.bone = base + AB_FINGER0 + f * 3 + j;
            m.mat = st.glove;
            float r0 = lerpf(rad[f][0], rad[f][1], j / 3.0f), r1 = lerpf(rad[f][0], rad[f][1], (j + 1) / 3.0f);
            float len = g.len[f][j];
            m.capsule({0, 0, 0}, {len - r1 * 0.4f, 0, 0}, r0, r1, 10);
            if (j == 0 && f < 4) {
                m.mat = st.pad;
                m.box({len * 0.45f, 0, r0 * 0.78f}, {len * 0.3f, r0 * 0.75f, 0.08f}, 0.05f);
            }
        }
}

}  // namespace

const HandGeom& handGeom() {
    static HandGeom g = [] {
        HandGeom h;
        h.knuckle[0] = {3.55f, 1.05f, 0.12f};
        h.knuckle[1] = {3.68f, 0.36f, 0.14f};
        h.knuckle[2] = {3.55f, -0.34f, 0.12f};
        h.knuckle[3] = {3.22f, -0.98f, 0.05f};
        h.knuckle[4] = {0.85f, 0.95f, -0.3f};
        const float L[5][3] = {{1.75f, 1.05f, 0.85f}, {1.9f, 1.15f, 0.9f}, {1.8f, 1.1f, 0.85f}, {1.45f, 0.85f, 0.75f}, {1.55f, 1.2f, 1.0f}};
        for (int f = 0; f < 5; f++)
            for (int j = 0; j < 3; j++) h.len[f][j] = L[f][j];
        h.thumbBase = xformFromBasis(vec3(0), normalize(vec3(0.75f, 0.62f, -0.28f)), normalize(vec3(0.0f, 0.55f, 0.85f))).q;
        return h;
    }();
    return g;
}

void poseFingers(const Xform& hand, const FingerPose& pose, bool left, Xform out[15]) {
    const HandGeom& g = handGeom();
    auto mir = [left](Xform x) {
        if (left) {
            x.p.y = -x.p.y;
            x.q = quat(-x.q.x, x.q.y, -x.q.z, x.q.w);
        }
        return x;
    };
    const vec3 Y(0, 1, 0), Z(0, 0, 1), X(1, 0, 0);
    for (int f = 0; f < 5; f++) {
        quat q0 = f < 4 ? qaxis(Z, pose.spread[f]) * qaxis(Y, pose.curl[f][0])
                        : g.thumbBase * qaxis(X, pose.thumbTwist) * qaxis(Z, pose.spread[4]) * qaxis(Y, pose.curl[4][0]);
        Xform acc = hand * mir(Xform(g.knuckle[f], q0));
        out[f * 3] = acc;
        acc = acc * mir(Xform(vec3(g.len[f][0], 0, 0), qaxis(Y, pose.curl[f][1])));
        out[f * 3 + 1] = acc;
        acc = acc * mir(Xform(vec3(g.len[f][1], 0, 0), qaxis(Y, pose.curl[f][2])));
        out[f * 3 + 2] = acc;
    }
}

void solveArmIK(vec3 shoulder, const Xform& hand, vec3 pole, float L1, float L2, Xform& upper, Xform& fore, float& stretch) {
    vec3 W = hand.p;
    vec3 d = W - shoulder;
    float dist = std::max(length(d), 1e-3f);
    vec3 n = d / dist;
    float dc = clampf(dist, std::fabs(L1 - L2) + 0.05f, (L1 + L2) * 0.9995f);
    float cosA = clampf((L1 * L1 + dc * dc - L2 * L2) / (2.0f * L1 * dc), -1.0f, 1.0f);
    float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    vec3 p = pole - n * dot(pole, n);
    p = length2(p) < 1e-6f ? anyPerp(n) : normalize(p);
    vec3 E = shoulder + n * (cosA * L1) + p * (sinA * L1);
    upper = xformFromBasis(shoulder, E - shoulder, p);
    vec3 handZ = rotate(hand.q, vec3(0, 0, 1));
    fore = xformFromBasis(E, W - E, handZ);
    stretch = std::max(1.0f, length(W - E) / L2);
}

void buildArmsModel(int team, Renderer& r, ArmsModel& out) {
    ArmStyle st;
    if (team == 2) {  // CT: navy camo sleeves, black tactical gloves
        st.sleeve = {{0.21f, 0.25f, 0.33f}, 0.9f, 0.0f, 3};
        st.cuff = {{0.18f, 0.21f, 0.28f}, 0.9f, 0.0f, 2};
        st.glove = {{0.2f, 0.2f, 0.21f}, 0.55f, 0.0f, 6};
        st.pad = {{0.3f, 0.31f, 0.32f}, 0.5f, 0.0f, 4};
        out.patternA = {0.13f, 0.15f, 0.21f};
        out.patternB = {0.33f, 0.37f, 0.45f};
    } else {  // T: desert jacket, brown leather gloves
        st.sleeve = {{0.50f, 0.44f, 0.33f}, 0.9f, 0.0f, 3};
        st.cuff = {{0.44f, 0.38f, 0.28f}, 0.9f, 0.0f, 2};
        st.glove = {{0.30f, 0.21f, 0.14f}, 0.58f, 0.0f, 6};
        st.pad = {{0.20f, 0.15f, 0.10f}, 0.6f, 0.0f, 6};
        out.patternA = {0.38f, 0.32f, 0.23f};
        out.patternB = {0.60f, 0.55f, 0.42f};
    }
    MeshBuilder m;
    buildArm(m, 0, st, out.upperLen, out.foreLen);
    m.setXform(scale(vec3(1, -1, 1)));
    buildArm(m, AB_PER_ARM, st, out.upperLen, out.foreLen);
    m.resetXform();
    out.mesh = m.upload(r);
}
