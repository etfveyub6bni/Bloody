#include "assets/models.h"

namespace {

struct ArmStyle {
    Mat sleeve, cuff, skin, glove, pad, armor;
    bool bareForearm = false;  // T: sleeves rolled up above the elbow
    bool knuckleArmor = false;
};

inline float sq(float x) { return x * x; }

// Cross-section in the bone's YZ plane at distance x along the bone: superellipse with separate
// dorsal (top, +Z) and palmar (bottom, -Z) extents. `bump` adds a radial offset by angle.
template <typename F>
Ring section(float x, float halfW, float top, float bottom, float power, int n, F bump, float zOff = 0.0f) {
    Ring r;
    for (int i = 0; i < n; i++) {
        float a = kTwoPi * i / n, c = std::cos(a), s = std::sin(a);
        float e = 2.0f / power;
        float y = halfW * (c < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(c), e);
        float z = (s >= 0 ? top : bottom) * (s < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(s), e);
        float b = bump(a);
        float len = std::sqrt(y * y + z * z);
        if (len > 1e-5f) {
            y += y / len * b;
            z += z / len * b;
        }
        r.push_back(vec3(x, y, z + zOff));
    }
    return r;
}
Ring section(float x, float halfW, float top, float bottom, float power, int n, float zOff = 0.0f) {
    return section(x, halfW, top, bottom, power, n, [](float) { return 0.0f; }, zOff);
}

// One finger phalanx along +X in its bone frame: slightly flattened, knuckle bulge at the base, rounded tip.
void phalanx(MeshBuilder& m, float len, float r0, float r1, bool tip) {
    std::vector<Ring> rs;
    const int n = 12;
    float x0 = -r0 * 0.55f;
    rs.push_back(section(x0, r0 * 0.55f, r0 * 0.5f, r0 * 0.45f, 2.0f, n));
    rs.push_back(section(x0 + r0 * 0.3f, r0 * 0.92f, r0 * 0.95f, r0 * 0.85f, 2.2f, n));
    rs.push_back(section(len * 0.18f, r0 * 1.02f, r0 * 1.02f, r0 * 0.9f, 2.3f, n));
    rs.push_back(section(len * 0.55f, lerpf(r0, r1, 0.5f) * 0.93f, lerpf(r0, r1, 0.5f) * 0.9f, lerpf(r0, r1, 0.5f) * 0.92f, 2.3f, n));
    if (tip) {
        rs.push_back(section(len * 0.82f, r1 * 1.0f, r1 * 0.85f, r1 * 1.0f, 2.2f, n));
        for (int k = 1; k <= 3; k++) {
            float a = k / 3.0f * kPi * 0.5f;
            float c = std::cos(a);
            rs.push_back(section(len * 0.82f + std::sin(a) * r1 * 1.05f, r1 * c, r1 * 0.85f * c, r1 * c, 2.2f, n, -r1 * 0.1f * (1 - c)));
        }
    } else {
        rs.push_back(section(len - r1 * 0.2f, r1 * 0.96f, r1 * 0.95f, r1 * 0.9f, 2.3f, n));
        rs.push_back(section(len + r1 * 0.35f, r1 * 0.7f, r1 * 0.65f, r1 * 0.6f, 2.0f, n));
    }
    m.loft(rs, true, true);
}

void buildArm(MeshBuilder& m, int base, const ArmStyle& st, float L1, float L2) {
    // Upper arm (bone X along the arm). Mostly off screen; a rolled sleeve for T, a sleeve for CT.
    m.bone = base + AB_UPPER;
    {
        std::vector<Ring> r;
        const float xs[] = {-2.0f, 0.5f, 4.0f, 8.0f, L1 + 0.6f};
        const float w[] = {2.5f, 2.6f, 2.45f, 2.2f, 2.0f};
        for (int i = 0; i < 5; i++) r.push_back(section(xs[i], w[i], w[i] * 0.95f, w[i] * 0.95f, 2.0f, 18));
        m.mat = st.bareForearm ? st.skin : st.sleeve;
        m.loft(r);
    }
    if (st.bareForearm) {  // rolled sleeve band
        m.mat = st.sleeve;
        std::vector<Ring> r;
        const float xs[] = {-2.2f, 0.0f, 3.0f, 4.6f, 5.0f};
        const float w[] = {2.7f, 2.95f, 2.9f, 2.75f, 2.4f};
        for (int i = 0; i < 5; i++)
            r.push_back(section(xs[i], w[i], w[i], w[i], 2.0f, 18, [i](float a) { return 0.09f * std::sin(a * 4.0f + i * 1.3f); }));
        m.loft(r);
    }
    // Forearm: +Y is the thumb (radial) side, +Z follows the back of the hand.
    m.bone = base + AB_FORE;
    m.mat = st.bareForearm ? st.skin : st.sleeve;
    {
        std::vector<Ring> r;
        struct S { float x, w, top, bot; };
        const S ss[] = {{-1.1f, 1.45f, 1.35f, 1.35f}, {0.2f, 1.9f, 1.72f, 1.62f}, {2.0f, 2.02f, 1.74f, 1.6f}, {4.2f, 1.86f, 1.52f, 1.42f},
                        {6.6f, 1.6f, 1.25f, 1.18f}, {8.8f, 1.36f, 1.0f, 0.95f}, {L2 - 0.6f, 1.24f, 0.86f, 0.84f}, {L2 + 0.4f, 1.2f, 0.82f, 0.8f}};
        const int n = 20;
        for (const S& s : ss) {
            float x = s.x;
            float grow = st.bareForearm ? 0.0f : 0.28f;
            // Brachioradialis bulge on the radial side near the elbow, ulnar head bump at the wrist.
            auto bump = [x, L2](float a) {
                float radial = std::max(0.0f, std::cos(a - 0.5f));
                float ulnar = std::max(0.0f, std::cos(a + 2.2f));
                return 0.22f * radial * std::exp(-sq((x - 2.2f) / 2.6f)) + 0.12f * ulnar * std::exp(-sq((x - (L2 - 0.7f)) / 0.7f));
            };
            r.push_back(section(x, s.w + grow, s.top + grow, s.bot + grow, 2.1f, n, bump));
        }
        m.loft(r);
    }
    if (!st.bareForearm) {  // sleeve folds and cuff
        m.mat = st.cuff;
        std::vector<Ring> r;
        const float xs[] = {L2 - 2.9f, L2 - 2.7f, L2 - 1.5f, L2 - 1.3f, L2 - 1.45f};
        const float g[] = {1.52f, 1.64f, 1.62f, 1.46f, 1.3f};
        for (int i = 0; i < 5; i++)
            r.push_back(section(xs[i], g[i] * 1.12f, g[i] * 0.88f, g[i] * 0.86f, 2.2f, 20, [i](float a) { return 0.04f * std::sin(a * 5.0f + i); }));
        m.loft(r, false, true);
    }

    // Hand. Frame: X toward the fingers, +Y thumb side, +Z back of the hand; wrist joint at the origin.
    m.bone = base + AB_HAND;
    m.mat = st.glove;
    {
        // Glove cuff over the wrist.
        std::vector<Ring> r;
        r.push_back(section(-2.1f, 1.22f, 0.86f, 0.84f, 2.2f, 18));
        r.push_back(section(-1.9f, 1.3f, 0.93f, 0.9f, 2.2f, 18));
        r.push_back(section(-0.6f, 1.28f, 0.88f, 0.86f, 2.2f, 18));
        r.push_back(section(0.2f, 1.24f, 0.66f, 0.72f, 2.4f, 18));
        m.loft(r, true, false);
        m.mat = st.pad;
        m.box({-1.25f, 0.1f, 1.02f}, {0.55f, 0.9f, 0.1f}, 0.08f);  // strap tab
    }
    m.mat = st.glove;
    {
        // Palm: wider toward the knuckles, thick heel, slightly arched back. The knuckle line is oblique.
        std::vector<Ring> r;
        struct S { float x, w, top, bot; };
        const S ss[] = {{-0.2f, 1.18f, 0.56f, 0.6f}, {0.9f, 1.36f, 0.52f, 0.62f}, {2.0f, 1.5f, 0.47f, 0.54f}, {2.9f, 1.56f, 0.42f, 0.44f}, {3.45f, 1.5f, 0.38f, 0.37f}};
        for (int i = 0; i < 5; i++) {
            Ring ring = section(ss[i].x, ss[i].w, ss[i].top, ss[i].bot, 2.8f, 20);
            if (i >= 3)
                for (auto& p : ring) p.x += 0.16f * (p.y / ss[i].w) - 0.1f * std::fabs(p.y / ss[i].w);
            r.push_back(ring);
        }
        Ring tipRing = section(3.75f, 1.36f, 0.3f, 0.28f, 2.6f, 20);
        for (auto& p : tipRing) p.x += 0.2f * (p.y / 1.36f) - 0.22f * sq(p.y / 1.36f);
        r.push_back(tipRing);
        m.loft(r, false, true);
    }
    // Thenar (thumb) and hypothenar pads on the palm side.
    m.sphere({1.25f, 0.82f, -0.28f}, {1.15f, 0.62f, 0.55f}, 14, 10);
    m.sphere({1.3f, -0.95f, -0.3f}, {1.1f, 0.42f, 0.45f}, 12, 8);
    const HandGeom& g = handGeom();
    // Knuckle heads on the back of the hand.
    for (int f = 0; f < 4; f++) m.sphere(g.knuckle[f] + vec3(-0.12f, 0, 0.12f), {0.36f, 0.34f, 0.3f}, 10, 7);
    if (st.knuckleArmor) {
        m.mat = st.armor;
        // Curved hard shell across the knuckles.
        for (int k = 0; k <= 6; k++) {
            float t = k / 6.0f, y = lerpf(1.3f, -1.25f, t);
            float x = 3.0f + 0.16f * (y / 1.3f) - 0.12f * sq(y / 1.3f);
            m.box({x, y, 0.62f - 0.05f * sq(y / 1.3f)}, {0.42f, 0.2f, 0.12f}, 0.08f);
        }
        m.box({1.9f, 0.1f, 0.62f}, {0.7f, 0.95f, 0.1f}, 0.08f);  // back-of-hand pad
    }
    // Fingers: phalanges bound to their own bones.
    const float rad[5][2] = {{0.37f, 0.31f}, {0.385f, 0.32f}, {0.365f, 0.305f}, {0.32f, 0.27f}, {0.46f, 0.36f}};
    for (int f = 0; f < 5; f++)
        for (int j = 0; j < 3; j++) {
            m.bone = base + AB_FINGER0 + f * 3 + j;
            m.mat = st.glove;
            float r0 = lerpf(rad[f][0], rad[f][1], j / 3.0f), r1 = lerpf(rad[f][0], rad[f][1], (j + 1) / 3.0f);
            if (f == 4 && j == 0) {  // thumb metacarpal: a fleshy wedge that blends into the thenar pad
                std::vector<Ring> rs;
                float L = g.len[4][0];
                rs.push_back(section(-0.3f, 0.5f, 0.45f, 0.5f, 2.2f, 12));
                rs.push_back(section(L * 0.3f, 0.56f, 0.5f, 0.52f, 2.2f, 12));
                rs.push_back(section(L * 0.75f, 0.5f, 0.45f, 0.45f, 2.2f, 12));
                rs.push_back(section(L + 0.1f, 0.4f, 0.38f, 0.36f, 2.1f, 12));
                m.loft(rs);
                continue;
            }
            phalanx(m, g.len[f][j], r0, r1, j == 2);
            if (st.knuckleArmor && j == 0 && f < 4) {
                m.mat = st.armor;
                m.box({g.len[f][j] * 0.45f, 0, r0 * 0.92f}, {g.len[f][j] * 0.3f, r0 * 0.72f, 0.07f}, 0.05f);
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
    if (team == 2) {  // CT: navy camo sleeves, black tactical gloves with knuckle armor
        st.sleeve = {{0.22f, 0.26f, 0.34f}, 0.88f, 0.0f, 3};
        st.cuff = {{0.19f, 0.22f, 0.29f}, 0.9f, 0.0f, 2};
        st.glove = {{0.15f, 0.15f, 0.155f}, 0.6f, 0.0f, 6};
        st.pad = {{0.14f, 0.14f, 0.15f}, 0.7f, 0.0f, 2};
        st.armor = {{0.1f, 0.1f, 0.105f}, 0.45f, 0.0f, 4};
        st.skin = {{0.72f, 0.54f, 0.43f}, 0.5f, 0.0f, 7};
        st.knuckleArmor = true;
        out.patternA = {0.14f, 0.16f, 0.22f};
        out.patternB = {0.34f, 0.38f, 0.46f};
    } else {  // T: sleeves rolled up, bare forearms, dark leather gloves
        st.sleeve = {{0.46f, 0.41f, 0.31f}, 0.9f, 0.0f, 3};
        st.cuff = {{0.4f, 0.35f, 0.26f}, 0.9f, 0.0f, 2};
        st.glove = {{0.17f, 0.15f, 0.13f}, 0.5f, 0.0f, 6};
        st.pad = {{0.16f, 0.12f, 0.09f}, 0.6f, 0.0f, 6};
        st.armor = st.pad;
        st.skin = {{0.6f, 0.47f, 0.4f}, 0.5f, 0.0f, 7};
        st.bareForearm = true;
        out.patternA = {0.36f, 0.31f, 0.22f};
        out.patternB = {0.58f, 0.53f, 0.41f};
    }
    // Bake occlusion on one arm, then mirror it: both arms share the same space in the bind pose.
    MeshBuilder right;
    buildArm(right, 0, st, out.upperLen, out.foreLen);
    right.bakeAO(2.2f, 48, 0.45f);
    MeshBuilder m = right;
    size_t base = m.verts.size();
    for (size_t i = 0; i < right.verts.size(); i++) {
        ModelVertex v = right.verts[i];
        v.pos.y = -v.pos.y;
        v.normal.y = -v.normal.y;
        uint32_t bone = (v.mat >> 24) + AB_PER_ARM;
        v.mat = (v.mat & 0x00FFFFFFu) | (bone << 24);
        m.verts.push_back(v);
    }
    for (size_t t = 0; t + 2 < right.idx.size(); t += 3)
        m.tri((uint32_t)base + right.idx[t], (uint32_t)base + right.idx[t + 2], (uint32_t)base + right.idx[t + 1]);
    out.mesh = m.upload(r);
}
