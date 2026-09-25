// Small self-contained math library. World convention (as in Source): X forward, Y left, Z up.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kDeg = kPi / 180.0f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth01(float t) { t = saturate(t); return t * t * (3.0f - 2.0f * t); }
inline float rangeT(float x, float a, float b) { return saturate((x - a) / (b - a)); }
// Smooth 0->1 transition while x goes from a to b.
inline float ease(float x, float a, float b) { return smooth01(rangeT(x, a, b)); }
// Smooth 0->1->0 bump: rises over [a,b], holds, falls over [c,d].
inline float bump(float x, float a, float b, float c, float d) { return ease(x, a, b) * (1.0f - ease(x, c, d)); }
inline float approach(float cur, float target, float delta) {
    return cur < target ? std::min(cur + delta, target) : std::max(cur - delta, target);
}
inline float wrapAngle(float deg) {
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0) deg += 360.0f;
    return deg - 180.0f;
}
// Frame-rate independent exponential smoothing toward target.
inline float damp(float cur, float target, float rate, float dt) { return target + (cur - target) * std::exp(-rate * dt); }

struct vec2 {
    float x = 0, y = 0;
    constexpr vec2() = default;
    constexpr vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr explicit vec2(float s) : x(s), y(s) {}
    vec2 operator+(vec2 o) const { return {x + o.x, y + o.y}; }
    vec2 operator-(vec2 o) const { return {x - o.x, y - o.y}; }
    vec2 operator*(vec2 o) const { return {x * o.x, y * o.y}; }
    vec2 operator/(vec2 o) const { return {x / o.x, y / o.y}; }
    vec2 operator*(float s) const { return {x * s, y * s}; }
    vec2 operator/(float s) const { return {x / s, y / s}; }
    vec2 operator-() const { return {-x, -y}; }
    vec2& operator+=(vec2 o) { x += o.x; y += o.y; return *this; }
    vec2& operator-=(vec2 o) { x -= o.x; y -= o.y; return *this; }
    vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(vec2 o) const { return x == o.x && y == o.y; }
};
inline vec2 operator*(float s, vec2 v) { return v * s; }
inline float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
inline float length(vec2 v) { return std::sqrt(dot(v, v)); }
inline vec2 normalize(vec2 v) { float l = length(v); return l > 1e-8f ? v / l : vec2(0, 0); }
inline vec2 lerp(vec2 a, vec2 b, float t) { return a + (b - a) * t; }
inline vec2 perp(vec2 v) { return {-v.y, v.x}; }
inline vec2 damp(vec2 cur, vec2 target, float rate, float dt) { return target + (cur - target) * std::exp(-rate * dt); }

struct vec3 {
    float x = 0, y = 0, z = 0;
    constexpr vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit vec3(float s) : x(s), y(s), z(s) {}
    constexpr vec3(vec2 v, float z_) : x(v.x), y(v.y), z(z_) {}
    vec3 operator+(vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    vec3 operator/(vec3 o) const { return {x / o.x, y / o.y, z / o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3& operator+=(vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(vec3 o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
    bool operator==(vec3 o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(vec3 o) const { return !(*this == o); }
    vec2 xy() const { return {x, y}; }
};
inline vec3 operator*(float s, vec3 v) { return v * s; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length2(vec3 v) { return dot(v, v); }
inline float length(vec3 v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(vec3 v) { float l = length(v); return l > 1e-8f ? v / l : vec3(0, 0, 0); }
inline vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline vec3 vmin(vec3 a, vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline vec3 vmax(vec3 a, vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline vec3 vabs(vec3 a) { return {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; }
inline vec3 reflect(vec3 i, vec3 n) { return i - n * (2.0f * dot(i, n)); }
inline float distance(vec3 a, vec3 b) { return length(a - b); }
inline vec3 damp(vec3 cur, vec3 target, float rate, float dt) { return target + (cur - target) * std::exp(-rate * dt); }
// Any unit vector perpendicular to n.
inline vec3 anyPerp(vec3 n) { return std::fabs(n.z) < 0.9f ? normalize(cross(n, vec3(0, 0, 1))) : normalize(cross(n, vec3(1, 0, 0))); }

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr vec4() = default;
    constexpr vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr vec4(vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    vec4 operator+(vec4 o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    vec4 operator-(vec4 o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    vec3 xyz() const { return {x, y, z}; }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};
inline float dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline vec4 lerp(vec4 a, vec4 b, float t) { return a + (b - a) * t; }

// Column-major 4x4 matrix (OpenGL layout): element (row r, col c) is m[c*4 + r].
struct mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& operator()(int r, int c) { return m[c * 4 + r]; }
    float operator()(int r, int c) const { return m[c * 4 + r]; }
    vec3 col(int c) const { return {m[c * 4], m[c * 4 + 1], m[c * 4 + 2]}; }
    void setCol(int c, vec3 v, float w) { m[c * 4] = v.x; m[c * 4 + 1] = v.y; m[c * 4 + 2] = v.z; m[c * 4 + 3] = w; }
    static mat4 identity() { return mat4(); }
};
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a(rr, k) * b(k, c);
            r(rr, c) = s;
        }
    return r;
}
inline vec4 operator*(const mat4& a, vec4 v) {
    vec4 r;
    for (int rr = 0; rr < 4; rr++) r[rr] = a(rr, 0) * v.x + a(rr, 1) * v.y + a(rr, 2) * v.z + a(rr, 3) * v.w;
    return r;
}
inline vec3 xformPoint(const mat4& a, vec3 p) { return (a * vec4(p, 1.0f)).xyz(); }
inline vec3 xformDir(const mat4& a, vec3 d) { return (a * vec4(d, 0.0f)).xyz(); }
inline mat4 translate(vec3 t) { mat4 r; r(0, 3) = t.x; r(1, 3) = t.y; r(2, 3) = t.z; return r; }
inline mat4 scale(vec3 s) { mat4 r; r(0, 0) = s.x; r(1, 1) = s.y; r(2, 2) = s.z; return r; }
inline mat4 fromBasis(vec3 x, vec3 y, vec3 z, vec3 o) {
    mat4 r; r.setCol(0, x, 0); r.setCol(1, y, 0); r.setCol(2, z, 0); r.setCol(3, o, 1); return r;
}
inline mat4 rotateAxis(vec3 axis, float ang) {
    axis = normalize(axis);
    float c = std::cos(ang), s = std::sin(ang), t = 1 - c;
    float x = axis.x, y = axis.y, z = axis.z;
    mat4 r;
    r(0, 0) = t * x * x + c;     r(0, 1) = t * x * y - s * z; r(0, 2) = t * x * z + s * y;
    r(1, 0) = t * x * y + s * z; r(1, 1) = t * y * y + c;     r(1, 2) = t * y * z - s * x;
    r(2, 0) = t * x * z - s * y; r(2, 1) = t * y * z + s * x; r(2, 2) = t * z * z + c;
    return r;
}
inline mat4 rotX(float a) { return rotateAxis({1, 0, 0}, a); }
inline mat4 rotY(float a) { return rotateAxis({0, 1, 0}, a); }
inline mat4 rotZ(float a) { return rotateAxis({0, 0, 1}, a); }
inline mat4 transpose(const mat4& a) { mat4 r; for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r(i, j) = a(j, i); return r; }
inline mat4 perspective(float fovy, float aspect, float n, float f) {
    mat4 r; float t = 1.0f / std::tan(fovy * 0.5f);
    r(0, 0) = t / aspect; r(1, 1) = t; r(2, 2) = (f + n) / (n - f); r(2, 3) = 2 * f * n / (n - f); r(3, 2) = -1; r(3, 3) = 0;
    return r;
}
inline mat4 ortho(float l, float rr, float b, float t, float n, float f) {
    mat4 r;
    r(0, 0) = 2 / (rr - l); r(1, 1) = 2 / (t - b); r(2, 2) = -2 / (f - n);
    r(0, 3) = -(rr + l) / (rr - l); r(1, 3) = -(t + b) / (t - b); r(2, 3) = -(f + n) / (f - n);
    return r;
}
inline mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye), s = normalize(cross(f, up)), u = cross(s, f);
    mat4 r;
    r(0, 0) = s.x; r(0, 1) = s.y; r(0, 2) = s.z;
    r(1, 0) = u.x; r(1, 1) = u.y; r(1, 2) = u.z;
    r(2, 0) = -f.x; r(2, 1) = -f.y; r(2, 2) = -f.z;
    r(0, 3) = -dot(s, eye); r(1, 3) = -dot(u, eye); r(2, 3) = dot(f, eye);
    return r;
}
inline mat4 inverse(const mat4& mm) {
    const float* m = mm.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    mat4 r;
    if (std::fabs(det) < 1e-12f) return r;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}

struct quat {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr quat() = default;
    constexpr quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};
inline quat operator*(quat a, quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline quat qaxis(vec3 axis, float ang) {
    axis = normalize(axis);
    float s = std::sin(ang * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(ang * 0.5f)};
}
inline quat qnormalize(quat q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 0 ? quat(q.x / l, q.y / l, q.z / l, q.w / l) : quat();
}
inline quat qconj(quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline vec3 rotate(quat q, vec3 v) {
    vec3 u(q.x, q.y, q.z);
    float s = q.w;
    return u * (2.0f * dot(u, v)) + v * (s * s - dot(u, u)) + cross(u, v) * (2.0f * s);
}
inline mat4 toMat4(quat q) {
    mat4 r;
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x,
          wy = q.w * q.y, wz = q.w * q.z;
    r(0, 0) = 1 - 2 * (yy + zz); r(0, 1) = 2 * (xy - wz);     r(0, 2) = 2 * (xz + wy);
    r(1, 0) = 2 * (xy + wz);     r(1, 1) = 1 - 2 * (xx + zz); r(1, 2) = 2 * (yz - wx);
    r(2, 0) = 2 * (xz - wy);     r(2, 1) = 2 * (yz + wx);     r(2, 2) = 1 - 2 * (xx + yy);
    return r;
}
// Rotation from an orthonormal basis given as columns.
inline quat quatFromBasis(vec3 x, vec3 y, vec3 z) {
    float tr = x.x + y.y + z.z;
    quat q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2;
        q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s};
    } else if (x.x > y.y && x.x > z.z) {
        float s = std::sqrt(1.0f + x.x - y.y - z.z) * 2;
        q = {0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        float s = std::sqrt(1.0f + y.y - x.x - z.z) * 2;
        q = {(y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        float s = std::sqrt(1.0f + z.z - x.x - y.y) * 2;
        q = {(z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s};
    }
    return qnormalize(q);
}
inline quat slerp(quat a, quat b, float t) {
    float c = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (c < 0) { b = {-b.x, -b.y, -b.z, -b.w}; c = -c; }
    if (c > 0.9995f) {
        return qnormalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    float th = std::acos(c), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}
// Euler in degrees applied as yaw (Z), then pitch (Y, positive = nose up), then roll (X).
inline quat quatFromEuler(float pitchDeg, float yawDeg, float rollDeg) {
    return qaxis({0, 0, 1}, yawDeg * kDeg) * qaxis({0, -1, 0}, pitchDeg * kDeg) * qaxis({1, 0, 0}, rollDeg * kDeg);
}

// Rigid transform (rotation + translation).
struct Xform {
    vec3 p;
    quat q;
    constexpr Xform() = default;
    Xform(vec3 p_, quat q_) : p(p_), q(q_) {}
};
inline Xform operator*(const Xform& a, const Xform& b) { return {a.p + rotate(a.q, b.p), qnormalize(a.q * b.q)}; }
inline Xform inverse(const Xform& a) { quat qi = qconj(a.q); return {rotate(qi, -a.p), qi}; }
inline vec3 apply(const Xform& a, vec3 v) { return a.p + rotate(a.q, v); }
inline mat4 toMat4(const Xform& a) { mat4 r = toMat4(a.q); r(0, 3) = a.p.x; r(1, 3) = a.p.y; r(2, 3) = a.p.z; return r; }
inline Xform lerp(const Xform& a, const Xform& b, float t) { return {lerp(a.p, b.p, t), slerp(a.q, b.q, t)}; }
inline Xform xformFromBasis(vec3 origin, vec3 xAxis, vec3 zHint) {
    vec3 x = normalize(xAxis);
    vec3 y = normalize(cross(zHint, x));
    if (length2(y) < 1e-6f) y = anyPerp(x);
    vec3 z = cross(x, y);
    return {origin, quatFromBasis(x, y, z)};
}

struct AABB {
    vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    AABB() = default;
    AABB(vec3 a, vec3 b) : mn(a), mx(b) {}
    void add(vec3 p) { mn = vmin(mn, p); mx = vmax(mx, p); }
    void add(const AABB& b) { mn = vmin(mn, b.mn); mx = vmax(mx, b.mx); }
    vec3 center() const { return (mn + mx) * 0.5f; }
    vec3 size() const { return mx - mn; }
    bool valid() const { return mn.x <= mx.x; }
    bool overlaps(const AABB& b) const {
        return mn.x <= b.mx.x && mx.x >= b.mn.x && mn.y <= b.mx.y && mx.y >= b.mn.y && mn.z <= b.mx.z && mx.z >= b.mn.z;
    }
    bool contains(vec3 p) const { return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y && p.z >= mn.z && p.z <= mx.z; }
};

struct Plane {
    vec3 n;
    float d = 0;  // dot(n, p) == d on the plane
};

struct Frustum {
    vec4 planes[6];
    void fromMatrix(const mat4& vp) {
        auto row = [&](int r) { return vec4(vp(r, 0), vp(r, 1), vp(r, 2), vp(r, 3)); };
        vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        planes[0] = r3 + r0; planes[1] = r3 - r0; planes[2] = r3 + r1;
        planes[3] = r3 - r1; planes[4] = r3 + r2; planes[5] = r3 - r2;
        for (auto& p : planes) { float l = length(p.xyz()); p = p * (1.0f / l); }
    }
    bool visible(const AABB& b) const {
        for (const auto& p : planes) {
            vec3 v(p.x > 0 ? b.mx.x : b.mn.x, p.y > 0 ? b.mx.y : b.mn.y, p.z > 0 ? b.mx.z : b.mn.z);
            if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
        }
        return true;
    }
};

// Forward vector from pitch/yaw in degrees (pitch positive = up, yaw 0 = +X, CCW).
inline vec3 angleForward(float pitchDeg, float yawDeg) {
    float cp = std::cos(pitchDeg * kDeg), sp = std::sin(pitchDeg * kDeg);
    float cy = std::cos(yawDeg * kDeg), sy = std::sin(yawDeg * kDeg);
    return {cp * cy, cp * sy, sp};
}
inline vec3 yawLeft(float yawDeg) { return {-std::sin(yawDeg * kDeg), std::cos(yawDeg * kDeg), 0}; }
