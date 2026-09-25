// Third-person agents: procedural T/CT soldiers, skeleton animation with arm/leg IK, hitboxes and a Verlet ragdoll.
#include "game/agent.h"

#include <algorithm>

#include "world/map.h"

namespace {

const vec3 kX(1, 0, 0), kY(0, 1, 0), kZ(0, 0, 1);

// Neutral skeleton in model space (feet at origin, +X forward, +Y left). Torso bones carry no rotation in this pose,
// so torso geometry is authored in model space and shifted into bone space.
const vec3 kPelvisPos(0, 0, 37.2f);
const vec3 kSpineOff(0, 0, 6.0f), kChestOff(0, 0, 7.4f), kNeckOff(-0.7f, 0, 7.8f), kHeadOff(0.7f, 0, 4.0f);
const vec3 kSpinePos = kPelvisPos + kSpineOff;
const vec3 kChestPos = kSpinePos + kChestOff;
const vec3 kNeckPos = kChestPos + kNeckOff;
const vec3 kHipOff(0.2f, 3.7f, -0.9f);       // from pelvis, y mirrored for the right side
const vec3 kShoulderOff(-0.3f, 6.6f, 5.9f);  // from chest
const float kThigh = 17.0f, kCalf = 16.4f, kUArm = 11.4f, kFArm = 10.4f, kAnkleZ = 3.1f;
const vec3 kHeadCenter(0.2f, 0, 2.6f);  // skull center in head space (hitbox, ragdoll particle)

struct Style {
    bool ct = false;
    Mat skin, lips, hair, eye, iris, pupil, shirt, pants, vest, pouch, strap, glove, knuckle, boot, sole, helmet, black, lens,
        cloth, scarf, patch;
};

// ---------------------------------------------------------------------------------------------------------------
// Geometry helpers

float crs(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
}

// Catmull-Rom through table rows, u in [0, rows - 1]; field selects the interpolated member.
template <class T, class F>
float spline(const std::vector<T>& rows, float u, F field) {
    int n = (int)rows.size();
    u = clampf(u, 0.0f, (float)(n - 1));
    int i = std::min((int)u, n - 2);
    float t = u - (float)i;
    auto at = [&](int k) { return field(rows[(size_t)std::clamp(k, 0, n - 1)]); };
    return crs(at(i - 1), at(i), at(i + 1), at(i + 2), t);
}

float sgnPow(float v, float e) { return std::copysign(std::pow(std::fabs(v), e), v); }
float gauss(float x, float s) { return std::exp(-(x * x) / (s * s)); }
float wrapPi(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a <= -kPi) a += kTwoPi;
    return a;
}

// Vertex grid, row-major with nu + 1 columns; closed grids repeat column 0 at column nu.
using Grid = std::vector<vec3>;

std::vector<vec3> gridNormals(const Grid& g, int nu, int nv, bool closedU, const vec3* ref) {
    int cols = nu + 1;
    auto P = [&](int i, int j) { return g[(size_t)j * cols + i]; };
    std::vector<vec3> rowC(nv + 1, vec3(0)), out(g.size());
    vec3 all(0);
    for (int j = 0; j <= nv; j++) {
        for (int i = 0; i < nu; i++) rowC[j] += P(i, j);
        rowC[j] /= (float)nu;
        all += rowC[j];
    }
    all /= (float)(nv + 1);
    for (int j = 0; j <= nv; j++)
        for (int i = 0; i <= nu; i++) {
            int il = i - 1, ir = i + 1;
            if (closedU) {
                if (il < 0) il = nu - 1;
                if (ir > nu) ir = 1;
            } else {
                il = std::max(il, 0);
                ir = std::min(ir, nu);
            }
            vec3 n = cross(P(ir, j) - P(il, j), P(i, std::min(j + 1, nv)) - P(i, std::max(j - 1, 0)));
            vec3 away = ref ? P(i, j) - *ref : P(i, j) - rowC[j];
            if (length2(away) < 1e-8f) away = P(i, j) - all;
            if (length2(n) < 1e-12f) n = away;
            if (dot(n, away) < 0) n = -n;
            out[(size_t)j * cols + i] = normalize(n);
        }
    return out;
}

template <class C, class K>
void emitGrid(MeshBuilder& m, const Grid& g, int nu, int nv, bool closedU, C color, K keep, const vec3* ref = nullptr) {
    std::vector<vec3> nrm = gridNormals(g, nu, nv, closedU, ref);
    int cols = nu + 1;
    uint32_t base = (uint32_t)m.verts.size();
    vec3 saved = m.mat.color;
    for (int j = 0; j <= nv; j++)
        for (int i = 0; i <= nu; i++) {
            m.mat.color = color(i, j);
            m.vtx(g[(size_t)j * cols + i], nrm[(size_t)j * cols + i]);
        }
    m.mat.color = saved;
    for (int j = 0; j < nv; j++)
        for (int i = 0; i < nu; i++)
            if (keep(i, j)) {
                uint32_t a = base + (uint32_t)(j * cols + i);
                m.quad(a, a + 1, a + 1 + (uint32_t)cols, a + (uint32_t)cols);
            }
}

void emitGrid(MeshBuilder& m, const Grid& g, int nu, int nv, bool closedU, const vec3* ref = nullptr) {
    vec3 c = m.mat.color;
    emitGrid(m, g, nu, nv, closedU, [c](int, int) { return c; }, [](int, int) { return true; }, ref);
}

// Flat fan over grid row j of a closed grid.
void capRow(MeshBuilder& m, const Grid& g, int nu, int j, vec3 n) {
    int cols = nu + 1;
    vec3 c(0);
    for (int i = 0; i < nu; i++) c += g[(size_t)j * cols + i];
    c /= (float)nu;
    uint32_t ci = m.vtx(c, n), b0 = (uint32_t)m.verts.size();
    for (int i = 0; i < nu; i++) m.vtx(g[(size_t)j * cols + i], n);
    for (int i = 0; i < nu; i++) m.tri(ci, b0 + (uint32_t)i, b0 + (uint32_t)((i + 1) % nu));
}

void ellipsoid(MeshBuilder& m, vec3 c, vec3 r, quat q = quat(), int seg = 14, int rings = 9) {
    mat4 prev = m.xform();
    m.setXform(prev * toMat4(Xform(c, q)));
    m.sphere(vec3(0), r, seg, rings);
    m.setXform(prev);
}

void boxq(MeshBuilder& m, vec3 c, vec3 h, quat q, float bevel) {
    mat4 prev = m.xform();
    m.setXform(prev * toMat4(Xform(c, q)));
    m.box(vec3(0), h, bevel);
    m.setXform(prev);
}

// Rounded-rectangle section (hw across the path, hh along 'up') swept along a polyline.
void sweep(MeshBuilder& m, const std::vector<vec3>& path, const std::vector<vec3>& up, float hw, float hh, float rad, bool caps = true) {
    std::vector<Ring> rings;
    size_t n = path.size();
    for (size_t i = 0; i < n; i++) {
        vec3 T = normalize(path[std::min(i + 1, n - 1)] - path[i > 0 ? i - 1 : 0]);
        vec3 U = up.size() == 1 ? up[0] : up[i];
        vec3 S = normalize(cross(T, U));
        U = cross(S, T);
        rings.push_back(ringRoundRect(path[i], S, U, hw, hh, rad, 2));
    }
    m.loft(rings, caps, caps);
}

// Round tube along a polyline with a parallel-transported frame (no twisting).
void tube(MeshBuilder& m, const std::vector<vec3>& path, float r, int seg = 8, bool caps = true) {
    std::vector<Ring> rings;
    size_t n = path.size();
    vec3 S(0);
    for (size_t i = 0; i < n; i++) {
        vec3 T = normalize(path[std::min(i + 1, n - 1)] - path[i > 0 ? i - 1 : 0]);
        S = i == 0 ? anyPerp(T) : normalize(S - T * dot(S, T));
        rings.push_back(ringEllipse(path[i], S, cross(T, S), r, r, seg));
    }
    m.loft(rings, caps, caps);
}

// Tube along +X through Catmull-Rom interpolated elliptical rings; 'wrinkle' adds cloth folds.
struct LRing { float x, ry, rz, oy, oz; };
void limb(MeshBuilder& m, const std::vector<LRing>& rs, int seg, int sub, float wrinkle = 0.0f, float phase = 0.0f) {
    int nv = (int)(rs.size() - 1) * sub;
    Grid g;
    for (int j = 0; j <= nv; j++) {
        float u = (float)j / sub;
        float x = spline(rs, u, [](const LRing& r) { return r.x; }), ry = spline(rs, u, [](const LRing& r) { return r.ry; });
        float rz = spline(rs, u, [](const LRing& r) { return r.rz; }), oy = spline(rs, u, [](const LRing& r) { return r.oy; });
        float oz = spline(rs, u, [](const LRing& r) { return r.oz; });
        for (int i = 0; i <= seg; i++) {
            float a = kTwoPi * (float)(i % seg) / seg;
            float f = 1.0f + wrinkle * std::sin(a * 3.0f + x * 1.3f + phase) * std::sin(x * 1.9f + phase * 1.7f);
            g.push_back({x, oy + std::cos(a) * ry * f, oz + std::sin(a) * rz * f});
        }
    }
    emitGrid(m, g, seg, nv, true);
    capRow(m, g, seg, 0, -kX);
    capRow(m, g, seg, nv, kX);
}

// Torso cross-sections: superellipse with separate front/back depth; model-space heights.
struct Sec { float z, cx, w, df, db, n; };
using Secs = std::vector<Sec>;

Sec secAt(const Secs& t, float u) {
    return {spline(t, u, [](const Sec& s) { return s.z; }), spline(t, u, [](const Sec& s) { return s.cx; }),
            spline(t, u, [](const Sec& s) { return s.w; }), spline(t, u, [](const Sec& s) { return s.df; }),
            spline(t, u, [](const Sec& s) { return s.db; }), spline(t, u, [](const Sec& s) { return s.n; })};
}

Sec secAtZ(const Secs& t, float z) {
    float lo = 0, hi = (float)(t.size() - 1);
    for (int k = 0; k < 22; k++) {
        float mid = (lo + hi) * 0.5f;
        (secAt(t, mid).z < z ? lo : hi) = mid;
    }
    Sec s = secAt(t, (lo + hi) * 0.5f);
    s.z = z;
    return s;
}

vec3 secPoint(const Sec& s, float th, float grow) {
    float c = std::cos(th), sn = std::sin(th), e = 2.0f / s.n;
    return {s.cx + sgnPow(c, e) * ((c >= 0 ? s.df : s.db) + grow), sgnPow(sn, e) * (s.w + grow), s.z};
}

// Frame on a torso surface: X out of the surface (horizontal), Y along increasing angle, Z up.
Xform surfaceFrame(const Secs& t, float th, float z, float off) {
    Sec s = secAtZ(t, z);
    vec3 p = secPoint(s, th, off), p2 = secPoint(s, th + 0.01f, off);
    vec3 n = cross(normalize(vec3(p2.x - p.x, p2.y - p.y, 0)), kZ);
    return Xform(p, qaxis(kZ, std::atan2(n.y, n.x)));
}

template <class G>
void torsoLoft(MeshBuilder& m, const Secs& t, int seg, int sub, G grow, bool capBottom = true, bool capTop = true) {
    int nv = (int)(t.size() - 1) * sub;
    Grid g;
    for (int j = 0; j <= nv; j++) {
        Sec s = secAt(t, (float)j / sub);
        for (int i = 0; i <= seg; i++) {
            float th = kTwoPi * (float)(i % seg) / seg;
            g.push_back(secPoint(s, th, grow(th, s.z)));
        }
    }
    emitGrid(m, g, seg, nv, true);
    if (capBottom) capRow(m, g, seg, 0, -kZ);
    if (capTop) capRow(m, g, seg, nv, kZ);
}

// Curved slab over a torso surface between offsets [inner, outer]; arc(z) gives the angular span.
template <class A>
void torsoSlab(MeshBuilder& m, const Secs& t, float z0, float z1, int rows, int k, float inner, float outer, A arc) {
    int nu = 2 * (k + 1);
    Grid g;
    for (int j = 0; j <= rows; j++) {
        float z = lerpf(z0, z1, (float)j / rows);
        Sec s = secAtZ(t, z);
        vec2 a = arc(z);
        size_t row0 = g.size();
        for (int i = 0; i <= k; i++) g.push_back(secPoint(s, lerpf(a.x, a.y, (float)i / k), outer));
        for (int i = k; i >= 0; i--) g.push_back(secPoint(s, lerpf(a.x, a.y, (float)i / k), inner));
        g.push_back(g[row0]);
    }
    emitGrid(m, g, nu, rows, true);
    for (int e = 0; e < 2; e++) {
        size_t row = (size_t)(e ? rows : 0) * (nu + 1);
        vec3 n = e ? kZ : -kZ;
        uint32_t b0 = (uint32_t)m.verts.size();
        for (int i = 0; i < nu; i++) m.vtx(g[row + i], n);
        for (int i = 0; i < k; i++) m.quad(b0 + i, b0 + i + 1, b0 + (uint32_t)(nu - 2 - i), b0 + (uint32_t)(nu - 1 - i));
    }
}

// Closed band around a torso table (belts, straps, collars) with rounded edges.
void band(MeshBuilder& m, const Secs& t, float z0, float z1, float grow, int seg = 40) {
    std::vector<Ring> rings;
    const float zs[4] = {z0, z0 + 0.22f, z1 - 0.22f, z1};
    const float gs[4] = {grow - 0.25f, grow, grow, grow - 0.25f};
    for (int k = 0; k < 4; k++) {
        Sec s = secAtZ(t, zs[k]);
        Ring r;
        for (int i = 0; i < seg; i++) r.push_back(secPoint(s, kTwoPi * (float)i / seg, gs[k]));
        rings.push_back(r);
    }
    m.loft(rings, false, false);
}

// ---------------------------------------------------------------------------------------------------------------
// Body proportions (model space). Pelvis/spine/chest parts overlap generously so bending never opens gaps.

const Secs kPelvisSecs = {
    {31.9f, 0.1f, 3.6f, 2.2f, 2.5f, 2.0f},   {33.3f, 0.1f, 5.6f, 3.3f, 3.8f, 2.2f},  {35.2f, 0.1f, 6.6f, 3.75f, 4.35f, 2.4f},
    {37.4f, 0.1f, 6.95f, 3.95f, 4.55f, 2.5f}, {39.6f, 0.2f, 6.7f, 4.0f, 4.25f, 2.4f}, {41.8f, 0.3f, 6.3f, 4.05f, 3.95f, 2.3f},
    {43.4f, 0.35f, 6.0f, 3.95f, 3.7f, 2.3f},
};
const Secs kSpineSecs = {
    {39.8f, 0.3f, 6.15f, 3.9f, 3.85f, 2.3f}, {42.4f, 0.35f, 6.0f, 3.95f, 3.75f, 2.3f}, {45.0f, 0.4f, 6.15f, 4.15f, 3.8f, 2.35f},
    {47.6f, 0.4f, 6.4f, 4.35f, 3.9f, 2.4f},  {49.6f, 0.4f, 6.5f, 4.4f, 3.95f, 2.4f},   {51.6f, 0.4f, 6.35f, 4.3f, 3.8f, 2.4f},
};
const Secs kJacketLowSecs = {  // T jacket: flared hem over the belt
    {38.4f, 0.25f, 7.2f, 4.45f, 4.8f, 2.4f}, {39.0f, 0.25f, 7.15f, 4.45f, 4.75f, 2.4f}, {41.0f, 0.3f, 6.7f, 4.3f, 4.3f, 2.35f},
    {43.4f, 0.35f, 6.45f, 4.3f, 4.1f, 2.3f}, {46.0f, 0.4f, 6.6f, 4.5f, 4.15f, 2.35f},   {48.6f, 0.4f, 6.85f, 4.7f, 4.25f, 2.4f},
    {51.6f, 0.4f, 6.7f, 4.6f, 4.1f, 2.4f},
};
const Secs kChestSecs = {
    {48.6f, 0.4f, 6.65f, 4.6f, 4.05f, 2.45f}, {50.8f, 0.38f, 6.95f, 4.85f, 4.15f, 2.5f}, {53.0f, 0.3f, 7.1f, 4.9f, 4.2f, 2.55f},
    {54.9f, 0.15f, 7.05f, 4.6f, 4.2f, 2.6f},  {56.4f, -0.05f, 6.6f, 4.0f, 3.95f, 2.6f},  {57.5f, -0.25f, 5.5f, 3.35f, 3.6f, 2.4f},
    {58.5f, -0.4f, 4.1f, 2.75f, 3.1f, 2.2f},  {59.3f, -0.5f, 2.95f, 2.4f, 2.6f, 2.0f},
};
// Shirt surface from waist to neck, used to fit gear.
const Secs kTorsoSecs = {
    {42.4f, 0.35f, 6.0f, 3.95f, 3.75f, 2.3f}, {45.0f, 0.4f, 6.15f, 4.15f, 3.8f, 2.35f}, {47.2f, 0.4f, 6.42f, 4.38f, 3.95f, 2.4f},
    {48.9f, 0.4f, 6.7f, 4.64f, 4.07f, 2.45f}, {50.8f, 0.38f, 6.95f, 4.85f, 4.15f, 2.5f}, {53.0f, 0.3f, 7.1f, 4.9f, 4.2f, 2.55f},
    {54.9f, 0.15f, 7.05f, 4.6f, 4.2f, 2.6f},  {56.4f, -0.05f, 6.6f, 4.0f, 3.95f, 2.6f},  {57.5f, -0.25f, 5.5f, 3.35f, 3.6f, 2.4f},
};
const Secs kCollarSecs = {{57.2f, -0.45f, 3.35f, 3.0f, 3.3f, 2.1f}, {60.3f, -0.3f, 2.95f, 2.7f, 2.95f, 2.0f}};

// Limb rings in bone space (X along the bone). Upper arm/thigh Z faces the bend pole, forearm Z the back of the hand.
const std::vector<LRing> kUpperArm = {
    {-1.5f, 2.45f, 2.45f, 0, 0}, {0.8f, 2.58f, 2.52f, 0, 0}, {3.5f, 2.38f, 2.43f, 0, -0.12f},
    {6.5f, 2.2f, 2.32f, 0, -0.15f}, {9.2f, 2.02f, 2.1f, 0, 0}, {11.6f, 1.9f, 1.92f, 0, 0.1f},
};
const std::vector<LRing> kForearm = {
    {-1.2f, 2.05f, 2.0f, 0, 0}, {1.6f, 2.18f, 2.05f, 0, 0.05f}, {4.6f, 1.95f, 1.8f, 0, 0}, {7.4f, 1.68f, 1.5f, 0, 0}, {9.4f, 1.55f, 1.38f, 0, 0},
};
const std::vector<LRing> kThighRings = {
    {-2.6f, 3.8f, 3.7f, 0, -0.4f}, {0.6f, 4.1f, 4.05f, 0, 0}, {4.2f, 3.95f, 3.9f, 0, 0.15f}, {8.2f, 3.62f, 3.5f, 0, 0.12f},
    {12.2f, 3.22f, 3.1f, 0, 0}, {15.6f, 2.95f, 2.9f, 0, 0.05f}, {17.6f, 2.85f, 2.9f, 0, 0.12f},
};
const std::vector<LRing> kCalfRings = {  // loose trouser leg, bloused over the boot top
    {-1.8f, 2.8f, 2.85f, 0, 0.15f}, {1.2f, 2.92f, 3.02f, 0, -0.1f}, {4.5f, 2.98f, 3.12f, 0, -0.25f}, {8.0f, 2.82f, 2.92f, 0, -0.15f},
    {11.0f, 2.66f, 2.72f, 0, -0.05f}, {12.6f, 2.78f, 2.84f, 0, 0}, {13.6f, 2.62f, 2.66f, 0, 0.05f}, {13.9f, 2.25f, 2.3f, 0, 0.05f},
};

// ---------------------------------------------------------------------------------------------------------------
// Head: rows of superellipses (head space, pivot at the skull base) plus Gaussian facial relief.

struct HRow { float z, f, b, w, cx, nf; };
const std::vector<HRow> kHeadRows = {
    {-3.0f, 1.2f, 1.8f, 1.6f, 0.55f, 2.0f},  {-2.5f, 2.15f, 2.05f, 1.9f, 0.45f, 1.8f}, {-1.75f, 2.72f, 2.25f, 2.15f, 0.32f, 1.65f},
    {-0.85f, 3.02f, 2.35f, 2.36f, 0.2f, 1.8f}, {0.2f, 3.2f, 2.55f, 2.6f, 0.1f, 2.1f},   {1.2f, 3.28f, 3.05f, 2.86f, 0.0f, 2.35f},
    {2.25f, 3.18f, 3.55f, 3.0f, -0.1f, 2.5f}, {3.2f, 3.42f, 3.82f, 3.05f, -0.2f, 2.6f},  {4.35f, 3.4f, 3.92f, 3.0f, -0.3f, 2.5f},
    {5.45f, 2.95f, 3.55f, 2.68f, -0.36f, 2.3f}, {6.3f, 2.1f, 2.62f, 1.98f, -0.42f, 2.1f}, {6.85f, 1.05f, 1.35f, 1.0f, -0.45f, 2.0f},
    {7.05f, 0.12f, 0.15f, 0.12f, -0.45f, 2.0f},
};
struct Feat { float th, z, amp, sth, sz; };
const Feat kFeats[] = {  // mirrored across the midline when th != 0
    {0.40f, 2.3f, -0.3f, 0.22f, 0.45f},  {0.42f, 3.15f, 0.12f, 0.34f, 0.26f}, {0.0f, 2.55f, 0.12f, 0.12f, 0.6f},
    {0.78f, 1.75f, 0.18f, 0.3f, 0.45f},  {0.95f, 0.45f, -0.1f, 0.3f, 0.45f},  {0.0f, 0.55f, 0.13f, 0.28f, 0.22f},
    {0.0f, -0.12f, 0.11f, 0.25f, 0.2f},  {0.0f, -1.45f, 0.16f, 0.32f, 0.35f}, {1.25f, 3.7f, -0.07f, 0.3f, 0.5f},
};

float headDisp(float th, float z) {
    float d = 0;
    for (const Feat& f : kFeats) {
        float gz = gauss(z - f.z, f.sz);
        d += f.amp * gauss(th - f.th, f.sth) * gz;
        if (f.th != 0.0f) d += f.amp * gauss(th + f.th, f.sth) * gz;
    }
    return d;
}

vec3 headPoint(float th, float u, bool features) {
    const auto& R = kHeadRows;
    float z = spline(R, u, [](const HRow& r) { return r.z; }), f = spline(R, u, [](const HRow& r) { return r.f; });
    float b = spline(R, u, [](const HRow& r) { return r.b; }), w = spline(R, u, [](const HRow& r) { return r.w; });
    float cx = spline(R, u, [](const HRow& r) { return r.cx; }), nf = spline(R, u, [](const HRow& r) { return r.nf; });
    float c = std::cos(th), s = std::sin(th), e = 2.0f / (c >= 0 ? nf : 2.0f);
    vec3 p(cx + sgnPow(c, e) * (c >= 0 ? f : b), sgnPow(s, e) * w, z);
    if (features) p += normalize(vec3(p.x - cx, p.y, 0)) * headDisp(wrapPi(th), z);
    return p;
}

float headU(float z) {
    float lo = 0, hi = (float)(kHeadRows.size() - 1);
    for (int k = 0; k < 22; k++) {
        float mid = (lo + hi) * 0.5f;
        (spline(kHeadRows, mid, [](const HRow& r) { return r.z; }) < z ? lo : hi) = mid;
    }
    return (lo + hi) * 0.5f;
}

// Helmet rim height around the head (a = |angle from the face|): brow line, high cut over the ears, low at the back.
float helmetRimZ(float a) {
    struct K { float a, z; };
    static const std::vector<K> k = {{0.0f, 4.55f}, {0.7f, 4.3f},  {1.05f, 4.05f}, {1.45f, 3.95f},
                                     {1.9f, 3.7f},  {2.35f, 2.6f}, {2.8f, 1.75f},  {kPi, 1.55f}};
    float lo = 0, hi = (float)(k.size() - 1);
    for (int it = 0; it < 20; it++) {
        float mid = (lo + hi) * 0.5f;
        (spline(k, mid, [](const K& e) { return e.a; }) < a ? lo : hi) = mid;
    }
    return spline(k, (lo + hi) * 0.5f, [](const K& e) { return e.z; });
}

// Eyeball with iris and pupil, covered by upper and lower lids that leave an almond-shaped opening.
void buildEye(MeshBuilder& m, const Style& s, float sd) {
    const vec3 c(2.36f, sd * 1.2f, 2.3f);
    const float r = 0.44f;
    m.mat = s.eye;
    m.sphere(c, vec3(r), 14, 10);
    m.mat = s.iris;
    ellipsoid(m, c + vec3(r - 0.035f, 0, 0), {0.06f, 0.19f, 0.19f}, quat(), 12, 6);
    m.mat = s.pupil;
    ellipsoid(m, c + vec3(r + 0.004f, 0, 0), {0.02f, 0.08f, 0.08f}, quat(), 10, 5);
    // Lids in the eye's spherical coordinates (hx yaw, hy pitch), the frame turned slightly outward like the face.
    const quat fq = qaxis(kZ, sd * 0.22f);
    const int nu = 22, nv = 7;
    for (int lid = 0; lid < 2; lid++) {
        bool upper = lid == 0;
        Grid g;
        std::vector<float> rowV;
        for (int j = 0; j <= nv; j++) {
            float v = (float)j / nv;
            for (int i = 0; i <= nu; i++) {
                float hx = lerpf(-1.25f, 1.25f, (float)i / nu);
                float open = std::pow(std::max(0.0f, 1.0f - (hx / 0.95f) * (hx / 0.95f)), 0.8f);
                float edge = upper ? 0.33f * open + 0.02f : -(0.19f * open + 0.02f);
                float far = upper ? 1.35f : -1.2f;
                // Row 0 tucks against the eyeball so the lid margin looks thick.
                float hy = j == 0 ? edge : lerpf(edge + (upper ? 0.03f : -0.03f), far, std::pow((v - 1.0f / nv) / (1.0f - 1.0f / nv), 1.3f));
                float rad = j == 0 ? r - 0.01f : r + 0.045f + 0.1f * v * v;
                vec3 d(std::cos(hy) * std::cos(hx), std::cos(hy) * std::sin(hx), std::sin(hy));
                g.push_back(c + rotate(fq, d) * rad);
            }
        }
        vec3 skin = s.skin.color, lash = upper ? s.hair.color : s.skin.color * 0.72f;
        m.mat = s.skin;
        emitGrid(m, g, nu, nv, false, [&](int, int j) { return j <= 1 ? lash : lerp(skin * 0.82f, skin, saturate((j - 1) / 2.0f)); },
                 [](int, int) { return true; }, &c);
    }
}

void buildHead(MeshBuilder& m, const Style& s) {
    m.resetXform();
    m.bone = AG_HEAD;
    const int nu = 56, nv = 60;
    const float umax = (float)(kHeadRows.size() - 1);
    Grid g;
    std::vector<float> th(nu + 1), zs(nv + 1);
    for (int i = 0; i <= nu; i++) th[i] = wrapPi(kTwoPi * (float)(i % nu) / nu);
    for (int j = 0; j <= nv; j++) {
        float u = umax * (float)j / nv;
        zs[j] = spline(kHeadRows, u, [](const HRow& r) { return r.z; });
        for (int i = 0; i <= nu; i++) g.push_back(headPoint(th[i], u, true));
    }
    // Skin tone: lips, mouth line, brows, darker sockets; CT adds stubble and short hair below the helmet.
    auto skinCol = [&](int i, int j) {
        float a = th[i], aa = std::fabs(a), z = zs[j];
        vec3 c = s.skin.color;
        float lips = gauss(a, 0.3f) * gauss(z - 0.22f, 0.36f);
        c = lerp(c, s.lips.color, 0.6f * lips);
        c = lerp(c, c * 0.5f, gauss(a, 0.26f) * gauss(z - 0.22f, 0.07f));
        c = lerp(c, c * vec3(0.68f, 0.6f, 0.58f), 0.55f * gauss(aa - 0.4f, 0.2f) * gauss(z - 2.3f, 0.45f));
        float brow = gauss(aa - 0.44f, 0.22f) * gauss(z - 3.3f, 0.15f) * saturate((aa - 0.1f) / 0.08f);
        c = lerp(c, s.hair.color, 0.85f * brow);
        if (s.ct) {
            float stub = saturate((1.0f - z) / 1.2f) * saturate((1.9f - aa) / 0.4f) * (1.0f - lips);
            c = lerp(c, lerp(c, s.hair.color, 0.5f), 0.5f * stub);
            float hair = std::max(saturate((aa - 1.3f) / 0.15f) * saturate((z - 2.0f) / 0.4f), saturate((z - 4.8f) / 0.3f));
            c = lerp(c, s.hair.color, hair);
        }
        return c;
    };
    m.mat = s.skin;
    emitGrid(m, g, nu, nv, true, skinCol, [](int, int) { return true; });
    capRow(m, g, nu, nv, kZ);
    for (float sd : {-1.0f, 1.0f}) buildEye(m, s, sd);
    if (s.ct) {
        // Nose, then helmet, headset and glasses.
        m.mat = s.skin;
        ellipsoid(m, {3.02f, 0, 2.04f}, {0.3f, 0.26f, 0.85f}, qaxis(kY, -0.28f), 12, 10);
        ellipsoid(m, {3.4f, 0, 1.34f}, {0.31f, 0.32f, 0.3f}, quat(), 12, 8);
        for (float sd : {-1.0f, 1.0f}) ellipsoid(m, {3.2f, sd * 0.42f, 1.2f}, {0.28f, 0.25f, 0.23f}, quat(), 10, 7);

        const vec3 hc(-0.3f, 0, 3.0f), hr(4.45f, 3.9f, 5.0f);
        const int hu = 64, hv = 14;
        Grid hg;
        for (int j = 0; j <= hv; j++)
            for (int i = 0; i <= hu; i++) {
                float a = kTwoPi * (float)(i % hu) / hu;
                float phiR = std::acos(clampf((helmetRimZ(std::fabs(wrapPi(a))) - hc.z) / hr.z, -1.0f, 1.0f));
                float phi = phiR * (1.0f - (float)j / hv);
                hg.push_back(hc + vec3(hr.x * std::sin(phi) * std::cos(a), hr.y * std::sin(phi) * std::sin(a), hr.z * std::cos(phi)));
            }
        m.mat = s.helmet;
        emitGrid(m, hg, hu, hv, true, &hc);
        // Rubber edge trim along the rim.
        m.mat = s.black;
        std::vector<vec3> rim;
        for (int i = 0; i <= hu; i++) rim.push_back(hc + (hg[(size_t)i] - hc) * 0.985f);
        tube(m, rim, 0.2f, 6, false);
        // Side rails, NVG shroud, velcro and strobe.
        auto shellAt = [&](float a, float z, float off) {
            float phi = std::acos(clampf((z - hc.z) / hr.z, -1.0f, 1.0f));
            vec3 p = hc + vec3(hr.x * std::sin(phi) * std::cos(a), hr.y * std::sin(phi) * std::sin(a), hr.z * std::cos(phi));
            vec3 n = normalize(vec3((p.x - hc.x) / (hr.x * hr.x), (p.y - hc.y) / (hr.y * hr.y), (p.z - hc.z) / (hr.z * hr.z)));
            return p + n * off;
        };
        for (float sd : {-1.0f, 1.0f}) {
            std::vector<vec3> path, up;
            for (int k = 0; k <= 8; k++) {
                float a = sd * lerpf(0.95f, 2.2f, k / 8.0f);
                path.push_back(shellAt(a, 4.5f, 0.16f));
                up.push_back(normalize(shellAt(a, 4.5f, 1.0f) - shellAt(a, 4.5f, 0.0f)));
            }
            sweep(m, path, up, 0.36f, 0.2f, 0.08f);
        }
        boxq(m, shellAt(0.0f, 5.45f, 0.12f), {0.3f, 0.75f, 0.55f}, qaxis(kY, -0.46f), 0.12f);
        boxq(m, shellAt(0.0f, 5.0f, 0.3f), {0.18f, 0.35f, 0.25f}, qaxis(kY, -0.4f), 0.06f);
        boxq(m, shellAt(kPi, 4.7f, 0.15f), {0.25f, 0.45f, 0.4f}, qaxis(kY, 0.2f), 0.1f);
        m.mat = s.patch;
        ellipsoid(m, {-1.0f, 0, 7.82f}, {1.95f, 1.5f, 0.28f}, qaxis(kY, 0.1f), 16, 6);
        // Headset: cushions, cups, band arms to the rails, boom mic on the right.
        for (float sd : {-1.0f, 1.0f}) {
            m.mat = s.black;
            ellipsoid(m, {-0.35f, sd * 3.15f, 1.9f}, {1.22f, 0.5f, 1.45f}, quat(), 14, 8);
            m.mat = s.pouch;
            ellipsoid(m, {-0.35f, sd * 3.6f, 1.9f}, {1.3f, 0.62f, 1.55f}, quat(), 16, 9);
            m.mat = s.black;
            m.cylinder({-0.35f, sd * 4.1f, 1.9f}, {-0.35f, sd * 4.28f, 1.9f}, 0.55f, 0.5f, 12);
            boxq(m, {-0.35f, sd * 3.9f, 3.85f}, {0.28f, 0.13f, 0.75f}, qaxis(kX, -sd * 0.1f), 0.08f);
        }
        tube(m, {{0.4f, -3.9f, 1.35f}, {1.4f, -3.55f, 0.75f}, {2.4f, -2.75f, 0.35f}, {3.1f, -1.75f, 0.25f}}, 0.1f, 6);
        ellipsoid(m, {3.3f, -1.45f, 0.25f}, {0.36f, 0.3f, 0.28f}, quat(), 10, 6);
        // Wraparound ballistic glasses.
        const int gu = 30, gv = 6;
        Grid lg;
        for (int j = 0; j <= gv; j++)
            for (int i = 0; i <= gu; i++) {
                float a = lerpf(-1.3f, 1.3f, (float)i / gu);
                float zb = 1.72f + 0.62f * gauss(a, 0.26f);
                float z = lerpf(zb, 3.02f, (float)j / gv);
                vec3 p = headPoint(a, headU(z), false);
                float off = lerpf(0.62f, 0.3f, std::fabs(a) / 1.3f);
                lg.push_back(p + normalize(vec3(p.x + 0.3f, p.y, 0)) * off);
            }
        m.mat = s.lens;
        vec3 ref(-2.5f, 0, 2.4f);
        emitGrid(m, lg, gu, gv, false, &ref);
        m.mat = s.black;
        std::vector<vec3> top;
        for (int i = 0; i <= gu; i += 2) top.push_back(lg[(size_t)gv * (gu + 1) + i] + vec3(0, 0, 0.04f));
        tube(m, top, 0.1f, 6);
        for (float sd : {-1.0f, 1.0f}) {
            vec3 e = lg[(size_t)(gv - 1) * (gu + 1) + (sd > 0 ? gu : 0)];
            tube(m, {e, vec3(0.9f, sd * 3.1f, 2.75f), vec3(0.0f, sd * 3.2f, 2.7f)}, 0.08f, 6);
        }
    } else {
        // Knitted balaclava over the whole head; the fabric dives under the skin inside the eye opening.
        std::vector<vec3> nrm = gridNormals(g, nu, nv, true, nullptr);
        Grid b = g;
        for (int j = 0; j <= nv; j++)
            for (int i = 0; i <= nu; i++) {
                float a = th[i], z = zs[j];
                float hole = std::pow(std::pow(std::fabs(a) / 0.8f, 4.0f) + std::pow(std::fabs(z - 2.32f) / 0.64f, 4.0f), 0.25f);
                float off = 0.14f + 0.08f * gauss(hole - 1.05f, 0.1f) - 0.55f * saturate((1.0f - hole) / 0.12f);
                off += 0.62f * gauss(a, 0.2f) * gauss(z - 1.5f, 0.5f);  // nose under the fabric
                off += 0.1f * gauss(a, 0.4f) * gauss(z - 0.2f, 0.4f);
                off += 0.3f * gauss(std::fabs(a) - 1.62f, 0.2f) * gauss(z - 1.9f, 0.7f);  // ears
                size_t k = (size_t)j * (nu + 1) + i;
                b[k] += nrm[k] * off;
            }
        m.mat = s.cloth;
        emitGrid(m, b, nu, nv, true);
        capRow(m, b, nu, nv, kZ);
    }
}

void buildNeck(MeshBuilder& m, const Style& s) {
    m.bone = AG_NECK;
    m.setXform(translate(-kNeckPos));
    m.mat = s.ct ? s.skin : s.cloth;
    std::vector<Ring> r;
    r.push_back(ringEllipse({-0.5f, 0, 57.2f}, kX, kY, 2.55f, 2.75f, 20));
    r.push_back(ringEllipse({-0.25f, 0, 59.4f}, kX, kY, 2.3f, 2.35f, 20));
    r.push_back(ringEllipse({0.1f, 0, 61.4f}, kX, kY, 2.2f, 2.2f, 20));
    r.push_back(ringEllipse({0.3f, 0, 63.6f}, kX, kY, 2.1f, 2.05f, 20));
    m.loft(r);
    m.resetXform();
}

// Right hand in hand space (X to the fingers, Z dorsal, Y thumb side); the left hand is its mirror image.
void buildHand(MeshBuilder& m, bool left, const Style& s) {
    int f0 = left ? AG_FINGERS_L : AG_FINGERS_R;
    m.setXform(left ? scale(vec3(1, -1, 1)) : mat4());
    m.bone = left ? AG_HAND_L : AG_HAND_R;
    m.mat = s.glove;
    limb(m, {{-2.5f, 1.66f, 1.5f, 0, 0}, {-1.6f, 1.7f, 1.52f, 0, 0}, {-0.6f, 1.5f, 1.2f, 0, 0}, {0.4f, 1.4f, 1.0f, 0, 0.02f}}, 14, 2);
    {
        std::vector<Ring> r;
        const float xs[] = {0.1f, 1.2f, 2.5f, 3.4f, 3.9f};
        const float hu[] = {1.2f, 1.46f, 1.62f, 1.62f, 1.44f}, hv[] = {0.6f, 0.66f, 0.62f, 0.53f, 0.4f};
        for (int i = 0; i < 5; i++) r.push_back(ringRoundRect({xs[i], 0.05f, 0.05f + (i >= 3 ? 0.03f : 0.0f)}, kY, kZ, hu[i], hv[i], 0.42f, 3));
        m.loft(r);
    }
    m.sphere({1.35f, 0.95f, -0.25f}, {1.05f, 0.62f, 0.62f}, 12, 8);
    if (s.ct) {
        m.mat = s.knuckle;
        m.box({3.2f, 0.02f, 0.5f}, {0.4f, 1.48f, 0.2f}, 0.14f);
        m.box({1.4f, 0.05f, 0.62f}, {0.9f, 1.05f, 0.1f}, 0.08f);
    }
    const HandGeom& g = handGeom();
    const float rad[5][2] = {{0.40f, 0.35f}, {0.41f, 0.36f}, {0.39f, 0.34f}, {0.35f, 0.30f}, {0.50f, 0.39f}};
    m.mat = s.glove;
    for (int f = 0; f < 5; f++)
        for (int j = 0; j < 3; j++) {
            m.bone = f0 + f * 3 + j;
            float r0 = lerpf(rad[f][0], rad[f][1], j / 3.0f), r1 = lerpf(rad[f][0], rad[f][1], (j + 1) / 3.0f);
            m.capsule({0, 0, 0}, {g.len[f][j] - r1 * 0.4f, 0, 0}, r0, r1, 8);
        }
    m.resetXform();
}

// Boot below the ankle in foot space (X forward, Z up, ground at z = -kAnkleZ).
void buildBootFoot(MeshBuilder& m, const Style& s) {
    const float g = -kAnkleZ;
    struct BR { float x, hw, zb, zt, r; };
    const BR br[] = {
        {-3.05f, 1.3f, 0.55f, 3.4f, 1.0f},  {-2.55f, 1.8f, 0.55f, 5.4f, 1.3f}, {-0.6f, 1.98f, 0.55f, 6.0f, 1.5f},
        {1.5f, 2.02f, 0.55f, 5.3f, 1.5f},   {3.6f, 2.08f, 0.55f, 3.9f, 1.4f},  {5.6f, 2.05f, 0.55f, 3.05f, 1.25f},
        {7.2f, 1.9f, 0.55f, 2.6f, 1.05f},   {8.3f, 1.55f, 0.55f, 2.2f, 0.8f},  {8.9f, 0.95f, 0.6f, 1.7f, 0.45f},
    };
    std::vector<Ring> rings;
    for (const BR& b : br) {
        float hv = (b.zt - b.zb) * 0.5f;
        rings.push_back(ringRoundRect({b.x, 0, g + (b.zb + b.zt) * 0.5f}, kY, kZ, b.hw, hv, std::min(b.r, hv * 0.98f), 3));
    }
    m.mat = s.boot;
    m.loft(rings);
    m.mat = s.strap;
    boxq(m, {3.0f, 0, g + 4.55f}, {1.8f, 0.7f, 0.12f}, qaxis(kY, 0.55f), 0.1f);  // tongue and laces
    for (int k = 0; k < 4; k++) boxq(m, {1.6f + k * 0.75f, 0, g + 5.45f - k * 0.55f}, {0.1f, 0.85f, 0.08f}, qaxis(kY, 0.55f), 0.03f);
    m.mat = s.sole;
    {
        std::vector<Ring> r;  // rubber sole with toe spring, following the upper's footprint
        const float xs[] = {-3.3f, -2.6f, 1.5f, 5.6f, 7.9f, 9.0f}, hw[] = {1.4f, 2.0f, 2.1f, 2.15f, 1.8f, 0.9f};
        const float lift[] = {0.1f, 0.0f, 0.0f, 0.0f, 0.12f, 0.35f};
        for (int i = 0; i < 6; i++) r.push_back(ringRoundRect({xs[i], 0, g + 0.36f + lift[i]}, kY, kZ, hw[i], 0.36f, 0.3f, 2));
        m.loft(r);
        m.boxMinMax({-3.2f, -1.95f, g}, {-0.5f, 1.95f, g + 0.95f}, 0.25f);  // heel block
    }
}

void buildBody(MeshBuilder& m, const Style& s) {
    const bool ct = s.ct;
    // Pelvis: trousers and belt kit.
    m.bone = AG_PELVIS;
    m.setXform(translate(-kPelvisPos));
    m.mat = s.pants;
    torsoLoft(m, kPelvisSecs, 32, 3, [](float th, float z) { return 0.05f * std::sin(th * 5.0f + z * 1.1f) * saturate((39.0f - z) / 4.0f); });
    if (ct) {
        m.mat = s.strap;
        band(m, kPelvisSecs, 40.3f, 43.5f, 0.55f);
        Xform f = surfaceFrame(kPelvisSecs, 0.0f, 41.9f, 0.55f);
        m.mat = s.black;
        boxq(m, apply(f, {0.2f, 0, 0}), {0.22f, 0.95f, 0.72f}, f.q, 0.1f);
        m.mat = s.pouch;
        f = surfaceFrame(kPelvisSecs, 0.62f, 41.4f, 0.55f);
        for (float y : {-0.48f, 0.48f}) boxq(m, apply(f, {0.45f, y, -0.25f}), {0.45f, 0.4f, 1.15f}, f.q, 0.18f);
        f = surfaceFrame(kPelvisSecs, -2.35f, 40.9f, 0.55f);
        boxq(m, apply(f, {0.8f, 0, -0.5f}), {0.8f, 1.6f, 1.45f}, f.q, 0.5f);
        f = surfaceFrame(kPelvisSecs, 2.55f, 41.4f, 0.55f);
        boxq(m, apply(f, {0.7f, 0, 0}), {0.7f, 1.3f, 1.0f}, f.q, 0.35f);
    }

    // Abdomen (T: lower jacket with the chest rig).
    m.bone = AG_SPINE;
    m.setXform(translate(-kSpinePos));
    m.mat = s.shirt;
    if (ct) {
        torsoLoft(m, kSpineSecs, 32, 3, [](float th, float z) { return 0.04f * std::sin(th * 6.0f + z * 1.4f); });
    } else {
        torsoLoft(m, kJacketLowSecs, 32, 3, [](float th, float z) {
            return 0.07f * std::sin(th * 5.0f + z * 1.7f) + 0.05f * std::sin(z * 3.1f + th) * saturate((44.0f - z) / 3.0f);
        });
        m.mat = s.black;
        std::vector<vec3> zip;
        for (float z = 38.6f; z <= 49.0f; z += 1.3f) zip.push_back(secPoint(secAtZ(kJacketLowSecs, z), 0.0f, 0.06f));
        sweep(m, zip, {kX}, 0.11f, 0.05f, 0.03f);
        // Type 56 style chest rig: canvas panel, three AK magazine pouches, two grenade pouches.
        m.mat = s.vest;
        torsoSlab(m, kJacketLowSecs, 43.4f, 49.6f, 6, 12, 0.2f, 0.75f, [](float) { return vec2(-1.2f, 1.2f); });
        m.mat = s.pouch;
        for (int k = -1; k <= 1; k++) {
            Xform f = surfaceFrame(kJacketLowSecs, k * 0.36f, 46.1f, 0.75f);
            boxq(m, apply(f, {0.85f, 0, 0}), {0.85f, 1.0f, 2.45f}, f.q, 0.35f);
            boxq(m, apply(f, {1.0f, 0, 2.3f}), {0.95f, 1.08f, 0.35f}, f.q * qaxis(kY, 0.12f), 0.15f);
            m.mat = s.strap;
            boxq(m, apply(f, {1.72f, 0, 1.6f}), {0.1f, 0.25f, 0.5f}, f.q, 0.04f);
            m.mat = s.pouch;
        }
        for (float sd : {-1.0f, 1.0f}) {
            Xform f = surfaceFrame(kJacketLowSecs, sd * 1.02f, 45.6f, 0.75f);
            boxq(m, apply(f, {0.75f, 0, 0}), {0.75f, 0.85f, 1.5f}, f.q, 0.3f);
            boxq(m, apply(f, {0.85f, 0, 1.45f}), {0.8f, 0.9f, 0.28f}, f.q, 0.12f);
        }
    }

    // Chest.
    m.bone = AG_CHEST;
    m.setXform(translate(-kChestPos));
    m.mat = s.shirt;
    float bulk = ct ? 0.0f : 0.3f;
    torsoLoft(m, kChestSecs, 32, 3, [bulk](float th, float z) { return bulk + 0.04f * std::sin(th * 6.0f + z * 1.3f); }, true, false);
    if (ct) {
        m.mat = s.shirt;
        torsoSlab(m, kCollarSecs, 57.3f, 60.0f, 3, 16, 0.0f, 0.35f, [](float) { return vec2(0.3f, kTwoPi - 0.3f); });
        // Plate carrier: front and back bags with shooter's cut, cummerbund, padded shoulder straps.
        m.mat = s.vest;
        torsoSlab(m, kTorsoSecs, 44.4f, 56.6f, 12, 12, 0.1f, 1.35f, [](float z) {
            float hw = 0.78f - 0.34f * saturate((z - 53.6f) / 3.0f);
            return vec2(-hw, hw);
        });
        torsoSlab(m, kTorsoSecs, 44.6f, 56.9f, 12, 12, 0.1f, 1.15f, [](float z) {
            float hw = 0.82f - 0.26f * saturate((z - 54.6f) / 2.2f);
            return vec2(kPi - hw, kPi + hw);
        });
        for (float sd : {-1.0f, 1.0f}) {
            torsoSlab(m, kTorsoSecs, 44.6f, 50.6f, 5, 10, 0.08f, 0.7f, [sd](float) {
                return sd > 0 ? vec2(0.7f, kPi - 0.75f) : vec2(-(kPi - 0.75f), -0.7f);
            });
            std::vector<vec3> path = {{4.4f, sd * 4.3f, 55.9f}, {3.4f, sd * 4.3f, 57.4f}, {1.6f, sd * 4.3f, 58.3f},
                                      {-0.6f, sd * 4.3f, 58.45f}, {-2.8f, sd * 4.3f, 57.9f}, {-4.2f, sd * 4.3f, 56.5f}};
            std::vector<vec3> up;
            for (auto& p : path) up.push_back(normalize(vec3(p.x, 0, p.z - 54.2f)));
            m.mat = s.vest;
            sweep(m, path, up, 1.25f, 0.32f, 0.25f);
        }
        // Molle rows on the back bag.
        m.mat = s.strap;
        for (float z : {47.0f, 49.0f, 51.0f})
            torsoSlab(m, kTorsoSecs, z, z + 0.4f, 1, 10, 1.1f, 1.28f, [](float) { return vec2(kPi - 0.72f, kPi + 0.72f); });
        // Triple rifle magazine pouches with magazines and bungees.
        for (int k = -1; k <= 1; k++) {
            Xform f = surfaceFrame(kTorsoSecs, k * 0.33f, 46.5f, 1.35f);
            m.mat = s.pouch;
            boxq(m, apply(f, {0.8f, 0, 0}), {0.8f, 0.95f, 1.9f}, f.q, 0.28f);
            m.mat = s.black;
            for (float y : {-0.44f, 0.44f}) boxq(m, apply(f, {0.8f, y, 2.1f}), {0.5f, 0.33f, 0.62f}, f.q, 0.12f);
            m.mat = s.strap;
            boxq(m, apply(f, {0.85f, 0, 1.75f}), {0.86f, 1.0f, 0.13f}, f.q, 0.06f);
        }
        // Admin pouch with a subdued flag patch.
        Xform f = surfaceFrame(kTorsoSecs, 0.0f, 52.3f, 1.35f);
        m.mat = s.pouch;
        boxq(m, apply(f, {0.4f, 0, 0}), {0.42f, 2.5f, 1.3f}, f.q, 0.25f);
        m.mat = s.patch;
        boxq(m, apply(f, {0.84f, 1.1f, 0.3f}), {0.05f, 0.95f, 0.6f}, f.q, 0.03f);
        // Radio on the left rear with a whip antenna.
        f = surfaceFrame(kTorsoSecs, 2.3f, 49.2f, 0.7f);
        m.mat = s.black;
        boxq(m, apply(f, {0.7f, 0, 0}), {0.62f, 0.8f, 1.9f}, f.q, 0.18f);
        m.cylinder(apply(f, {0.9f, 0.3f, 1.85f}), apply(f, {0.9f, 0.3f, 2.6f}), 0.22f, 0.2f, 8);
        tube(m, {apply(f, {0.9f, 0.3f, 2.5f}), apply(f, {1.0f, 0.3f, 7.0f}), apply(f, {1.2f, 0.3f, 11.0f})}, 0.08f, 5);
    } else {
        m.mat = s.black;
        std::vector<vec3> zip;
        for (float z = 48.8f; z <= 57.2f; z += 1.2f) zip.push_back(secPoint(secAtZ(kChestSecs, z), 0.0f, bulk + 0.06f));
        sweep(m, zip, {kX}, 0.11f, 0.05f, 0.03f);
        // Chest pockets.
        for (float sd : {-1.0f, 1.0f}) {
            Xform f = surfaceFrame(kChestSecs, sd * 0.45f, 52.6f, bulk);
            m.mat = s.shirt;
            boxq(m, apply(f, {0.22f, 0, 0}), {0.25f, 1.35f, 1.5f}, f.q, 0.15f);
            boxq(m, apply(f, {0.36f, 0, 1.3f}), {0.2f, 1.45f, 0.45f}, f.q, 0.1f);
        }
        // Rig harness over the shoulders, crossing at the back.
        m.mat = s.strap;
        for (float sd : {-1.0f, 1.0f}) {
            std::vector<vec3> path = {{4.6f, sd * 4.4f, 49.4f}, {4.9f, sd * 4.4f, 53.0f}, {3.6f, sd * 4.4f, 57.2f}, {1.5f, sd * 4.4f, 58.5f},
                                      {-0.8f, sd * 4.3f, 58.6f}, {-3.3f, sd * 3.6f, 57.2f}, {-4.7f, sd * 1.2f, 53.5f},
                                      {-4.8f, -sd * 1.8f, 50.2f}, {-4.4f, -sd * 4.6f, 47.6f}};
            std::vector<vec3> up;
            for (auto& p : path) up.push_back(normalize(vec3(p.x, p.y * 0.3f, p.z - 53.0f)));
            sweep(m, path, up, 0.8f, 0.14f, 0.1f);
        }
        // Shemagh bunched around the neck with a small knot and a triangular flap over the chest.
        m.mat = s.scarf;
        std::vector<Ring> sc;
        const float zs[] = {56.9f, 57.8f, 58.9f, 59.8f, 60.5f};
        const float rx[] = {3.55f, 3.75f, 3.5f, 3.05f, 2.65f}, ry[] = {4.05f, 4.1f, 3.75f, 3.2f, 2.75f};
        for (int k = 0; k < 5; k++) {
            Ring r;
            for (int i = 0; i < 28; i++) {
                float a = kTwoPi * i / 28;
                float fo = 1.0f + 0.07f * std::sin(a * 6.0f + k * 1.9f) + 0.04f * std::sin(a * 11.0f + k * 1.3f);
                r.push_back({-0.35f + std::cos(a) * rx[k] * fo, std::sin(a) * ry[k] * fo, zs[k]});
            }
            sc.push_back(r);
        }
        m.loft(sc, false, false);
        ellipsoid(m, {3.65f, 0.35f, 57.5f}, {0.65f, 0.8f, 0.55f}, qaxis(kX, 0.3f), 12, 8);
        ellipsoid(m, {3.5f, 0.75f, 56.5f}, {0.4f, 0.5f, 0.95f}, qaxis(kX, 0.4f), 10, 7);
    }

    // Arms.
    for (int side = 0; side < 2; side++) {
        bool left = side == 0;
        m.resetXform();
        m.bone = left ? AG_UARM_L : AG_UARM_R;
        m.mat = s.shirt;
        ellipsoid(m, {0.7f, 0, 0}, {2.9f, 2.85f, 2.85f}, quat(), 16, 10);
        limb(m, kUpperArm, 16, 3, ct ? 0.02f : 0.035f, side * 1.7f);
        if (ct) {
            m.mat = s.patch;
            ellipsoid(m, {2.6f, 0, -2.45f}, {1.5f, 1.3f, 0.3f}, quat(), 12, 6);
            m.mat = s.black;
            ellipsoid(m, {kUArm - 0.2f, 0, 1.55f}, {1.8f, 1.75f, 0.9f}, quat(), 12, 8);
        }
        m.bone = left ? AG_FARM_L : AG_FARM_R;
        m.mat = s.shirt;
        m.sphere(vec3(0), vec3(2.02f), 14, 9);
        limb(m, kForearm, 16, 3, ct ? 0.025f : 0.045f, side * 2.3f + 1.0f);
        limb(m, {{7.6f, 1.72f, 1.6f, 0, 0}, {8.0f, 1.84f, 1.72f, 0, 0}, {9.0f, 1.78f, 1.66f, 0, 0}, {9.3f, 1.6f, 1.5f, 0, 0}}, 16, 1);
        buildHand(m, left, s);
    }

    // Legs.
    for (int side = 0; side < 2; side++) {
        float sd = side ? -1.0f : 1.0f;  // outer side of this leg along bone Y
        m.resetXform();
        m.bone = side ? AG_THIGH_R : AG_THIGH_L;
        m.mat = s.pants;
        ellipsoid(m, {0, 0, 0}, {3.8f, 3.95f, 3.85f}, quat(), 16, 10);
        limb(m, kThighRings, 18, 3, 0.03f, side * 1.3f);
        if (!ct || side == 0) {
            boxq(m, {7.6f, sd * 3.5f, 0.35f}, {2.3f, 0.42f, 1.9f}, quat(), 0.3f);
            boxq(m, {5.35f, sd * 3.72f, 0.35f}, {0.4f, 0.32f, 2.05f}, quat(), 0.15f);
        }
        if (ct && side == 1) {
            // Drop-leg holster with a pistol.
            m.mat = s.strap;
            limb(m, {{4.0f, 3.9f, 3.77f, 0, 0.15f}, {4.9f, 3.88f, 3.75f, 0, 0.15f}}, 18, 1);
            limb(m, {{8.2f, 3.5f, 3.4f, 0, 0.12f}, {9.1f, 3.46f, 3.36f, 0, 0.12f}}, 18, 1);
            m.mat = s.black;
            boxq(m, {5.6f, -3.95f, 0.3f}, {3.0f, 0.45f, 1.6f}, quat(), 0.2f);
            boxq(m, {5.2f, -4.55f, 0.45f}, {2.3f, 0.5f, 1.05f}, quat(), 0.3f);
            boxq(m, {1.8f, -4.5f, 0.95f}, {1.3f, 0.42f, 0.7f}, qaxis(kY, -0.3f), 0.25f);
        }
        m.bone = side ? AG_CALF_R : AG_CALF_L;
        m.sphere({0.0f, 0, 0.1f}, vec3(2.8f), 14, 9);
        limb(m, kCalfRings, 16, 3, ct ? 0.03f : 0.05f, side * 0.7f + 2.0f);
        if (ct) {
            m.mat = s.black;
            ellipsoid(m, {0.6f, 0, 2.3f}, {2.6f, 2.35f, 1.05f}, quat(), 14, 8);
            m.mat = s.strap;
            limb(m, {{2.5f, 3.0f, 3.08f, 0, -0.2f}, {3.2f, 3.02f, 3.12f, 0, -0.22f}}, 16, 1);
        }
        m.mat = s.boot;
        limb(m, {{12.4f, 2.3f, 2.36f, 0, 0.05f}, {12.9f, 2.28f, 2.34f, 0, 0.05f}, {14.6f, 2.15f, 2.25f, 0, 0.1f}, {16.4f, 2.08f, 2.4f, 0, 0.3f},
                 {17.4f, 1.95f, 2.3f, 0, 0.4f}},
             16, 2);
        m.bone = side ? AG_FOOT_R : AG_FOOT_L;
        buildBootFoot(m, s);
    }
    m.resetXform();
}

// ---------------------------------------------------------------------------------------------------------------
// Animation helpers

// Height of the stock's butt center in weapon space (the rear end comes from the mesh bounds).
float buttZ(int id) {
    switch (id) {
        case W_AK47: return -1.95f;
        case W_M4A4: return -0.3f;
        case W_AWP: return -1.6f;
        default: return -1.0f;
    }
}

quat yawPitchRoll(float yaw, float pitch, float roll = 0.0f) { return qaxis(kZ, yaw) * qaxis(-kY, pitch) * qaxis(kX, roll); }

}  // namespace

void buildAgentModel(int team, Renderer& r, AgentModel& out) {
    Style s;
    s.ct = team == TEAM_CT;
    s.eye = {{0.56f, 0.52f, 0.48f}, 0.45f, 0.0f, 0};
    s.iris = {{0.14f, 0.09f, 0.05f}, 0.35f, 0.0f, 0};
    s.black = {{0.055f, 0.055f, 0.06f}, 0.55f, 0.0f, 4};
    s.pupil = {{0.02f, 0.02f, 0.02f}, 0.6f, 0.0f, 0};
    s.lens = {{0.05f, 0.055f, 0.06f}, 0.3f, 0.25f, 0};
    if (s.ct) {
        s.skin = {{0.66f, 0.52f, 0.44f}, 0.55f, 0.0f, 7};
        s.lips = {{0.62f, 0.42f, 0.38f}, 0.5f, 0.0f, 7};
        s.hair = {{0.13f, 0.1f, 0.08f}, 0.8f, 0.0f, 7};
        s.shirt = {{0.27f, 0.31f, 0.38f}, 0.9f, 0.0f, 3};
        s.pants = {{0.25f, 0.29f, 0.35f}, 0.9f, 0.0f, 3};
        s.vest = {{0.25f, 0.26f, 0.21f}, 0.85f, 0.0f, 2};
        s.pouch = {{0.22f, 0.235f, 0.19f}, 0.85f, 0.0f, 2};
        s.strap = {{0.16f, 0.17f, 0.14f}, 0.85f, 0.0f, 2};
        s.glove = {{0.1f, 0.1f, 0.11f}, 0.6f, 0.0f, 6};
        s.knuckle = {{0.17f, 0.17f, 0.18f}, 0.5f, 0.0f, 4};
        s.boot = {{0.075f, 0.07f, 0.07f}, 0.5f, 0.0f, 6};
        s.sole = {{0.05f, 0.05f, 0.05f}, 0.9f, 0.0f, 4};
        s.helmet = {{0.25f, 0.27f, 0.21f}, 0.7f, 0.0f, 4};
        s.patch = {{0.15f, 0.16f, 0.14f}, 0.95f, 0.0f, 2};
        s.cloth = s.black;
        s.scarf = s.vest;
        out.patternA = {0.14f, 0.16f, 0.22f};
        out.patternB = {0.36f, 0.4f, 0.48f};
    } else {
        s.skin = {{0.62f, 0.45f, 0.34f}, 0.55f, 0.0f, 7};
        s.lips = s.skin;
        s.hair = {{0.08f, 0.06f, 0.05f}, 0.8f, 0.0f, 7};
        s.shirt = {{0.5f, 0.44f, 0.33f}, 0.9f, 0.0f, 3};
        s.pants = {{0.31f, 0.29f, 0.22f}, 0.9f, 0.0f, 2};
        s.vest = {{0.34f, 0.32f, 0.22f}, 0.85f, 0.0f, 2};
        s.pouch = {{0.31f, 0.29f, 0.2f}, 0.85f, 0.0f, 2};
        s.strap = {{0.25f, 0.23f, 0.16f}, 0.85f, 0.0f, 2};
        s.glove = {{0.29f, 0.2f, 0.13f}, 0.6f, 0.0f, 6};
        s.knuckle = s.glove;
        s.boot = {{0.24f, 0.17f, 0.11f}, 0.6f, 0.0f, 6};
        s.sole = {{0.3f, 0.26f, 0.2f}, 0.9f, 0.0f, 4};
        s.helmet = s.black;
        s.patch = s.strap;
        s.cloth = {{0.09f, 0.09f, 0.095f}, 0.95f, 0.0f, 2};
        s.scarf = {{0.34f, 0.17f, 0.14f}, 0.9f, 0.0f, 2};
        out.patternA = {0.40f, 0.33f, 0.24f};
        out.patternB = {0.62f, 0.56f, 0.43f};
    }
    MeshBuilder m;
    buildHead(m, s);
    buildNeck(m, s);
    buildBody(m, s);
    m.resetXform();
    // Parts are stored in bone space, so occlusion is only meaningful within a bone.
    m.bakeAO(3.0f, 32, 1.0f, true);
    out.mesh = m.upload(r);
}

void animateAgent(const AgentAnimInput& in, AgentPose& out) {
    const float t = in.time;
    vec3 vel = in.velLocal;
    float speed = length(vec2(vel.x, vel.y));
    float sp = saturate(speed / 250.0f);
    float moving = saturate(speed / 40.0f);
    float air = in.onGround ? 0.0f : 1.0f;
    const WeaponModel* wm = in.wm;
    int cls = wm ? in.weaponClass : -1;
    bool longGun = cls == WC_RIFLE || cls == WC_SNIPER;
    bool pistol = cls == WC_PISTOL;
    float duck = saturate(in.duck);
    if (in.planting) duck = std::max(duck, 0.8f);
    float aimR = clampf(in.aimPitch, -85.0f, 85.0f) * kDeg;
    float breath = std::sin(t * 1.6f), sway = std::sin(t * 0.55f + 1.3f);
    float kick = in.fireT >= 0.0f && in.fireT < 0.14f ? 1.0f - in.fireT / 0.14f : 0.0f;
    kick *= kick;
    float reload = in.reloadT >= 0.0f && in.reloadDur > 0.0f ? saturate(in.reloadT / in.reloadDur) : -1.0f;
    float relT = reload >= 0.0f ? bump(reload, 0.0f, 0.12f, 0.82f, 1.0f) : 0.0f;

    // Gait: cadence rises with speed; stance travel is capped by leg reach, so CS speeds only slide the feet a little.
    float dtG = out.gaitTime >= 0.0f ? clampf(t - out.gaitTime, 0.0f, 0.1f) : 0.0f;
    out.gaitTime = t;
    float cadence = lerpf(1.25f, 1.55f, sp) * (1.0f + 0.15f * duck);  // cycles per second
    out.gaitPhase = std::fmod(out.gaitPhase + dtG * cadence * moving, 1.0f);
    float duty = lerpf(0.58f, 0.36f, sp);
    float stanceLen = std::min(duty * speed / cadence, lerpf(24.0f, 34.0f, sp) * (1.0f - 0.3f * duck)) * moving;
    vec3 moveDir = speed > 1.0f ? vec3(vel.x / speed, vel.y / speed, 0) : kX;
    float liftH = lerpf(3.2f, 9.5f, sp) * moving * (1.0f - 0.4f * duck);
    vec3 footPos[2];
    float footYaw[2], footPitch[2], along[2];
    for (int side = 0; side < 2; side++) {
        float s = side ? -1.0f : 1.0f;
        float p = std::fmod(out.gaitPhase + (side ? 0.5f : 0.0f), 1.0f);
        float a, lift = 0.0f, pitch;
        if (p < duty) {
            float u = p / duty;
            a = stanceLen * (0.5f - u);
            pitch = (0.2f * (1.0f - ease(u, 0.0f, 0.15f)) - 0.5f * ease(u, 0.65f, 1.0f)) * sp;
        } else {
            float q = (p - duty) / (1.0f - duty);
            // Heel comes up behind first, then the foot reaches forward.
            a = stanceLen * (smooth01(std::pow(q, 1.35f)) - 0.5f);
            lift = liftH * std::sin(kPi * std::pow(q, 0.7f));
            pitch = lerpf(-0.5f, 0.2f, ease(q, 0.0f, 0.8f)) * sp;
        }
        along[side] = a;
        float stanceX = longGun ? (side ? -2.4f : 2.8f) : (side ? -1.0f : 1.2f);
        stanceX = lerpf(stanceX, side ? -3.2f : 4.0f, duck) * (1.0f - moving);
        vec3 f(stanceX - 1.5f * duck, s * (4.5f + 0.9f * duck + (longGun ? 0.4f : 0.0f)), kAnkleZ);
        f += moveDir * a;
        f.z += lift + (pitch < 0.0f ? 8.0f * std::sin(-pitch) : 2.5f * std::sin(pitch));  // rolling over toe / heel
        f += vec3(-2.0f * s * 0.5f - 1.0f, 0, 6.0f + s * 1.5f) * air;
        footPos[side] = f;
        float yawOut = longGun ? (side ? 24.0f : 9.0f) : 11.0f;
        footYaw[side] = s * yawOut * kDeg * (1.0f - 0.7f * moving);
        footPitch[side] = pitch - 0.35f * air;
    }

    // Torso chain in model space: stance blade (left shoulder forward), gait counter-rotation, lean, aim, breathing.
    float blade = (longGun ? -24.0f : pistol ? -10.0f : -6.0f) * (1.0f - 0.5f * sp) * kDeg;
    float gaitYaw = -0.0025f * (along[0] - along[1]) * moveDir.x;
    // Walking vaults over the stance leg (highest at mid-stance), running compresses into it.
    float mid = std::cos(4.0f * kPi * (out.gaitPhase - duty * 0.5f));
    float bob = lerpf(0.35f * (mid - 1.0f), -0.6f * (mid + 1.0f), sp) * moving * (1.0f - 0.5f * duck);
    float hipRoll = 0.05f * std::sin(kTwoPi * (out.gaitPhase - duty * 0.5f)) * moving;
    float tilt = -0.18f * duck;
    float lean = (4.0f + 8.0f * sp + 12.0f * duck) * kDeg;
    float sink = lerpf(1.2f, 3.0f, sp) * moving * (1.0f - duck);  // bent knees give the stride its reach
    vec3 pelvisPos = kPelvisPos + vec3(-2.2f * duck, 0.35f * sway * (1.0f - moving), -15.5f * duck - sink + bob + 2.0f * air);
    out.bones[AG_PELVIS] = Xform(pelvisPos, yawPitchRoll(blade * 0.45f + gaitYaw, tilt, hipRoll + 0.02f * sway * (1.0f - moving)));
    out.bones[AG_SPINE] = Xform(apply(out.bones[AG_PELVIS], kSpineOff), yawPitchRoll(blade * 0.75f - gaitYaw * 0.3f, tilt - lean * 0.5f + aimR * 0.2f));
    float chestPitch = tilt - lean + aimR * 0.45f + (2.5f * kick + 0.6f * breath) * kDeg;
    float counter = wm && cls != WC_KNIFE ? 0.35f : 1.0f;  // aimed weapons keep the chest steadier
    out.bones[AG_CHEST] = Xform(apply(out.bones[AG_SPINE], kChestOff + vec3(0, 0, 0.12f * breath)),
                                yawPitchRoll(blade - gaitYaw * counter, chestPitch, -hipRoll * 0.5f));
    // Long guns: cheek weld, head dipped forward and tilted onto the stock.
    float weld = longGun ? 1.0f - relT : 0.0f;
    out.bones[AG_NECK] = Xform(apply(out.bones[AG_CHEST], kNeckOff),
                               yawPitchRoll(blade * 0.4f - gaitYaw * 0.3f, -lean * 0.3f + aimR * 0.6f - 0.12f * weld, 0.08f * weld));
    out.bones[AG_HEAD] = Xform(apply(out.bones[AG_NECK], kHeadOff), yawPitchRoll(0.0f, aimR * 0.92f - 0.05f * weld, 0.2f * weld));

    // Legs: two-bone IK to the foot targets, knees toward the feet.
    for (int side = 0; side < 2; side++) {
        float s = side ? -1.0f : 1.0f;
        vec3 hip = apply(out.bones[AG_PELVIS], vec3(kHipOff.x, s * kHipOff.y, kHipOff.z));
        vec3 ff(std::cos(footYaw[side]), std::sin(footYaw[side]), 0);
        Xform target = xformFromBasis(footPos[side], vec3(0, 0, -1), ff);
        Xform thigh, calf;
        float st;
        solveArmIK(hip, target, normalize(ff + vec3(0, s * 0.15f, 0.1f)), kThigh, kCalf, thigh, calf, st);
        out.bones[side ? AG_THIGH_R : AG_THIGH_L] = thigh;
        out.bones[side ? AG_CALF_R : AG_CALF_L] = calf;
        out.bones[side ? AG_FOOT_R : AG_FOOT_L] = Xform(apply(calf, vec3(kCalf, 0, 0)), yawPitchRoll(footYaw[side], footPitch[side]));
    }

    // Weapon: long guns sit in the right shoulder pocket and point along the aim; others are held in front.
    const Xform& chest = out.bones[AG_CHEST];
    quat aimQ = qaxis(-kY, aimR);
    Xform wpn = chest;
    out.hasWeapon = wm != nullptr;
    if (wm) {
        if (longGun) {
            vec3 pocket = apply(chest, vec3(3.4f, -3.6f, 5.4f));
            quat q = aimQ * qaxis(kZ, 3.0f * kDeg) * quatFromEuler(3.0f * kick - 6.0f * relT, 6.0f * relT, 22.0f * relT);
            vec3 butt(wm->bounds.mn.x + 0.35f, 0, buttZ(wm->id));
            wpn = Xform(pocket - rotate(q, butt), q);
            wpn.p += rotate(aimQ, vec3(-1.1f * kick, 0, 0)) + rotate(chest.q, vec3(-1.0f, 1.2f, -2.2f)) * relT;
        } else if (pistol) {
            vec3 base = apply(chest, vec3(0, 0, kShoulderOff.z));
            quat q = aimQ * quatFromEuler(4.0f * kick - 8.0f * relT, 0, 20.0f * relT);
            wpn = Xform(base + rotate(aimQ, vec3(17.0f - 1.0f * kick - 5.0f * relT, -1.2f, 0.8f - 3.0f * relT)), q);
        } else if (cls == WC_KNIFE) {
            wpn = chest * Xform({11.5f, -6.2f, -1.6f}, quatFromEuler(18.0f, 8.0f, -12.0f));
        } else if (cls == WC_GRENADE) {
            wpn = chest * Xform({10.0f, -6.6f, 2.0f}, quat());
        } else {
            wpn = chest * Xform({10.6f, 0.0f, -4.2f}, quatFromEuler(18.0f, 0, 0));
        }
        if (in.planting) wpn = chest * Xform({12.5f, 0.0f, -9.5f}, quatFromEuler(10.0f, 0.0f, 0.0f));
    }
    out.weapon = wpn;

    // Arms: IK to the grips (or a relaxed hang), fingers posed per weapon.
    for (int side = 0; side < 2; side++) {
        bool left = side == 0;
        float s = left ? 1.0f : -1.0f;
        bool onWeapon = wm && (!left || wm->leftOnWeapon);
        float prot = left && onWeapon && longGun ? 1.6f : 0.0f;
        vec3 shoulder = apply(chest, vec3(kShoulderOff.x + prot, s * kShoulderOff.y, kShoulderOff.z));
        Xform hand;
        FingerPose fp;
        vec3 pole;
        if (onWeapon) {
            hand = wpn * (left ? wm->leftGrip : wm->rightGrip);
            fp = left ? wm->leftPose : wm->rightPose;
            if (left && reload >= 0.0f && !in.planting) {
                float mag = bump(reload, 0.1f, 0.24f, 0.68f, 0.84f);
                hand = lerp(hand, wpn * wm->leftMagGrip, mag);
                fp = FingerPose::lerp(fp, wm->leftMagPose, mag);
            }
            if (longGun) pole = left ? vec3(-0.1f, 0.45f, -1.0f) : vec3(-0.35f, -0.9f, -0.75f);
            else pole = vec3(-0.2f, s * 0.7f, -1.0f);
        } else {
            // Free arm hangs relaxed and swings against the leg on the same side.
            float swing = -0.32f * along[side] * moveDir.x;
            vec3 hp = shoulder + rotate(chest.q, vec3(1.2f + swing + 0.3f * std::sin(t * 0.9f + side), s * 1.6f, -20.2f + 0.02f * swing * swing));
            hand = xformFromBasis(hp, rotate(chest.q, normalize(vec3(0.18f, -s * 0.08f, -1.0f))), rotate(chest.q, vec3(0, s, 0)));
            fp = poseRelaxed();
            pole = vec3(-1.0f, s * 0.3f, -0.3f);
        }
        pole = rotate(chest.q, normalize(pole));
        // Reach a little further with the shoulder when the grip is beyond arm's length.
        vec3 d = hand.p - shoulder;
        float L = length(d), reach = (kUArm + kFArm) * 0.985f;
        if (L > reach) shoulder += d * (std::min(L - reach, 3.0f) / L);
        Xform upper, fore;
        float st;
        solveArmIK(shoulder, hand, pole, kUArm, kFArm, upper, fore, st);
        hand.p = apply(fore, vec3(kFArm, 0, 0));
        out.bones[left ? AG_UARM_L : AG_UARM_R] = upper;
        out.bones[left ? AG_FARM_L : AG_FARM_R] = fore;
        out.bones[left ? AG_HAND_L : AG_HAND_R] = hand;
        Xform fingers[15];
        poseFingers(hand, fp, left, fingers);
        for (int k = 0; k < 15; k++) out.bones[(left ? AG_FINGERS_L : AG_FINGERS_R) + k] = fingers[k];
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
            if (tt > 0) {
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
    out[n++] = {P(AG_HEAD, {0.35f, 0, 1.2f}), P(AG_HEAD, {0.05f, 0, 4.3f}), 3.9f, HG_HEAD};
    out[n++] = {P(AG_CHEST, {0.5f, 0, 0.3f}), P(AG_CHEST, {0.0f, 0, 5.6f}), 6.7f, HG_CHEST};
    out[n++] = {P(AG_PELVIS, {0.2f, 0, 0.3f}), P(AG_SPINE, {0.35f, 0, 3.8f}), 6.4f, HG_STOMACH};
    const int arms[4] = {AG_UARM_L, AG_FARM_L, AG_UARM_R, AG_FARM_R};
    for (int b : arms) {
        bool up = b == AG_UARM_L || b == AG_UARM_R;
        out[n++] = {P(b, {0, 0, 0}), P(b, {up ? kUArm : kFArm, 0, 0}), up ? 2.6f : 2.1f, HG_ARM};
    }
    const int legs[4] = {AG_THIGH_L, AG_CALF_L, AG_THIGH_R, AG_CALF_R};
    for (int b : legs) {
        bool up = b == AG_THIGH_L || b == AG_THIGH_R;
        out[n++] = {P(b, {0, 0, 0}), P(b, {up ? kThigh : kCalf, 0, 0}), up ? 3.7f : 2.7f, HG_LEG};
    }
    return n;
}

// Particles: 0 pelvis, 1 chest, 2 head, 3 shL, 4 elL, 5 haL, 6 shR, 7 elR, 8 haR, 9 hipL, 10 knL, 11 ftL, 12 hipR, 13 knR, 14 ftR
void Ragdoll::init(const AgentPose& pose, const Xform& world, vec3 velocity, vec3 impulse, vec3 impulsePoint) {
    const int bones[N] = {AG_PELVIS, AG_CHEST, AG_HEAD, AG_UARM_L, AG_FARM_L, AG_HAND_L, AG_UARM_R, AG_FARM_R, AG_HAND_R,
                          AG_THIGH_L, AG_CALF_L, AG_FOOT_L, AG_THIGH_R, AG_CALF_R, AG_FOOT_R};
    const float dt = 1.0f / 128.0f;
    for (int i = 0; i < N; i++) {
        vec3 local = i == 2 ? kHeadCenter : vec3(0);
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
    out.bones[AG_SPINE] = Xform(lerp(pelvis, chest, kSpineOff.z / (kSpineOff.z + kChestOff.z)), slerp(pelvisQ, torso, 0.5f));
    out.bones[AG_CHEST] = Xform(chest, torso);
    vec3 neckBase = chest + rotate(torso, kNeckOff);
    vec3 hup = normalize(head - neckBase);
    vec3 hf = normalize(fwd - hup * dot(fwd, hup));
    quat headQ = quatFromBasis(hf, cross(hup, hf), hup);
    out.bones[AG_NECK] = Xform(neckBase, headQ);
    out.bones[AG_HEAD] = Xform(head - rotate(headQ, kHeadCenter), headQ);
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
    Xform fingers[15];
    for (int s = 0; s < 2; s++) {
        poseFingers(out.bones[s ? AG_HAND_R : AG_HAND_L], poseRelaxed(), s == 0, fingers);
        for (int k = 0; k < 15; k++) out.bones[(s ? AG_FINGERS_R : AG_FINGERS_L) + k] = fingers[k];
    }
    out.hasWeapon = false;
}
