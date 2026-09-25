#include "audio/audio.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define CS2P_FLUSH_DENORMALS 1
#endif

#include "core/common.h"

#if CS2P_AUDIO
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#include "miniaudio.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

namespace {

constexpr int kRate = 48000;
using Buf = std::vector<float>;

const char* const kSoundNames[] = {
    "ak47", "m4a4", "awp", "deagle", "glock", "usp",
    "knife_swing", "knife_hit", "knife_stab", "empty", "reload_out", "reload_in", "bolt", "deploy", "zoom",
    "step0", "step1", "step2", "step3", "jump", "land",
    "hit_body", "hit_head", "helmet",
    "impact_stone", "impact_wood", "impact_metal", "impact_sand",
    "explosion", "smoke", "flash", "bounce", "throw",
    "beep", "planted", "defused",
    "round_start", "round_win", "round_lose",
    "ui_hover", "ui_click", "buy", "pickup", "shell", "kill"};
static_assert(sizeof(kSoundNames) / sizeof(kSoundNames[0]) == SND_COUNT, "sound name table out of sync with SoundId");

inline size_t smp(float sec) { return sec <= 0.0f ? 0 : (size_t)(sec * kRate + 0.5f); }
inline float decayCoef(float tau) { return std::exp(-1.0f / (std::max(tau, 1e-5f) * kRate)); }
inline float onePoleCoef(float hz) { return 1.0f - std::exp(-kTwoPi * hz / kRate); }
inline float white(Rng& r) { return r.range(-1.0f, 1.0f); }

// ------------------------------------------------------------------------------------------------ filters

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float operator()(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

enum BqType { BQ_LP, BQ_HP, BQ_BP, BQ_PEAK, BQ_HSHELF, BQ_LSHELF };

// RBJ cookbook biquads.
Biquad biquad(BqType type, float fc, float q = 0.7071f, float db = 0.0f) {
    fc = clampf(fc, 10.0f, kRate * 0.45f);
    float w = kTwoPi * fc / kRate, cw = std::cos(w), sw = std::sin(w), al = sw / (2.0f * q);
    float A = std::pow(10.0f, db / 40.0f), sa = 2.0f * std::sqrt(A) * al;
    float b0, b1, b2, a0, a1, a2;
    switch (type) {
        case BQ_LP: b0 = (1 - cw) * 0.5f; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case BQ_HP: b0 = (1 + cw) * 0.5f; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case BQ_BP: b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case BQ_PEAK: b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
        case BQ_HSHELF:
            b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sa);
            a0 = (A + 1) - (A - 1) * cw + sa; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sa;
            break;
        default:
            b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sa);
            a0 = (A + 1) + (A - 1) * cw + sa; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sa;
            break;
    }
    Biquad f;
    f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0; f.a1 = a1 / a0; f.a2 = a2 / a0;
    return f;
}

void filt(Buf& s, BqType type, float fc, float q = 0.7071f, float db = 0.0f) {
    Biquad f = biquad(type, fc, q, db);
    for (float& v : s) v = f(v);
}

// Topology-preserving state-variable filter: stable under fast cutoff sweeps.
struct Svf {
    float s1 = 0, s2 = 0, lp = 0, bp = 0, hp = 0;
    void run(float x, float g, float k) {
        hp = (x - (g + k) * s1 - s2) / (1.0f + g * (g + k));
        float v1 = g * hp;
        bp = v1 + s1;
        s1 = bp + v1;
        float v2 = g * bp;
        lp = v2 + s2;
        s2 = lp + v2;
    }
};
inline float svfG(float hz) { return std::tan(kPi * clampf(hz, 10.0f, kRate * 0.45f) / kRate); }

// ------------------------------------------------------------------------------------------------ layer generators

// Linear attack, exponential decay; evaluated incrementally.
struct Env {
    size_t na, i = 0;
    float k, e = 1.0f;
    Env(float attack, float tau) : na(std::max<size_t>(1, smp(attack))), k(decayCoef(tau)) {}
    float next() {
        if (i < na) return (float)(i++) / (float)na;
        i++;
        return e *= k;
    }
};

// Smooth random signal in [-1, 1] that changes about `hz` times per second.
struct Drift {
    Rng& r;
    float a, b;
    size_t seg, i = 0;
    Drift(Rng& rng, float hz) : r(rng), a(white(rng)), b(white(rng)), seg(std::max<size_t>(8, smp(1.0f / hz))) {}
    float next() {
        if (i == seg) { i = 0; a = b; b = white(r); }
        float u = (float)(i++) / (float)seg;
        return a + (b - a) * (0.5f - 0.5f * std::cos(kPi * u));
    }
};

size_t spanAt(const Buf& out, float at, float len, size_t& o) {
    o = smp(at);
    if (o >= out.size()) return 0;
    return std::min(smp(len), out.size() - o);
}

void mixInto(Buf& dst, const Buf& src, float at, float gain) {
    size_t o = smp(at);
    if (o >= dst.size()) return;
    size_t n = std::min(src.size(), dst.size() - o);
    for (size_t i = 0; i < n; i++) dst[o + i] += src[i] * gain;
}

float peakOf(const Buf& s) {
    float m = 0.0f;
    for (float v : s) m = std::max(m, std::fabs(v));
    return m;
}
void gainBy(Buf& s, float g) {
    for (float& v : s) v *= g;
}
void softDrive(Buf& s, float drive) {
    float n = 1.0f / std::tanh(drive);
    for (float& v : s) v = std::tanh(v * drive) * n;
}

// Band-limited noise burst; `amp` is roughly the peak level whatever the bandwidth.
void addNoise(Buf& out, float at, float attack, float tau, float amp, float hp, float lp, Rng& r) {
    size_t o, n = spanAt(out, at, attack + tau * 7.0f, o);
    Biquad fh = biquad(BQ_HP, hp), fl = biquad(BQ_LP, lp);
    Env e(attack, tau);
    float top = std::min(lp, 20000.0f), bw = std::max(top - hp, top * 0.25f);
    float g = amp * 0.52f / std::sqrt(clampf(bw / 24000.0f, 0.002f, 1.0f));
    for (size_t i = 0; i < n; i++) out[o + i] += fl(fh(white(r) * e.next())) * g;
}

// Sine with an exponential pitch drop: the low "boom" of shots, thuds and drums.
void addThump(Buf& out, float at, float f0, float f1, float pitchTau, float attack, float tau, float amp) {
    size_t o, n = spanAt(out, at, attack + tau * 7.0f, o);
    Env e(attack, tau);
    float pk = decayCoef(pitchTau), pd = 1.0f, ph = 0.0f;
    for (size_t i = 0; i < n; i++) {
        ph += kTwoPi * (f1 + (f0 - f1) * pd) / kRate;
        if (ph > kTwoPi) ph -= kTwoPi;
        pd *= pk;
        out[o + i] += std::sin(ph) * e.next() * amp;
    }
}

struct Mode {
    float f, tau, amp;
};

// Modal synthesis: damped resonators struck by a raised-sine pulse. Longer pulses (soft hits) excite fewer high modes.
void addModes(Buf& out, float at, std::initializer_list<Mode> modes, float amp, float hardness, Rng& r, float pitch = 1.0f,
              float jitter = 0.02f) {
    size_t o = smp(at);
    if (o >= out.size()) return;
    int ne = std::max(1, std::min(256, (int)(hardness * kRate + 0.5f)));
    float ex[256], sum = 0.0f;
    for (int k = 0; k < ne; k++) sum += ex[k] = std::sin(kPi * ((float)k + 0.5f) / (float)ne);
    for (int k = 0; k < ne; k++) ex[k] /= sum;
    for (const Mode& m : modes) {
        float f = m.f * pitch * (1.0f + jitter * white(r));
        if (f < 20.0f || f > kRate * 0.45f) continue;
        float w = kTwoPi * f / kRate, rr = decayCoef(m.tau), c = 2.0f * rr * std::cos(w), r2 = rr * rr;
        float g = std::sin(w) * m.amp * amp;
        size_t n = std::min(smp(m.tau * 7.0f) + (size_t)ne, out.size() - o);
        float y1 = 0.0f, y2 = 0.0f;
        for (size_t i = 0; i < n; i++) {
            float y = (i < (size_t)ne ? ex[i] * g : 0.0f) + c * y1 - r2 * y2;
            y2 = y1;
            y1 = y;
            out[o + i] += y;
        }
    }
}

// Cloud of micro-impacts (sand, gravel, debris, splinters) band-limited to [lo, hi].
void addGrains(Buf& out, float at, float dur, float rate, float amp, float lo, float hi, Rng& r, float shape = 3.0f) {
    size_t o, n = spanAt(out, at, dur + 0.01f, o);
    Biquad h1 = biquad(BQ_HP, lo), h2 = h1, l1 = biquad(BQ_LP, hi), l2 = l1;
    float p = rate / kRate, k = decayCoef(dur / shape), e = 1.0f;
    for (size_t i = 0; i < n; i++) {
        float x = 0.0f;
        if (r.f01() < p * e) {
            float a = r.f01();
            x = ((r.next() & 1) ? a : -a) * a * 4.0f;  // mostly small grains, a few big ones
        }
        e *= k;
        out[o + i] += l2(l1(h2(h1(x)))) * amp;
    }
}

// Friction: rough noise through a resonant band-pass sweeping f0 -> f1 (metal sliding, scuffs).
void addScrape(Buf& out, float at, float dur, float amp, float f0, float f1, float q, float rough, Rng& r) {
    size_t o, n = spanAt(out, at, dur, o);
    Svf f;
    float k = 1.0f / q;
    for (size_t i = 0; i < n; i++) {
        float u = (float)i / (float)n;
        float env = std::sin(kPi * std::pow(u, 0.6f));
        float x = white(r) * (1.0f + (r.f01() < 0.03f ? rough * 6.0f : 0.0f));
        f.run(x, svfG(f0 * std::pow(f1 / f0, u)), k);
        out[o + i] += f.bp * k * env * amp;
    }
}

// Clothing / gear rustle.
void addRustle(Buf& out, float at, float dur, float amp, Rng& r) {
    size_t o, n = spanAt(out, at, dur, o);
    Biquad h = biquad(BQ_HP, 700.0f), l = biquad(BQ_LP, 5500.0f);
    float m = 0.0f, mk = onePoleCoef(60.0f);
    for (size_t i = 0; i < n; i++) {
        float u = (float)i / (float)n, env = std::sin(kPi * u);
        m += mk * (r.f01() - m);
        float x = white(r) * (0.3f + 2.5f * m * m) * (r.f01() < 0.004f ? 3.0f : 1.0f);
        out[o + i] += l(h(x)) * env * env * amp;
    }
}

// Wet flesh: band-passed noise with a falling centre and fast random amplitude flutter.
void addSquelch(Buf& out, float at, float dur, float amp, float f0, float f1, Rng& r) {
    size_t o, n = spanAt(out, at, dur, o);
    Svf f;
    float m = 0.0f, mk = onePoleCoef(180.0f);
    for (size_t i = 0; i < n; i++) {
        float u = (float)i / (float)n;
        float env = (1.0f - u) * (1.0f - u) * std::min(1.0f, u * 25.0f);
        m += mk * (white(r) - m);
        f.run(white(r) * (0.4f + 6.0f * std::fabs(m)), svfG(f0 * std::pow(f1 / f0, std::sqrt(u))), 0.4f);
        out[o + i] += f.bp * 0.4f * env * amp;
    }
}

// Air swish: band-pass sweep with a bell envelope and an optional narrow whistle an octave up.
void addWhoosh(Buf& out, float at, float dur, float amp, float fLo, float fHi, float q, float whistle, Rng& r) {
    size_t o, n = spanAt(out, at, dur, o);
    Svf a, b;
    for (size_t i = 0; i < n; i++) {
        float u = (float)i / (float)n;
        float sh = std::sin(kPi * std::pow(u, 0.75f));
        float fc = fLo + (fHi - fLo) * sh * sh, x = white(r);
        a.run(x, svfG(fc), 1.0f / q);
        float y = a.bp / q;
        if (whistle > 0.0f) {
            b.run(x, svfG(fc * 2.2f), 1.0f / 8.0f);
            y += b.bp / 8.0f * whistle;
        }
        out[o + i] += y * std::pow(sh, 1.5f) * amp;
    }
}

// Friedlander overpressure pulse with zero net impulse: the core of every muzzle blast and explosion.
void addBlast(Buf& out, float at, float T, float amp, float lpHz) {
    size_t o, n = spanAt(out, at, T * 12.0f + 0.002f, o);
    Biquad l1 = biquad(BQ_LP, lpHz), l2 = l1;
    float k = decayCoef(T), e = 1.0f;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kRate;
        out[o + i] += l2(l1((1.0f - t / T) * e)) * amp;
        e *= k;
    }
}

// Supersonic N-wave: instant rise, linear fall to -1, instant return.
void addNWave(Buf& out, float at, float len, float amp, float lpHz) {
    size_t o, n = spanAt(out, at, len + 0.001f, o);
    size_t m = std::max<size_t>(2, smp(len));
    Biquad l1 = biquad(BQ_LP, lpHz);
    for (size_t i = 0; i < n; i++) out[o + i] += l1(i < m ? 1.0f - 2.0f * (float)i / (float)(m - 1) : 0.0f) * amp;
}

// Start of a buffer with a faded end, padded for filter ringing.
Buf segment(const Buf& src, float len) {
    size_t sl = std::min(smp(len), src.size());
    Buf c(src.begin(), src.begin() + (long)sl);
    size_t f0 = sl * 6 / 10;
    for (size_t i = f0; i < sl; i++) c[i] *= 0.5f + 0.5f * std::cos(kPi * (float)(i - f0) / (float)(sl - f0));
    c.resize(sl + smp(0.01f), 0.0f);
    return c;
}

struct Tap {
    float t, g, lp;
};

// Fixed early reflections of the start of `src` (ground bounce, nearby walls): thickens a bang without tonal ringing.
void addTaps(Buf& out, const Buf& src, float srcLen, std::initializer_list<Tap> taps, float gain, Rng& r) {
    Buf seg = segment(src, srcLen);
    for (const Tap& tp : taps) {
        Buf c = seg;
        filt(c, BQ_LP, tp.lp);
        mixInto(out, c, tp.t * r.range(0.85f, 1.15f), tp.g * gain);
    }
}

// Sparse reflections: delayed copies of the start of `src` that get darker with time (outdoor slapback, rolling tail).
void addEchoes(Buf& out, const Buf& src, float srcLen, int count, float t0, float t1, float tau, float amp, float cut0, float cut1,
               Rng& r) {
    size_t sl = std::min(smp(srcLen), src.size());
    if (sl < 16 || count <= 0) return;
    Buf v[3];
    const float cuts[3] = {cut0, std::sqrt(cut0 * cut1), cut1};
    for (int k = 0; k < 3; k++) {
        v[k] = segment(src, srcLen);
        filt(v[k], BQ_LP, cuts[k]);
        filt(v[k], BQ_LP, cuts[k]);
        filt(v[k], BQ_HP, 80.0f);
    }
    for (int k = 0; k < count; k++) {
        float u = ((float)k + r.f01()) / (float)count;
        float t = t0 + (t1 - t0) * std::pow(u, 1.35f);
        float g = amp * std::exp(-(t - t0) / tau) * r.range(0.4f, 1.0f);
        mixInto(out, v[u < 0.2f ? 0 : (u < 0.55f ? 1 : 2)], t, g);
    }
}

// Diffuse reverberant noise that darkens over time (cut0 -> cut1) with slow random swells.
void addWash(Buf& out, float at, float attack, float tau, float amp, float cut0, float cut1, float modHz, float depth, Rng& r) {
    size_t o, n = spanAt(out, at, attack + tau * 6.0f, o);
    Svf f;
    Biquad hp = biquad(BQ_HP, 50.0f);
    Env e(attack, tau);
    Drift mod(r, modHz);
    float ck = decayCoef(tau * 1.2f), cd = 1.0f, g = svfG(cut0);
    for (size_t i = 0; i < n; i++) {
        if ((i & 15) == 0) g = svfG(cut1 + (cut0 - cut1) * cd);
        cd *= ck;
        f.run(white(r), g, 1.3f);
        out[o + i] += hp(f.lp) * e.next() * std::max(0.0f, 1.0f + depth * mod.next()) * amp;
    }
}

// Removes DC, normalizes to `peak`, trims inaudible tail and fades the end.
Buf finish(Buf s, float peak) {
    float x1 = 0.0f, y1 = 0.0f, R = 1.0f - kTwoPi * 12.0f / kRate;
    for (float& v : s) {
        float y = v - x1 + R * y1;
        x1 = v;
        y1 = y;
        v = y;
    }
    gainBy(s, peak / std::max(peakOf(s), 1e-9f));
    float th = peak * 2e-4f;
    size_t end = s.size();
    while (end > 0 && std::fabs(s[end - 1]) < th) end--;
    s.resize(std::min(s.size(), end + smp(0.01f)));
    size_t nf = std::min(s.size(), smp(0.006f));
    for (size_t i = 0; i < nf; i++) s[s.size() - 1 - i] *= (float)i / (float)nf;
    if (s.size() < 4) s.assign(4, 0.0f);
    return s;
}

// Loudest 50 ms RMS.
float loudOf(const Buf& s) {
    size_t w = std::max<size_t>(1, smp(0.05f));
    double acc = 0.0, best = 0.0;
    for (size_t i = 0; i < s.size(); i++) {
        acc += (double)s[i] * s[i];
        if (i >= w) acc -= (double)s[i - w] * s[i - w];
        best = std::max(best, acc);
    }
    return (float)std::sqrt(best / (double)w);
}

// Like finish(), but matches loudness (50 ms RMS in dBFS) so random variants play equally loud; the peak is capped.
Buf finishLoud(Buf s, float rmsDb, float peakCap) {
    s = finish(std::move(s), 1.0f);
    float g = std::pow(10.0f, rmsDb / 20.0f) / std::max(loudOf(s), 1e-9f);
    g = std::min(g, peakCap / std::max(peakOf(s), 1e-9f));
    gainBy(s, g);
    return s;
}

// ------------------------------------------------------------------------------------------------ foley building blocks

// Small steel click (trigger, button, latch).
void click(Buf& s, float at, float amp, float pitch, Rng& r) {
    addModes(s, at, {{2350, 0.007f, 1.0f}, {3720, 0.0055f, 0.8f}, {5230, 0.004f, 0.6f}, {7150, 0.003f, 0.45f}}, amp, 0.00012f, r, pitch, 0.04f);
    addNoise(s, at, 0.00005f, 0.0012f, amp * 0.6f, 2500.0f, 14000.0f, r);
}

// Heavier steel-on-steel impact (bolt, slide, magazine seating).
void clack(Buf& s, float at, float amp, float pitch, Rng& r) {
    addModes(s, at, {{980, 0.02f, 0.6f}, {1640, 0.017f, 1.0f}, {2480, 0.014f, 0.8f}, {3560, 0.011f, 0.6f}, {4870, 0.008f, 0.45f}, {6600, 0.006f, 0.3f}},
             amp, 0.00025f, r, pitch, 0.04f);
    addNoise(s, at, 0.0001f, 0.0025f, amp * 0.8f, 1200.0f, 12000.0f, r);
    addThump(s, at, 360.0f * pitch, 240.0f * pitch, 0.005f, 0.0002f, 0.009f, amp * 0.35f);
}

// Polymer / hand knock.
void thock(Buf& s, float at, float amp, float f, Rng& r) {
    addModes(s, at, {{f, 0.012f, 1.0f}, {f * 2.31f, 0.008f, 0.6f}, {f * 3.87f, 0.005f, 0.35f}}, amp, 0.0006f, r, 1.0f, 0.05f);
    addNoise(s, at, 0.0002f, 0.004f, amp * 0.4f, 200.0f, 3500.0f, r);
}

// Loose gear rattling.
void rattle(Buf& s, float at, float dur, int count, float amp, Rng& r) {
    for (int k = 0; k < count; k++) {
        float t = at + dur * std::pow(r.f01(), 1.4f);
        addModes(s, t, {{2600, 0.01f, 1.0f}, {4100, 0.007f, 0.7f}, {5900, 0.005f, 0.5f}}, amp * r.range(0.3f, 1.0f), 0.0002f, r, r.range(0.8f, 1.35f), 0.05f);
    }
}

// Small stone chip.
void chip(Buf& s, float at, float amp, float pitch, Rng& r) {
    addModes(s, at, {{1850, 0.008f, 1.0f}, {2900, 0.007f, 0.8f}, {4350, 0.005f, 0.6f}, {6100, 0.004f, 0.4f}}, amp, 0.0001f, r, pitch, 0.06f);
}

// ------------------------------------------------------------------------------------------------ gunshots

enum Mech { MECH_NONE, MECH_AK, MECH_M4, MECH_AWP, MECH_DEAGLE, MECH_GLOCK, MECH_USP };

struct Gun {
    float len;                                             // buffer length (s)
    float blastT, blastAmp, blastLp;                       // muzzle blast: Friedlander positive phase, level, smoothing
    float burstTau, burstLo, burstHi, burstAmp;            // turbulent muzzle gas
    float crackLen, crackAmp, sizzleTau, sizzleAmp;        // supersonic N-wave + high sizzle (or suppressor hiss)
    float earlyAmp;                                        // ground bounce and nearby walls
    float bodyF0, bodyF1, bodyPitchTau, bodyTau, bodyAmp;  // short low punch
    float rumbleCut, rumbleTau, rumbleAmp;                 // low noise body
    float voiceHz, voiceDb;                                // weapon character EQ
    float drive;                                           // saturation of the report
    float mechAmp;                                         // action level
    float echoCount, echoAmp, echoT0, echoT1, echoTau;     // outdoor reflections
    float washAmp, washTau, washCut, rollAmp;              // diffuse tail + low rolling rumble
    float loud, level;                                     // target loudness (50 ms RMS dBFS), peak cap
    int mech;
};

// Bolt carrier / slide noises, relative to a normalized report.
void addAction(Buf& m, int mech, Rng& r) {
    switch (mech) {
        case MECH_AK:  // heavy long-stroke carrier: slams back, loose rattle, forward and lock
            clack(m, 0.026f, 1.0f, 0.78f, r);
            rattle(m, 0.032f, 0.035f, 4, 0.25f, r);
            clack(m, 0.068f, 0.85f, 0.86f, r);
            addThump(m, 0.068f, 420.0f, 260.0f, 0.008f, 0.0003f, 0.012f, 0.35f);
            break;
        case MECH_M4:  // light bolt plus the buffer-spring twang
            click(m, 0.02f, 0.8f, 0.9f, r);
            addModes(m, 0.022f, {{1320, 0.11f, 1.0f}, {2710, 0.07f, 0.55f}, {4060, 0.045f, 0.3f}}, 0.22f, 0.0005f, r, 1.0f, 0.01f);
            clack(m, 0.052f, 0.7f, 1.12f, r);
            break;
        case MECH_AWP:  // bolt action: only the receiver and scope ring
            addModes(m, 0.004f, {{2950, 0.07f, 1.0f}, {4630, 0.05f, 0.6f}, {6900, 0.03f, 0.4f}}, 0.25f, 0.0003f, r);
            break;
        case MECH_DEAGLE:  // massive gas-operated slide
            clack(m, 0.03f, 1.0f, 0.82f, r);
            clack(m, 0.064f, 0.8f, 0.92f, r);
            break;
        case MECH_GLOCK:  // polymer frame: short, dry slide
            clack(m, 0.018f, 0.8f, 1.15f, r);
            click(m, 0.04f, 0.6f, 1.0f, r);
            break;
        case MECH_USP:  // with the report suppressed the slide is the loudest part
            clack(m, 0.012f, 1.0f, 1.05f, r);
            click(m, 0.036f, 0.8f, 0.95f, r);
            addThump(m, 0.036f, 380.0f, 260.0f, 0.006f, 0.0003f, 0.01f, 0.3f);
            break;
        default: break;
    }
}

// Layered shot: blast + gas + crack with early reflections and a short punch (saturated together), then the action,
// outdoor reflections and a noisy tail.
Buf gunshot(Gun g, uint64_t seed) {
    Rng r(seed);
    g.bodyF0 *= r.range(0.96f, 1.04f);
    g.bodyF1 *= r.range(0.97f, 1.03f);
    g.bodyTau *= r.range(0.92f, 1.08f);
    g.burstTau *= r.range(0.9f, 1.1f);
    g.voiceHz *= r.range(0.95f, 1.05f);
    size_t n = smp(g.len);
    Buf rep(n, 0.0f), dry(n, 0.0f), mech(n, 0.0f), tail(n, 0.0f);
    addBlast(rep, 0.0f, g.blastT, g.blastAmp, g.blastLp);
    addNoise(rep, 0.0f, 0.00015f, g.burstTau, g.burstAmp, g.burstLo, g.burstHi, r);
    if (g.crackAmp > 0.0f) addNWave(rep, 0.0f, g.crackLen, g.crackAmp, 15000.0f);
    if (g.sizzleAmp > 0.0f) addNoise(rep, 0.0f, 0.00005f, g.sizzleTau, g.sizzleAmp, 2800.0f, 14000.0f, r);
    addTaps(dry, rep, 0.018f, {{0.0045f, 0.5f, 7000}, {0.009f, 0.28f, 5000}, {0.014f, 0.22f, 4000}, {0.022f, 0.16f, 3000}, {0.031f, 0.12f, 2500}, {0.043f, 0.09f, 2000}},
            g.earlyAmp, r);
    for (size_t i = 0; i < n; i++) dry[i] += rep[i];
    addThump(dry, 0.0f, g.bodyF0, g.bodyF1, g.bodyPitchTau, 0.0005f, g.bodyTau, g.bodyAmp);
    addNoise(dry, 0.0f, 0.0008f, g.rumbleTau, g.rumbleAmp, 30.0f, g.rumbleCut, r);
    if (g.voiceDb != 0.0f) filt(dry, BQ_PEAK, g.voiceHz, 0.9f, g.voiceDb);
    gainBy(dry, 1.0f / std::max(peakOf(dry), 1e-6f));
    softDrive(dry, g.drive);
    addAction(mech, g.mech, r);
    addEchoes(tail, dry, 0.05f, (int)g.echoCount, g.echoT0, g.echoT1, g.echoTau, g.echoAmp, 3800.0f, 650.0f, r);
    addWash(tail, 0.003f, 0.015f, g.washTau, g.washAmp, g.washCut, g.washCut * 0.15f, 11.0f, 0.55f, r);
    if (g.rollAmp > 0.0f) addWash(tail, 0.06f, 0.12f, g.washTau * 1.6f, g.rollAmp, 320.0f, 160.0f, 4.0f, 0.85f, r);
    for (size_t i = 0; i < n; i++) dry[i] += mech[i] * g.mechAmp + tail[i];
    return finishLoud(std::move(dry), g.loud, g.level);
}

// clang-format off
// len | blast T, amp, lp | burst tau, lo, hi, amp | crack len, amp, sizzle tau, amp | early | body f0, f1, pitchTau, tau, amp |
// rumble cut, tau, amp | voice hz, db | drive | mech amp | echoes n, amp, t0, t1, tau | wash amp, tau, cut, roll | loud, level | mech
const Gun kGunAK = {1.05f, 0.0011f, 1.0f, 12000, 0.018f, 200, 5000, 0.7f, 0.0003f, 0.45f, 0.004f, 0.35f, 1.0f, 190, 62, 0.012f, 0.045f, 0.35f,
                    700, 0.07f, 0.55f, 350, 3.0f, 3.0f, 0.16f, 18, 0.25f, 0.07f, 0.85f, 0.25f, 0.12f, 0.3f, 2200, 0.0f, -7.5f, 1.0f, MECH_AK};
const Gun kGunM4 = {0.95f, 0.0008f, 1.0f, 13000, 0.014f, 350, 7000, 0.75f, 0.00022f, 0.55f, 0.0045f, 0.45f, 0.9f, 220, 75, 0.01f, 0.035f, 0.28f,
                    900, 0.05f, 0.4f, 1400, 3.0f, 3.5f, 0.2f, 16, 0.22f, 0.06f, 0.75f, 0.22f, 0.1f, 0.26f, 2600, 0.0f, -8.5f, 1.0f, MECH_M4};
const Gun kGunAWP = {2.3f, 0.0016f, 1.0f, 10000, 0.02f, 150, 5000, 0.8f, 0.00035f, 0.6f, 0.006f, 0.45f, 1.1f, 160, 45, 0.02f, 0.07f, 0.35f,
                    500, 0.14f, 0.7f, 250, 3.0f, 3.2f, 0.15f, 34, 0.35f, 0.09f, 2.0f, 0.55f, 0.16f, 0.6f, 1800, 0.3f, -5.5f, 1.0f, MECH_AWP};
const Gun kGunDeagle = {1.1f, 0.001f, 1.0f, 13000, 0.015f, 300, 8000, 0.8f, 0.00028f, 0.6f, 0.005f, 0.5f, 1.0f, 200, 64, 0.012f, 0.05f, 0.35f,
                    800, 0.08f, 0.5f, 1000, 3.0f, 3.2f, 0.22f, 20, 0.28f, 0.07f, 1.0f, 0.28f, 0.12f, 0.32f, 2400, 0.0f, -7.5f, 1.0f, MECH_DEAGLE};
const Gun kGunGlock = {0.55f, 0.0006f, 1.0f, 13000, 0.008f, 500, 8000, 0.6f, 0.0002f, 0.3f, 0.003f, 0.3f, 0.8f, 280, 120, 0.008f, 0.025f, 0.22f,
                    1400, 0.035f, 0.35f, 1700, 3.0f, 2.6f, 0.25f, 10, 0.2f, 0.05f, 0.45f, 0.13f, 0.07f, 0.14f, 2800, 0.0f, -11.5f, 0.95f, MECH_GLOCK};
const Gun kGunUSP = {0.4f, 0.0022f, 0.7f, 1500, 0.022f, 250, 2200, 0.45f, 0.0f, 0.0f, 0.025f, 0.08f, 0.5f, 230, 100, 0.008f, 0.02f, 0.3f,
                    600, 0.03f, 0.4f, 700, 2.0f, 2.0f, 0.55f, 4, 0.08f, 0.03f, 0.2f, 0.06f, 0.03f, 0.06f, 1500, 0.0f, -15.0f, 0.55f, MECH_USP};
// clang-format on

// ------------------------------------------------------------------------------------------------ weapon handling

Buf dryFire(Rng& r) {
    Buf s(smp(0.12f), 0.0f);
    click(s, 0.0f, 0.35f, 1.2f, r);  // trigger breaks
    addModes(s, 0.011f, {{2900, 0.012f, 1.0f}, {4600, 0.009f, 0.8f}, {6800, 0.006f, 0.6f}, {8900, 0.004f, 0.4f}}, 1.0f, 0.0001f, r);
    addNoise(s, 0.011f, 0.00005f, 0.0015f, 0.5f, 3000.0f, 14000.0f, r);  // striker hits an empty chamber
    addThump(s, 0.011f, 500.0f, 380.0f, 0.005f, 0.0002f, 0.006f, 0.25f);
    return finish(std::move(s), 0.6f);
}

// Magazine release + magazine sliding out. The release at 0.45 s matches the rifle animation (20% of the reload).
Buf reloadOut(Rng& r) {
    Buf s(smp(0.85f), 0.0f);
    addRustle(s, 0.0f, 0.18f, 0.12f, r);
    thock(s, 0.12f, 0.25f, 700.0f, r);
    click(s, 0.45f, 0.5f, 1.0f, r);
    addScrape(s, 0.46f, 0.14f, 0.35f, 1700.0f, 1100.0f, 5.0f, 0.6f, r);
    addModes(s, 0.46f, {{1250, 0.03f, 1.0f}, {2100, 0.02f, 0.6f}}, 0.2f, 0.001f, r);
    thock(s, 0.61f, 0.3f, 450.0f, r);
    addRustle(s, 0.64f, 0.15f, 0.08f, r);
    return finish(std::move(s), 0.7f);
}

// Magazine seats with a solid click, then the charging handle / slide is pulled and released (~0.5 s / ~0.72 s).
Buf reloadIn(Rng& r) {
    Buf s(smp(1.0f), 0.0f);
    addScrape(s, 0.0f, 0.045f, 0.35f, 1200.0f, 1900.0f, 4.0f, 0.6f, r);
    clack(s, 0.045f, 1.0f, 0.9f, r);
    addThump(s, 0.045f, 360.0f, 240.0f, 0.006f, 0.0002f, 0.012f, 0.5f);
    addModes(s, 0.05f, {{3100, 0.02f, 1.0f}, {4700, 0.015f, 0.6f}}, 0.2f, 0.0002f, r);
    thock(s, 0.12f, 0.2f, 600.0f, r);
    click(s, 0.47f, 0.35f, 0.9f, r);
    addScrape(s, 0.48f, 0.1f, 0.4f, 1400.0f, 2600.0f, 6.0f, 0.4f, r);
    clack(s, 0.585f, 0.55f, 1.1f, r);
    addScrape(s, 0.705f, 0.02f, 0.35f, 2600.0f, 1600.0f, 5.0f, 0.3f, r);
    clack(s, 0.725f, 1.0f, 0.8f, r);
    addThump(s, 0.725f, 300.0f, 200.0f, 0.006f, 0.0002f, 0.015f, 0.45f);
    addModes(s, 0.725f, {{1850, 0.08f, 1.0f}, {3350, 0.05f, 0.6f}}, 0.12f, 0.0003f, r);
    return finish(std::move(s), 0.75f);
}

// AWP bolt cycle, timed to the viewmodel (played 0.4 s after the shot).
Buf boltCycle(Rng& r) {
    Buf s(smp(0.8f), 0.0f);
    click(s, 0.0f, 0.6f, 0.85f, r);  // handle lifts
    addScrape(s, 0.025f, 0.13f, 0.45f, 1100.0f, 2300.0f, 5.0f, 0.5f, r);
    clack(s, 0.16f, 0.9f, 0.9f, r);  // rear stop
    addModes(s, 0.19f, {{3900, 0.05f, 1.0f}, {6100, 0.035f, 0.6f}, {8300, 0.02f, 0.4f}}, 0.18f, 0.0002f, r);  // casing ejects
    addModes(s, 0.55f, {{3900, 0.04f, 1.0f}, {6100, 0.03f, 0.6f}}, 0.07f, 0.0002f, r);                      // and lands
    addScrape(s, 0.23f, 0.14f, 0.45f, 2300.0f, 1200.0f, 5.0f, 0.5f, r);
    clack(s, 0.375f, 0.7f, 1.0f, r);  // round chambered
    clack(s, 0.5f, 1.0f, 0.8f, r);    // handle down, locked
    addThump(s, 0.5f, 320.0f, 220.0f, 0.006f, 0.0002f, 0.012f, 0.5f);
    return finish(std::move(s), 0.8f);
}

Buf deploySound(Rng& r) {
    Buf s(smp(0.45f), 0.0f);
    addRustle(s, 0.0f, 0.22f, 0.3f, r);
    thock(s, 0.09f, 0.35f, 650.0f, r);
    rattle(s, 0.1f, 0.08f, 3, 0.15f, r);
    click(s, 0.21f, 0.5f, 1.0f, r);
    clack(s, 0.24f, 0.6f, 1.1f, r);
    return finish(std::move(s), 0.6f);
}

Buf zoomSound(Rng& r) {
    Buf s(smp(0.1f), 0.0f);
    click(s, 0.0f, 0.8f, 1.4f, r);
    addScrape(s, 0.005f, 0.05f, 0.3f, 3000.0f, 5200.0f, 3.0f, 0.2f, r);
    click(s, 0.05f, 0.4f, 1.6f, r);
    return finish(std::move(s), 0.5f);
}

Buf knifeSwing(Rng& r) {
    Buf s(smp(0.32f), 0.0f);
    float dur = r.range(0.22f, 0.28f);
    addWhoosh(s, 0.0f, dur, 1.0f, r.range(300.0f, 380.0f), r.range(1700.0f, 2100.0f), 2.4f, 0.5f, r);
    addWhoosh(s, 0.01f, dur, 0.4f, 150.0f, 450.0f, 1.0f, 0.0f, r);
    return finishLoud(std::move(s), -16.0f, 0.7f);
}

// Knife against a wall: blade ring, stone chips, a short skid.
Buf knifeWall(Rng& r) {
    Buf s(smp(0.45f), 0.0f);
    addNoise(s, 0.0f, 0.0001f, 0.002f, 0.8f, 1200.0f, 12000.0f, r);
    addModes(s, 0.0f, {{2250, 0.06f, 1.0f}, {3720, 0.05f, 0.8f}, {5480, 0.035f, 0.6f}, {7930, 0.025f, 0.4f}, {9800, 0.015f, 0.3f}}, 0.5f, 0.0001f, r);
    addNoise(s, 0.0f, 0.0002f, 0.006f, 0.4f, 400.0f, 4000.0f, r);
    addThump(s, 0.0f, 220.0f, 140.0f, 0.005f, 0.0002f, 0.012f, 0.2f);
    addGrains(s, 0.002f, 0.12f, 900.0f, 0.3f, 1500.0f, 7000.0f, r);
    addScrape(s, 0.004f, 0.06f, 0.25f, 4200.0f, 3000.0f, 3.0f, 0.8f, r);
    return finishLoud(std::move(s), -14.5f, 0.85f);
}

// Knife into a body: slap, meaty thud, wet squelch, cloth tearing.
Buf knifeFlesh(Rng& r) {
    Buf s(smp(0.4f), 0.0f);
    addNoise(s, 0.0f, 0.0003f, 0.004f, 0.5f, 400.0f, 5000.0f, r);
    addNoise(s, 0.0f, 0.0005f, 0.015f, 0.55f, 200.0f, 2000.0f, r);
    addThump(s, 0.0f, 170.0f, 80.0f, 0.012f, 0.0008f, 0.025f, 0.35f);
    addNoise(s, 0.0f, 0.001f, 0.035f, 0.6f, 60.0f, 700.0f, r);
    addSquelch(s, 0.01f, 0.12f, 0.4f, 1800.0f, 500.0f, r);
    addNoise(s, 0.003f, 0.0005f, 0.015f, 0.25f, 2500.0f, 9000.0f, r);
    return finishLoud(std::move(s), -14.0f, 0.85f);
}

// ------------------------------------------------------------------------------------------------ movement

// Rubber sole on sand-covered stone: heel thud + crunch, then toe roll with a gritty scuff.
Buf footstep(Rng& r, int v) {
    Buf s(smp(0.3f), 0.0f);
    float toe = r.range(0.042f, 0.065f), p = 1.0f + 0.06f * ((float)v - 1.5f);
    addThump(s, 0.0f, 120.0f * p, 65.0f * p, 0.008f, 0.0006f, 0.012f, 0.25f);
    addNoise(s, 0.0f, 0.0006f, 0.01f, 0.55f, 70.0f, 600.0f * p, r);
    addNoise(s, 0.0f, 0.0002f, 0.004f, 0.35f, 900.0f, 6000.0f, r);
    addGrains(s, 0.001f, 0.045f, 2200.0f, 0.45f, 1300.0f, 6500.0f, r);
    addNoise(s, toe, 0.0008f, 0.008f, 0.3f, 90.0f, 900.0f * p, r);
    addGrains(s, toe, 0.07f, 1600.0f, 0.4f, 1600.0f, 7500.0f, r, 2.0f);
    addScrape(s, toe + 0.004f, r.range(0.045f, 0.07f), 0.2f, 3200.0f * p, 2100.0f * p, 1.1f, 0.8f, r);
    gainBy(s, 1.0f / std::max(peakOf(s), 1e-6f));
    softDrive(s, 1.6f);
    return finishLoud(std::move(s), -17.0f, 0.9f);
}

Buf jumpSound(Rng& r) {
    Buf s(smp(0.32f), 0.0f);
    addNoise(s, 0.0f, 0.001f, 0.01f, 0.35f, 80.0f, 800.0f, r);
    addGrains(s, 0.0f, 0.05f, 1800.0f, 0.35f, 1500.0f, 7000.0f, r);
    addScrape(s, 0.003f, 0.05f, 0.12f, 2800.0f, 1800.0f, 1.0f, 0.8f, r);
    addRustle(s, 0.01f, 0.2f, 0.25f, r);
    rattle(s, 0.03f, 0.15f, 6, 0.3f, r);
    return finish(std::move(s), 0.6f);
}

Buf landSound(Rng& r) {
    Buf s(smp(0.45f), 0.0f);
    addThump(s, 0.0f, 95.0f, 50.0f, 0.012f, 0.0008f, 0.03f, 0.35f);
    addNoise(s, 0.0f, 0.001f, 0.03f, 0.75f, 40.0f, 400.0f, r);
    addNoise(s, 0.0f, 0.0003f, 0.006f, 0.4f, 700.0f, 5000.0f, r);
    addGrains(s, 0.001f, 0.08f, 3000.0f, 0.6f, 1200.0f, 7000.0f, r);
    addScrape(s, 0.05f, 0.06f, 0.2f, 3000.0f, 1900.0f, 1.0f, 0.8f, r);
    rattle(s, 0.015f, 0.18f, 8, 0.3f, r);
    clack(s, 0.03f, 0.2f, 0.9f, r);
    addRustle(s, 0.0f, 0.22f, 0.2f, r);
    return finishLoud(std::move(s), -14.0f, 0.9f);
}

// ------------------------------------------------------------------------------------------------ hits and impacts

Buf hitBody(Rng& r) {
    Buf s(smp(0.25f), 0.0f);
    addNoise(s, 0.0f, 0.0001f, 0.0025f, 0.6f, 1000.0f, 7500.0f, r);
    addNoise(s, 0.0f, 0.0003f, 0.012f, 0.6f, 250.0f, 2500.0f, r);
    addThump(s, 0.0f, r.range(190.0f, 215.0f), 100.0f, 0.008f, 0.0004f, 0.018f, 0.35f);
    addNoise(s, 0.0f, 0.0006f, 0.022f, 0.6f, 70.0f, 900.0f, r);
    addSquelch(s, 0.003f, 0.08f, 0.35f, 1700.0f, 550.0f, r);
    filt(s, BQ_PEAK, 430.0f, 1.0f, 3.0f);
    return finishLoud(std::move(s), -15.0f, 0.9f);
}

// Headshot without a helmet: bony snap, crunch and a crisp tick on top of the flesh hit.
Buf hitHead(Rng& r) {
    Buf s(smp(0.3f), 0.0f);
    addNWave(s, 0.0f, 0.00015f, 0.8f, 14000.0f);
    addNoise(s, 0.0f, 0.0001f, 0.004f, 0.7f, 2000.0f, 11000.0f, r);
    addGrains(s, 0.001f, 0.035f, 5000.0f, 0.5f, 1800.0f, 8000.0f, r);
    addModes(s, 0.0f, {{3150, 0.022f, 1.0f}, {4870, 0.016f, 0.7f}, {6400, 0.011f, 0.5f}}, 0.35f, 0.0001f, r);
    addNoise(s, 0.0f, 0.0003f, 0.01f, 0.5f, 300.0f, 3000.0f, r);
    addThump(s, 0.0f, 230.0f, 120.0f, 0.007f, 0.0004f, 0.015f, 0.25f);
    addNoise(s, 0.0f, 0.0006f, 0.018f, 0.4f, 80.0f, 1000.0f, r);
    return finishLoud(std::move(s), -15.0f, 0.9f);
}

// The helmet "ding": inharmonic steel-shell modes with a beating pair.
Buf helmetDing(Rng& r, float pitch) {
    Buf s(smp(0.8f), 0.0f);
    addNoise(s, 0.0f, 0.0001f, 0.0015f, 0.8f, 2500.0f, 14000.0f, r);
    addModes(s, 0.0f,
             {{2380, 0.30f, 1.0f}, {2398, 0.28f, 0.8f}, {3610, 0.2f, 0.7f}, {4970, 0.14f, 0.55f}, {6420, 0.09f, 0.4f}, {8150, 0.06f, 0.3f},
              {9900, 0.04f, 0.2f}},
             0.45f, 0.00008f, r, pitch, 0.005f);
    addThump(s, 0.0f, 300.0f, 200.0f, 0.004f, 0.0002f, 0.01f, 0.3f);
    return finishLoud(std::move(s), -14.0f, 0.7f);
}

Buf impactStone(Rng& r, float p) {
    Buf s(smp(0.4f), 0.0f);
    addNWave(s, 0.0f, 0.0001f, 0.3f, 15000.0f);
    addNoise(s, 0.0f, 0.0001f, 0.003f, 0.8f, 1500.0f, 12000.0f, r);
    addNoise(s, 0.0f, 0.0002f, 0.008f, 0.45f, 400.0f, 4000.0f, r);
    chip(s, 0.0f, 0.5f, p, r);
    addThump(s, 0.0f, 260.0f, 160.0f, 0.004f, 0.0002f, 0.008f, 0.2f);
    addGrains(s, 0.015f, 0.22f, 400.0f, 0.35f, 1800.0f, 8000.0f, r, 4.0f);  // debris patter
    addNoise(s, 0.002f, 0.004f, 0.05f, 0.15f, 3000.0f, 12000.0f, r);        // dust
    return finishLoud(std::move(s), -17.0f, 0.85f);
}

Buf impactWood(Rng& r, float p) {
    Buf s(smp(0.3f), 0.0f);
    addNoise(s, 0.0f, 0.0001f, 0.004f, 0.6f, 600.0f, 6000.0f, r);
    addNoise(s, 0.0f, 0.0002f, 0.01f, 0.4f, 200.0f, 1500.0f, r);
    addModes(s, 0.0f, {{390, 0.01f, 1.0f}, {720, 0.009f, 0.8f}, {1160, 0.008f, 0.7f}, {1850, 0.006f, 0.5f}, {2900, 0.004f, 0.35f}}, 0.9f, 0.0002f, r, p);
    addThump(s, 0.0f, 210.0f, 150.0f, 0.006f, 0.0003f, 0.01f, 0.2f);
    addGrains(s, 0.001f, 0.06f, 2500.0f, 0.3f, 1500.0f, 6000.0f, r);  // splinters
    return finishLoud(std::move(s), -17.0f, 0.85f);
}

// Falling ricochet whine.
void addRicochet(Buf& s, float at, float dur, float amp, float f0, float f1, Rng& r) {
    size_t o, n = spanAt(s, at, dur, o);
    float ph = 0.0f;
    Drift wob(r, 25.0f);
    for (size_t i = 0; i < n; i++) {
        float u = (float)i / (float)n;
        float f = f0 * std::pow(f1 / f0, u) * (1.0f + 0.01f * wob.next());
        ph += kTwoPi * f / kRate;
        if (ph > kTwoPi) ph -= kTwoPi;
        s[o + i] += std::sin(ph) * std::min(1.0f, u * 30.0f) * (1.0f - u) * (1.0f - u) * amp;
    }
}

Buf impactMetal(Rng& r, float p, bool ricochet) {
    Buf s(smp(0.6f), 0.0f);
    addNoise(s, 0.0f, 0.0001f, 0.0015f, 0.8f, 2000.0f, 14000.0f, r);
    addModes(s, 0.0f,
             {{920, 0.2f, 0.6f}, {1530, 0.16f, 0.8f}, {2390, 0.12f, 1.0f}, {3480, 0.09f, 0.8f}, {4790, 0.06f, 0.6f}, {6250, 0.04f, 0.4f}, {8100, 0.03f, 0.3f}},
             0.35f, 0.0001f, r, p);
    addThump(s, 0.0f, 350.0f, 250.0f, 0.004f, 0.0002f, 0.006f, 0.2f);
    if (ricochet) addRicochet(s, 0.01f, 0.35f, 0.16f, r.range(3800.0f, 4600.0f), r.range(1700.0f, 2100.0f), r);
    return finishLoud(std::move(s), -15.0f, 0.85f);
}

Buf impactSand(Rng& r) {
    Buf s(smp(0.3f), 0.0f);
    addNoise(s, 0.0f, 0.0005f, 0.012f, 0.5f, 60.0f, 700.0f, r);
    addThump(s, 0.0f, 130.0f, 80.0f, 0.008f, 0.0005f, 0.014f, 0.25f);
    addGrains(s, 0.002f, 0.12f, 6000.0f, 0.5f, 2000.0f, 9000.0f, r);  // sand spray
    addNoise(s, 0.002f, 0.003f, 0.06f, 0.3f, 2500.0f, 10000.0f, r);
    return finishLoud(std::move(s), -17.0f, 0.85f);
}

// ------------------------------------------------------------------------------------------------ grenades and bomb

Buf explosion(Rng& r) {
    size_t n = smp(3.4f);
    Buf dry(n, 0.0f), tail(n, 0.0f);
    addBlast(dry, 0.0f, 0.007f, 1.0f, 2500.0f);
    addBlast(dry, 0.0f, 0.0012f, 0.6f, 12000.0f);
    addNoise(dry, 0.0f, 0.0002f, 0.08f, 1.0f, 200.0f, 7000.0f, r);    // the main crash
    addGrains(dry, 0.0f, 0.12f, 6000.0f, 0.6f, 600.0f, 6000.0f, r);   // crackle of the fireball
    addThump(dry, 0.0f, 80.0f, 30.0f, 0.06f, 0.001f, 0.25f, 0.55f);
    addNoise(dry, 0.0f, 0.004f, 0.5f, 0.8f, 22.0f, 170.0f, r);        // sub rumble
    addNoise(dry, 0.0f, 0.002f, 0.15f, 0.6f, 60.0f, 800.0f, r);
    addTaps(dry, dry, 0.03f, {{0.006f, 0.5f, 5000}, {0.017f, 0.3f, 3500}, {0.031f, 0.22f, 2500}, {0.052f, 0.15f, 1800}}, 1.0f, r);
    gainBy(dry, 1.0f / std::max(peakOf(dry), 1e-6f));
    softDrive(dry, 3.0f);
    for (int k = 0; k < 70; k++) {  // stones and grit raining down
        float t = 0.12f + 1.6f * std::pow(r.f01(), 1.7f);
        chip(tail, t, 0.12f * std::exp(-(t - 0.12f) / 0.6f) * r.range(0.3f, 1.0f), r.range(0.7f, 1.6f), r);
    }
    addGrains(tail, 0.1f, 1.2f, 900.0f, 0.1f, 1200.0f, 7000.0f, r, 2.5f);
    addEchoes(tail, dry, 0.12f, 22, 0.1f, 2.4f, 0.55f, 0.32f, 2200.0f, 350.0f, r);
    addWash(tail, 0.01f, 0.05f, 0.7f, 0.12f, 1500.0f, 200.0f, 5.0f, 0.6f, r);
    addWash(tail, 0.05f, 0.15f, 1.0f, 0.2f, 260.0f, 120.0f, 3.0f, 0.8f, r);  // rolling rumble
    for (size_t i = 0; i < n; i++) dry[i] += tail[i];
    return finishLoud(std::move(dry), -5.0f, 0.98f);
}

// Sharp bang, canister ring, slapback and a tinnitus-like tone.
Buf flashbang(Rng& r) {
    size_t n = smp(2.4f);
    Buf dry(n, 0.0f), tail(n, 0.0f);
    addBlast(dry, 0.0f, 0.0007f, 1.0f, 14000.0f);
    addNWave(dry, 0.0f, 0.00018f, 0.45f, 16000.0f);
    addNoise(dry, 0.0f, 0.0001f, 0.014f, 0.9f, 600.0f, 12000.0f, r);
    addThump(dry, 0.0f, 150.0f, 62.0f, 0.02f, 0.0005f, 0.06f, 0.55f);
    addNoise(dry, 0.0f, 0.001f, 0.05f, 0.4f, 40.0f, 900.0f, r);
    gainBy(dry, 1.0f / std::max(peakOf(dry), 1e-6f));
    softDrive(dry, 3.5f);
    addModes(tail, 0.002f, {{2550, 0.12f, 1.0f}, {4180, 0.09f, 0.7f}, {5960, 0.06f, 0.5f}}, 0.12f, 0.0002f, r);
    addEchoes(tail, dry, 0.04f, 16, 0.08f, 1.3f, 0.32f, 0.3f, 4000.0f, 800.0f, r);
    addWash(tail, 0.003f, 0.01f, 0.3f, 0.08f, 3000.0f, 500.0f, 10.0f, 0.5f, r);
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kRate;
        float e = smooth01((t - 0.02f) / 0.25f) * std::exp(-std::max(0.0f, t - 0.3f) / 0.8f);
        tail[i] += (std::sin(kTwoPi * 3320.0f * t) + 0.6f * std::sin(kTwoPi * 3372.0f * t)) * e * 0.025f;
        dry[i] += tail[i];
    }
    return finishLoud(std::move(dry), -6.5f, 0.95f);
}

// Fuse pop, then pressurized gas hiss with turbulent swells.
Buf smokeSound(Rng& r) {
    size_t n = smp(3.2f);
    Buf s(n, 0.0f);
    click(s, 0.0f, 0.5f, 0.75f, r);
    addThump(s, 0.0f, 190.0f, 120.0f, 0.01f, 0.0005f, 0.015f, 0.35f);
    Biquad h1 = biquad(BQ_HP, 900.0f), h2 = h1, pk = biquad(BQ_PEAK, 4500.0f, 0.8f, 5.0f), top = biquad(BQ_LP, 9000.0f);
    Biquad l1 = biquad(BQ_LP, 380.0f), l2 = l1;
    Drift fast(r, 14.0f), slow(r, 3.0f);
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kRate;
        float e = smooth01((t - 0.03f) / 0.18f) * (t < 1.9f ? 1.0f : std::exp(-(t - 1.9f) / 0.35f));
        float mod = std::max(0.2f, 1.0f + 0.3f * fast.next() + 0.25f * slow.next());
        s[i] += (top(pk(h2(h1(white(r))))) * 0.5f + l2(l1(white(r))) * 1.2f) * e * mod;
    }
    return finishLoud(std::move(s), -19.0f, 0.45f);
}

// Grenade canister hitting the ground.
Buf bounceSound(Rng& r, float p) {
    Buf s(smp(0.32f), 0.0f);
    addNoise(s, 0.0f, 0.0001f, 0.002f, 0.45f, 800.0f, 9000.0f, r);
    addModes(s, 0.0f, {{1060, 0.06f, 1.0f}, {1790, 0.05f, 0.8f}, {2650, 0.04f, 0.7f}, {3920, 0.03f, 0.5f}, {5240, 0.02f, 0.35f}}, 0.45f, 0.0003f, r, p);
    addThump(s, 0.0f, 240.0f, 160.0f, 0.005f, 0.0003f, 0.012f, 0.35f);
    addGrains(s, 0.001f, 0.05f, 1500.0f, 0.15f, 1500.0f, 7000.0f, r);
    return finishLoud(std::move(s), -16.0f, 0.8f);
}

Buf throwSound(Rng& r) {
    Buf s(smp(0.4f), 0.0f);
    addWhoosh(s, 0.0f, 0.3f, 0.8f, 260.0f, 1150.0f, 1.1f, 0.0f, r);
    addModes(s, 0.015f, {{3300, 0.05f, 1.0f}, {5150, 0.035f, 0.7f}, {7400, 0.02f, 0.4f}}, 0.12f, 0.0001f, r);  // spoon flies off
    addRustle(s, 0.0f, 0.2f, 0.15f, r);
    return finish(std::move(s), 0.6f);
}

// Piezo beep: odd harmonics with a housing resonance and a relay tick.
Buf c4Beep(Rng& r) {
    Buf s(smp(0.13f), 0.0f);
    size_t n = smp(0.095f), na = smp(0.002f), nr = smp(0.01f);
    float ph = 0.0f, dp = kTwoPi * 2280.0f / kRate;
    for (size_t i = 0; i < n; i++) {
        float e = std::min(1.0f, std::min((float)i / (float)na, (float)(n - i) / (float)nr));
        s[i] += (std::sin(ph) + 0.33f * std::sin(3.0f * ph) + 0.12f * std::sin(5.0f * ph)) * e;
        ph += dp;
        if (ph > kTwoPi) ph -= kTwoPi;
    }
    filt(s, BQ_PEAK, 3600.0f, 2.0f, 4.0f);
    click(s, 0.0f, 0.15f, 1.0f, r);
    return finishLoud(std::move(s), -13.5f, 0.4f);
}

void addTone(Buf& s, float at, float dur, float f, float amp) {
    size_t o, n = spanAt(s, at, dur, o);
    float na = (float)smp(0.004f), nr = (float)smp(0.012f), ph = 0.0f, dp = kTwoPi * f / kRate;
    for (size_t i = 0; i < n; i++) {
        float e = std::min(1.0f, std::min((float)i / na, (float)(n - i) / nr));
        s[o + i] += (std::sin(ph) - std::sin(3.0f * ph) / 9.0f + std::sin(5.0f * ph) / 25.0f) * e * amp;
        ph += dp;
        if (ph > kTwoPi) ph -= kTwoPi;
    }
}

// Short radio transmission: squelch, alert tones, squelch tail, band-limited like a handheld radio.
Buf radioAlert(Rng& r, std::initializer_list<std::pair<float, float>> notes, float noteLen) {
    Buf s(smp(0.15f + noteLen * (float)notes.size() + 0.2f), 0.0f);
    addNoise(s, 0.0f, 0.001f, 0.02f, 0.5f, 900.0f, 3200.0f, r);
    click(s, 0.0f, 0.25f, 0.8f, r);
    float t = 0.05f;
    for (auto& nt : notes) {
        addTone(s, t, noteLen * 0.9f, nt.first, nt.second);
        t += noteLen;
    }
    addNoise(s, t + 0.02f, 0.001f, 0.03f, 0.4f, 900.0f, 3200.0f, r);
    click(s, t + 0.02f, 0.2f, 0.8f, r);
    softDrive(s, 1.4f);
    filt(s, BQ_HP, 350.0f);
    filt(s, BQ_HP, 350.0f);
    filt(s, BQ_LP, 3400.0f);
    filt(s, BQ_LP, 3400.0f);
    return finishLoud(std::move(s), -15.0f, 0.35f);
}

// ------------------------------------------------------------------------------------------------ music stingers

inline float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

// Three detuned band-limited saws through a low-pass that follows the envelope (pads, brass).
void addSynth(Buf& s, float at, float dur, float f, float amp, float attack, float release, float cutLo, float cutHi, Rng& r) {
    size_t o, n = spanAt(s, at, dur + release * 1.5f, o);
    float ph[3] = {r.f01(), r.f01(), r.f01()};
    const float det[3] = {1.0f, 1.0035f, 0.9965f};
    Svf flt;
    float relK = decayCoef(release / 3.0f), rel = 1.0f, g = svfG(cutLo);
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kRate, e;
        if (t < attack) e = smooth01(t / attack);
        else if (t < dur) e = 1.0f;
        else e = (rel *= relK);
        float x = 0.0f;
        for (int k = 0; k < 3; k++) {
            float dt = f * det[k] / kRate;
            x += 2.0f * ph[k] - 1.0f - polyBlep(ph[k], dt);
            ph[k] += dt;
            if (ph[k] >= 1.0f) ph[k] -= 1.0f;
        }
        if ((i & 15) == 0) g = svfG(cutLo + (cutHi - cutLo) * e);
        flt.run(x * (1.0f / 3.0f), g, 1.2f);
        s[o + i] += flt.lp * e * amp;
    }
}

// Plucked note: bright attack with a quickly closing filter.
void addPluck(Buf& s, float at, float f, float amp, float tau, Rng& r) {
    size_t o, n = spanAt(s, at, tau * 6.0f, o);
    float ph = r.f01(), dt = f / kRate, ck = decayCoef(tau * 0.35f), c = 1.0f, g = 0.0f;
    Svf flt;
    Env e(0.002f, tau);
    for (size_t i = 0; i < n; i++) {
        float x = 2.0f * ph - 1.0f - polyBlep(ph, dt);
        ph += dt;
        if (ph >= 1.0f) ph -= 1.0f;
        if ((i & 15) == 0) g = svfG(400.0f + 7000.0f * c);
        c *= ck;
        flt.run(x, g, 1.0f);
        s[o + i] += flt.lp * e.next() * amp;
    }
}

// Cinematic low hit.
void addHit(Buf& s, float at, float amp, Rng& r) {
    addThump(s, at, 110.0f, 45.0f, 0.03f, 0.001f, 0.15f, amp * 0.6f);
    addNoise(s, at, 0.0005f, 0.05f, amp * 0.35f, 60.0f, 2500.0f, r);
    addNoise(s, at, 0.002f, 0.35f, amp * 0.3f, 30.0f, 220.0f, r);
}

// Freeverb-style mono reverb baked into a buffer (extends it by `extra` seconds).
void bakeReverb(Buf& s, float extra, float room, float dampAmt, float wet) {
    static const int combT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int apT[4] = {556, 441, 341, 225};
    const float sc = (float)kRate / 44100.0f;
    Buf cb[8], ab[4];
    size_t ci[8] = {}, ai[4] = {};
    float store[8] = {};
    for (int k = 0; k < 8; k++) cb[k].assign((size_t)((float)combT[k] * sc), 0.0f);
    for (int k = 0; k < 4; k++) ab[k].assign((size_t)((float)apT[k] * sc), 0.0f);
    s.resize(s.size() + smp(extra), 0.0f);
    for (float& v : s) {
        float in = v * 0.015f, out = 0.0f;
        for (int k = 0; k < 8; k++) {
            float y = cb[k][ci[k]];
            store[k] = y * (1.0f - dampAmt) + store[k] * dampAmt;
            cb[k][ci[k]] = in + store[k] * room;
            if (++ci[k] >= cb[k].size()) ci[k] = 0;
            out += y;
        }
        for (int k = 0; k < 4; k++) {
            float bo = ab[k][ai[k]];
            ab[k][ai[k]] = out + bo * 0.5f;
            out = bo - out;
            if (++ai[k] >= ab[k].size()) ai[k] = 0;
        }
        v += out * wet * 3.0f;
    }
}

Buf roundStart(Rng& r) {
    Buf s(smp(1.4f), 0.0f);
    addHit(s, 0.0f, 0.7f, r);
    for (float f : {73.42f, 110.0f, 146.83f}) addSynth(s, 0.0f, 0.45f, f, 0.28f, 0.06f, 0.7f, 250.0f, 1600.0f, r);
    addPluck(s, 0.0f, 440.0f, 0.22f, 0.22f, r);
    addPluck(s, 0.11f, 587.33f, 0.25f, 0.3f, r);
    bakeReverb(s, 0.9f, 0.8f, 0.35f, 0.3f);
    return finishLoud(std::move(s), -14.0f, 0.6f);
}

Buf roundWin(Rng& r) {
    Buf s(smp(1.8f), 0.0f);
    addHit(s, 0.0f, 0.8f, r);
    for (float f : {146.83f, 185.0f, 220.0f, 293.66f}) addSynth(s, 0.0f, 0.8f, f, 0.2f, 0.04f, 1.0f, 350.0f, 3000.0f, r);
    const float arp[4] = {587.33f, 739.99f, 880.0f, 1174.66f};
    for (int k = 0; k < 4; k++) addPluck(s, 0.09f * (float)k, arp[k], 0.2f, 0.35f, r);
    addSynth(s, 0.3f, 0.7f, 880.0f, 0.06f, 0.3f, 0.9f, 1500.0f, 4000.0f, r);
    addSynth(s, 0.3f, 0.7f, 1174.66f, 0.05f, 0.3f, 0.9f, 1500.0f, 4000.0f, r);
    bakeReverb(s, 1.0f, 0.82f, 0.3f, 0.32f);
    return finishLoud(std::move(s), -14.0f, 0.6f);
}

Buf roundLose(Rng& r) {
    Buf s(smp(1.9f), 0.0f);
    addHit(s, 0.0f, 0.8f, r);
    for (float f : {146.83f, 174.61f, 220.0f}) addSynth(s, 0.0f, 0.42f, f, 0.22f, 0.05f, 0.35f, 250.0f, 1300.0f, r);
    for (float f : {116.54f, 146.83f, 174.61f}) addSynth(s, 0.45f, 0.6f, f, 0.22f, 0.08f, 1.0f, 220.0f, 1000.0f, r);
    const float arp[3] = {440.0f, 349.23f, 293.66f};
    for (int k = 0; k < 3; k++) addPluck(s, 0.15f * (float)k, arp[k], 0.2f, 0.4f, r);
    bakeReverb(s, 1.0f, 0.82f, 0.4f, 0.3f);
    return finishLoud(std::move(s), -14.0f, 0.6f);
}

// ------------------------------------------------------------------------------------------------ interface

Buf uiHover(Rng& r) {
    Buf s(smp(0.04f), 0.0f);
    addModes(s, 0.0f, {{2650, 0.0045f, 1.0f}, {5300, 0.002f, 0.3f}}, 1.0f, 0.0002f, r, 1.0f, 0.0f);
    addNoise(s, 0.0f, 0.00005f, 0.0006f, 0.15f, 3000.0f, 12000.0f, r);
    return finish(std::move(s), 0.35f);
}

Buf uiClick(Rng& r) {
    Buf s(smp(0.07f), 0.0f);
    addNoise(s, 0.0f, 0.00005f, 0.0012f, 0.5f, 2000.0f, 12000.0f, r);
    addModes(s, 0.0f, {{1250, 0.012f, 1.0f}, {2900, 0.006f, 0.4f}}, 0.8f, 0.0002f, r, 1.0f, 0.0f);
    addThump(s, 0.0f, 420.0f, 360.0f, 0.004f, 0.0002f, 0.006f, 0.3f);
    return finish(std::move(s), 0.6f);
}

// Gear handling plus a short confirmation chime.
Buf buySound(Rng& r) {
    Buf s(smp(0.4f), 0.0f);
    clack(s, 0.0f, 0.5f, 1.2f, r);
    rattle(s, 0.015f, 0.06f, 3, 0.2f, r);
    addModes(s, 0.04f, {{1568, 0.12f, 1.0f}, {3136, 0.06f, 0.3f}}, 0.25f, 0.0004f, r, 1.0f, 0.0f);
    addModes(s, 0.1f, {{2093, 0.14f, 1.0f}, {4186, 0.07f, 0.3f}}, 0.25f, 0.0004f, r, 1.0f, 0.0f);
    return finish(std::move(s), 0.5f);
}

Buf pickupSound(Rng& r) {
    Buf s(smp(0.32f), 0.0f);
    addRustle(s, 0.0f, 0.12f, 0.3f, r);
    clack(s, 0.05f, 0.7f, 1.0f, r);
    rattle(s, 0.06f, 0.1f, 3, 0.15f, r);
    click(s, 0.14f, 0.4f, 1.1f, r);
    return finish(std::move(s), 0.6f);
}

// Brass casing bouncing a few times on stone.
Buf shellSound(Rng& r) {
    Buf s(smp(0.45f), 0.0f);
    float t = 0.0f, a = 1.0f, gap = r.range(0.07f, 0.1f), p = r.range(0.92f, 1.1f);
    for (int k = 0; k < 4; k++) {
        addModes(s, t, {{3350, 0.05f, 0.6f}, {5480, 0.04f, 1.0f}, {7900, 0.03f, 0.7f}, {10300, 0.02f, 0.4f}}, a, 0.00008f, r, p, 0.01f);
        addNoise(s, t, 0.00005f, 0.0008f, a * 0.3f, 3000.0f, 14000.0f, r);
        t += gap;
        gap *= r.range(0.55f, 0.7f);
        a *= r.range(0.45f, 0.6f);
    }
    return finishLoud(std::move(s), -20.0f, 0.5f);
}

Buf killConfirm(Rng& r) {
    Buf s(smp(0.5f), 0.0f);
    addThump(s, 0.0f, 190.0f, 120.0f, 0.01f, 0.0005f, 0.03f, 0.6f);
    addModes(s, 0.005f, {{1318, 0.22f, 1.0f}, {2637, 0.12f, 0.45f}, {3951, 0.07f, 0.25f}, {5270, 0.05f, 0.1f}}, 0.35f, 0.0004f, r, 1.0f, 0.0f);
    return finishLoud(std::move(s), -15.0f, 0.55f);
}

// ------------------------------------------------------------------------------------------------ sound table

int variantCount(int id) {
    switch (id) {
        case SND_AK: case SND_M4: case SND_AWP: case SND_DEAGLE: case SND_GLOCK: case SND_USP:
        case SND_KNIFE_SWING: case SND_HIT_BODY: case SND_IMPACT_STONE: case SND_IMPACT_WOOD: case SND_IMPACT_METAL:
        case SND_IMPACT_SAND: case SND_BOUNCE: case SND_SHELL:
            return 3;
        case SND_STEP0: case SND_STEP1: case SND_STEP2: case SND_STEP3: case SND_KNIFE_HIT: case SND_KNIFE_STAB:
        case SND_HIT_HEAD: case SND_HELMET: case SND_LAND:
            return 2;
        default:
            return 1;
    }
}

Buf renderSound(int id, int v) {
    uint64_t seed = 0x9E3779B97F4A7C15ull * (uint64_t)(id + 1) + 7919ull * (uint64_t)(v + 1);
    Rng r(seed);
    const float pitches[3] = {1.0f, 0.93f, 1.07f};
    float p = pitches[v % 3];
    switch (id) {
        case SND_AK: return gunshot(kGunAK, seed);
        case SND_M4: return gunshot(kGunM4, seed);
        case SND_AWP: return gunshot(kGunAWP, seed);
        case SND_DEAGLE: return gunshot(kGunDeagle, seed);
        case SND_GLOCK: return gunshot(kGunGlock, seed);
        case SND_USP: return gunshot(kGunUSP, seed);
        case SND_KNIFE_SWING: return knifeSwing(r);
        case SND_KNIFE_HIT: return knifeWall(r);
        case SND_KNIFE_STAB: return knifeFlesh(r);
        case SND_EMPTY: return dryFire(r);
        case SND_RELOAD_OUT: return reloadOut(r);
        case SND_RELOAD_IN: return reloadIn(r);
        case SND_BOLT: return boltCycle(r);
        case SND_DEPLOY: return deploySound(r);
        case SND_ZOOM: return zoomSound(r);
        case SND_STEP0: case SND_STEP1: case SND_STEP2: case SND_STEP3: return footstep(r, id - SND_STEP0);
        case SND_JUMP: return jumpSound(r);
        case SND_LAND: return landSound(r);
        case SND_HIT_BODY: return hitBody(r);
        case SND_HIT_HEAD: return hitHead(r);
        case SND_HELMET: return helmetDing(r, v ? 1.04f : 1.0f);
        case SND_IMPACT_STONE: return impactStone(r, p);
        case SND_IMPACT_WOOD: return impactWood(r, p);
        case SND_IMPACT_METAL: return impactMetal(r, p, v == 2);
        case SND_IMPACT_SAND: return impactSand(r);
        case SND_EXPLOSION: return explosion(r);
        case SND_SMOKE: return smokeSound(r);
        case SND_FLASH: return flashbang(r);
        case SND_BOUNCE: return bounceSound(r, p);
        case SND_THROW: return throwSound(r);
        case SND_BEEP: return c4Beep(r);
        case SND_PLANTED: return radioAlert(r, {{880.0f, 1.0f}, {660.0f, 1.0f}, {880.0f, 1.0f}}, 0.15f);
        case SND_DEFUSED: return radioAlert(r, {{660.0f, 1.0f}, {990.0f, 1.0f}}, 0.16f);
        case SND_ROUND_START: return roundStart(r);
        case SND_ROUND_WIN: return roundWin(r);
        case SND_ROUND_LOSE: return roundLose(r);
        case SND_UI_HOVER: return uiHover(r);
        case SND_UI_CLICK: return uiClick(r);
        case SND_BUY: return buySound(r);
        case SND_PICKUP: return pickupSound(r);
        case SND_SHELL: return shellSound(r);
        case SND_KILL: return killConfirm(r);
        default: return Buf(4, 0.0f);
    }
}

// ------------------------------------------------------------------------------------------------ mixer

constexpr int kVoices = 96;
constexpr uint32_t kBlock = 256;
constexpr int kRing = 64;          // interaural delay line, power of two above the maximum ITD
constexpr uint32_t kQueue = 1024;  // command ring, power of two
constexpr float kHeadRadius = 0.0875f, kSpeedOfSound = 343.0f;
const float kShadowA = onePoleCoef(1500.0f);  // head-shadow shelf corner

// Reverb send per category: for 3D voices (scaled up with distance) and for 2D (first-person) voices.
const float kSend3D[SC_COUNT] = {0.32f, 0.22f, 0.0f, 0.0f};
const float kSend2D[SC_COUNT] = {0.12f, 0.05f, 0.0f, 0.0f};

inline float softClip(float x) {
    float a = std::fabs(x);
    if (a <= 0.9f) return x;
    float y = 0.9f + 0.1f * std::tanh((a - 0.9f) * 10.0f);
    return x < 0.0f ? -y : y;
}

// Game distance model (unchanged from the original mixer): ~1/r beyond `ref`, silent at 5000 units.
inline float distanceGain(float dist, float ref) {
    return std::pow(ref / std::max(ref, dist), 1.15f) * saturate(1.0f - dist / 5000.0f);
}

// Air absorption: progressive low-pass with distance (two cascaded one-poles at this cutoff).
inline float airCoef(float dist) {
    float fc = std::max(900.0f, 20000.0f * std::exp(-dist / 1600.0f));
    float a = onePoleCoef(fc);
    return a + (1.0f - a) * saturate((fc - 14000.0f) / 6000.0f);  // fades to bypass up close, no jump
}

// Outdoor ambience: stereo early reflections with slapback echoes feeding a Freeverb-style diffuse tail.
struct Reverb {
    struct Comb {
        Buf b;
        size_t i = 0;
        float store = 0.0f;
    };
    struct AllPass {
        Buf b;
        size_t i = 0;
    };
    struct Tap {
        size_t d;
        float g;
        bool cross;
    };
    Comb cl[8], cr[8];
    AllPass al[4], ar[4];
    Buf dl, dr;
    size_t w = 0, mask = 0;
    Tap tapL[7], tapR[7];
    float hpL = 0, hpR = 0, erL = 0, erR = 0;
    float hpA = onePoleCoef(110.0f), erA = onePoleCoef(3800.0f);
    float fb = 0.78f, damp = 0.4f, erGain = 0.55f, lateGain = 0.9f;

    void init() {
        static const int combT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int apT[4] = {556, 441, 341, 225};
        const float sc = (float)kRate / 44100.0f;
        for (int k = 0; k < 8; k++) {
            cl[k].b.assign((size_t)((float)combT[k] * sc), 0.0f);
            cr[k].b.assign((size_t)((float)(combT[k] + 23) * sc), 0.0f);
        }
        for (int k = 0; k < 4; k++) {
            al[k].b.assign((size_t)((float)apT[k] * sc), 0.0f);
            ar[k].b.assign((size_t)((float)(apT[k] + 23) * sc), 0.0f);
        }
        size_t len = 1;
        while (len < smp(0.45f)) len <<= 1;
        dl.assign(len, 0.0f);
        dr.assign(len, 0.0f);
        mask = len - 1;
        // {ms, gain, from the opposite channel}: close walls, then the big slapbacks at ~0.1 s and ~0.25 s.
        static const float L[7][3] = {{19, 0.3f, 0}, {43, 0.2f, 0}, {67, 0.18f, 1}, {101, 0.42f, 0}, {163, 0.2f, 1}, {241, 0.26f, 0}, {353, 0.12f, 1}};
        static const float R[7][3] = {{23, 0.28f, 0}, {47, 0.2f, 0}, {71, 0.17f, 1}, {113, 0.4f, 0}, {179, 0.19f, 1}, {263, 0.24f, 0}, {389, 0.11f, 1}};
        for (int k = 0; k < 7; k++) {
            tapL[k] = {smp(L[k][0] * 0.001f), L[k][1], L[k][2] > 0.5f};
            tapR[k] = {smp(R[k][0] * 0.001f), R[k][1], R[k][2] > 0.5f};
        }
    }

    void clear() {
        for (int k = 0; k < 8; k++) {
            std::fill(cl[k].b.begin(), cl[k].b.end(), 0.0f);
            std::fill(cr[k].b.begin(), cr[k].b.end(), 0.0f);
            cl[k].store = cr[k].store = 0.0f;
        }
        for (int k = 0; k < 4; k++) {
            std::fill(al[k].b.begin(), al[k].b.end(), 0.0f);
            std::fill(ar[k].b.begin(), ar[k].b.end(), 0.0f);
        }
        std::fill(dl.begin(), dl.end(), 0.0f);
        std::fill(dr.begin(), dr.end(), 0.0f);
        hpL = hpR = erL = erR = 0.0f;
    }

    static float comb(Comb& c, float x, float fb, float damp) {
        float y = c.b[c.i];
        c.store = y * (1.0f - damp) + c.store * damp;
        c.b[c.i] = x + c.store * fb;
        if (++c.i >= c.b.size()) c.i = 0;
        return y;
    }
    static float allpass(AllPass& a, float x) {
        float bo = a.b[a.i];
        a.b[a.i] = x + bo * 0.5f;
        if (++a.i >= a.b.size()) a.i = 0;
        return bo - x;
    }

    // Adds the wet signal for the stereo send into out.
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t n) {
        for (uint32_t i = 0; i < n; i++) {
            hpL += hpA * (inL[i] - hpL);
            hpR += hpA * (inR[i] - hpR);
            float xl = inL[i] - hpL, xr = inR[i] - hpR;
            dl[w] = xl;
            dr[w] = xr;
            float el = 0.0f, er = 0.0f;
            for (int k = 0; k < 7; k++) {
                el += tapL[k].g * (tapL[k].cross ? dr : dl)[(w - tapL[k].d) & mask];
                er += tapR[k].g * (tapR[k].cross ? dl : dr)[(w - tapR[k].d) & mask];
            }
            w = (w + 1) & mask;
            erL += erA * (el - erL);
            erR += erA * (er - erR);
            float in = ((xl + xr) * 0.3f + (erL + erR) * 0.5f) * 0.015f, ol = 0.0f, orr = 0.0f;
            for (int k = 0; k < 8; k++) {
                ol += comb(cl[k], in, fb, damp);
                orr += comb(cr[k], in, fb, damp);
            }
            for (int k = 0; k < 4; k++) {
                ol = allpass(al[k], ol);
                orr = allpass(ar[k], orr);
            }
            outL[i] += erL * erGain + ol * lateGain;
            outR[i] += erR * erGain + orr * lateGain;
        }
    }
};

// Stereo-linked look-ahead peak limiter followed by a soft clipper: the output never exceeds 1.0.
struct Limiter {
    static constexpr int kLook = 64;
    float bl[kLook] = {}, br[kLook] = {};
    int w = 0, holdCnt = 0;
    float gain = 1.0f, hold = 1.0f, att, rel, thr = 0.89f;
    Limiter() : att(1.0f - std::exp(-5.0f / kLook)), rel(1.0f - std::exp(-1.0f / (0.15f * kRate))) {}
    void run(float& l, float& r) {
        float pk = std::max(std::fabs(l), std::fabs(r));
        float need = pk > thr ? thr / pk : 1.0f;
        if (need <= hold) {
            hold = need;
            holdCnt = kLook;
        } else if (holdCnt > 0) {
            holdCnt--;
        } else {
            hold = need;
        }
        gain += (hold - gain) * (hold < gain ? att : rel);
        float dl = bl[w], dr = br[w];
        bl[w] = l;
        br[w] = r;
        w = (w + 1) % kLook;
        l = softClip(dl * gain);
        r = softClip(dr * gain);
    }
    void reset() { *this = Limiter(); }
};

enum CmdType { CMD_PLAY, CMD_STOP };

struct Cmd {
    int type = CMD_PLAY, id = 0;
    bool spatial = false;
    float vol = 1.0f, pitch = 1.0f, ref = 180.0f;
    vec3 pos;
};

struct Voice {
    const float* data = nullptr;
    uint32_t len = 0;
    double pos = 0.0;
    float pitch = 1.0f, vol = 1.0f, ref = 180.0f;
    int cat = 0;
    bool active = false, spatial = false, fresh = true, stopping = false;
    vec3 p;
    // Smoothed mix parameters: dry gains, reverb sends, interaural delays (samples), head-shadow HF gains, air filter.
    float gL = 0, gR = 0, sL = 0, sR = 0, dL = 0, dR = 0, hL = 1, hR = 1, air = 1;
    float a1 = 0, a2 = 0, lpL = 0, lpR = 0;
    float ring[kRing] = {};
    uint32_t w = 0;
};

inline float readRing(const Voice& v, float delay) {
    float p = (float)(v.w + kRing) - delay;
    int i0 = (int)p;
    float f = p - (float)i0, a = v.ring[i0 & (kRing - 1)], b = v.ring[(i0 + 1) & (kRing - 1)];
    return a + (b - a) * f;
}

struct Listener {
    vec3 pos, left, fwd;
};

bool writeWav(const std::string& path, const float* data, size_t frames, int channels) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t bytes = (uint32_t)(frames * (size_t)channels * 4);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(50 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(18);
    u16(3);  // IEEE float
    u16((uint16_t)channels);
    u32(kRate);
    u32((uint32_t)(kRate * channels * 4));
    u16((uint16_t)(channels * 4));
    u16(32);
    u16(0);
    std::fwrite("fact", 1, 4, f);
    u32(4);
    u32((uint32_t)frames);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    std::fwrite(data, 4, frames * (size_t)channels, f);
    return std::fclose(f) == 0;
}

}  // namespace

struct Audio::Impl {
    std::vector<Buf> bank[SND_COUNT];
    Voice voices[kVoices];
    Cmd queue[kQueue];
    std::atomic<uint32_t> qHead{0}, qTail{0};
    std::mutex mtx;               // serializes producers only; the audio callback never locks
    std::atomic<float> lis[6];    // listener position + left vector; a torn read only lasts one block
    std::atomic<float> master{0.8f};
    std::atomic<float> catVol[SC_COUNT];
    std::atomic<bool> hrtf{true};
    float masterCur = 0.8f, catCur[SC_COUNT] = {1, 1, 1, 1};
    int lastVariant[SND_COUNT] = {};
    Rng rng{0x5eed};
    Reverb reverb;
    Limiter limiter;
    float dryL[kBlock], dryR[kBlock], sendL[kBlock], sendR[kBlock];
    bool ok = false;
    double synthMs = 0.0;
#if CS2P_AUDIO
    ma_device device;
#endif

    Impl() {
        const float init[6] = {0, 0, 0, 0, 1, 0};
        for (int k = 0; k < 6; k++) lis[k].store(init[k]);
        for (auto& c : catVol) c.store(1.0f);
        reverb.init();
    }

    void push(const Cmd& c) {
        std::lock_guard<std::mutex> lock(mtx);
        uint32_t h = qHead.load(std::memory_order_relaxed);
        if (h - qTail.load(std::memory_order_acquire) >= kQueue) return;  // audio thread stalled: drop
        queue[h & (kQueue - 1)] = c;
        qHead.store(h + 1, std::memory_order_release);
    }

    Listener listener() const {
        Listener L;
        L.pos = {lis[0].load(std::memory_order_relaxed), lis[1].load(std::memory_order_relaxed), lis[2].load(std::memory_order_relaxed)};
        vec3 left(lis[3].load(std::memory_order_relaxed), lis[4].load(std::memory_order_relaxed), lis[5].load(std::memory_order_relaxed));
        left.z = 0.0f;
        float ll = length(left);
        L.left = ll > 1e-4f ? left / ll : vec3(0, 1, 0);
        L.fwd = cross(L.left, vec3(0, 0, 1));  // world is X forward, Y left, Z up
        return L;
    }

    // Audio thread: starts a voice, stealing the quietest one when all are busy.
    void start(const Cmd& c) {
        const std::vector<Buf>& b = bank[c.id];
        if (b.empty()) return;
        int nv = (int)b.size(), k = 0;
        if (nv > 1) {
            k = (int)(rng.next() % (uint32_t)nv);
            if (k == lastVariant[c.id]) k = (k + 1 + (int)(rng.next() % (uint32_t)(nv - 1))) % nv;
        }
        lastVariant[c.id] = k;
        Voice* slot = nullptr;
        float best = 1e30f;
        for (auto& v : voices) {
            if (!v.active) {
                slot = &v;
                break;
            }
            float score = std::max(std::max(v.gL, v.gR), std::max(v.sL, v.sR)) * (1.0f - (float)(v.pos / v.len));
            if (score < best) {
                best = score;
                slot = &v;
            }
        }
        Voice& v = *slot;
        v = Voice();
        v.data = b[k].data();
        v.len = (uint32_t)b[k].size();
        v.pitch = clampf(c.pitch, 0.25f, 4.0f);
        v.vol = std::max(0.0f, c.vol);
        v.ref = std::max(1.0f, c.ref);
        v.cat = soundCategory(c.id);
        v.spatial = c.spatial;
        v.p = c.pos;
        v.active = true;
    }

    void drain() {
        uint32_t t = qTail.load(std::memory_order_relaxed), h = qHead.load(std::memory_order_acquire);
        for (; t != h; t++) {
            const Cmd& c = queue[t & (kQueue - 1)];
            if (c.type == CMD_STOP) {
                for (auto& v : voices) v.stopping = v.stopping || v.active;
            } else {
                start(c);
            }
        }
        qTail.store(t, std::memory_order_release);
    }

    void renderVoice(Voice& v, uint32_t n, const Listener& L, bool headphones) {
        float vol = v.vol * catCur[v.cat];
        float tgL, tgR, tsL, tsR, tdL = 0, tdR = 0, thL = 1, thR = 1, tAir = 1;
        if (!v.spatial) {
            tgL = tgR = vol;
            tsL = tsR = vol * kSend2D[v.cat];
        } else {
            vec3 d = v.p - L.pos;
            float dist = length(d);
            vec3 dir = dist > 1e-3f ? d / dist : L.fwd;
            float lat = clampf(dot(dir, L.left), -1.0f, 1.0f), front = dot(dir, L.fwd);
            float g = distanceGain(dist, v.ref);
            tAir = airCoef(dist);
            float pa = (lat * 0.85f + 1.0f) * (kPi * 0.25f), pl = std::sin(pa), pr = std::cos(pa);
            float wet = vol * kSend3D[v.cat] * std::sqrt(g) * (0.4f + 0.6f * saturate(dist / 2000.0f));
            tsL = wet * pl;
            tsR = wet * pr;
            float rear = 1.0f - 0.3f * saturate(-front);  // sources behind sound slightly duller
            if (headphones) {
                float al = std::fabs(lat), az = std::asin(al);
                float itd = kHeadRadius / kSpeedOfSound * (az + al) * kRate;  // Woodworth: (r/c)(theta + sin theta), max ~0.66 ms
                float nearG = 0.7071f * (1.0f + 0.25f * al), farG = 0.7071f * (1.0f - 0.35f * al);
                float nearH = (1.0f + 0.15f * al) * rear, farH = (1.0f - 0.75f * al) * rear;
                if (lat >= 0.0f) {
                    tgL = nearG; tgR = farG; thL = nearH; thR = farH; tdR = itd;
                } else {
                    tgL = farG; tgR = nearG; thL = farH; thR = nearH; tdL = itd;
                }
                tgL *= vol * g;
                tgR *= vol * g;
            } else {
                tgL = vol * g * pl;
                tgR = vol * g * pr;
                thL = thR = rear;
            }
        }
        if (v.stopping) tgL = tgR = tsL = tsR = 0.0f;
        if (v.fresh) {  // no ramp on the first block: transients must start at full level
            v.gL = tgL; v.gR = tgR; v.sL = tsL; v.sR = tsR; v.dL = tdL; v.dR = tdR; v.hL = thL; v.hR = thR; v.air = tAir;
            v.fresh = false;
        }
        float loud = std::max(std::max(std::max(tgL, tgR), std::max(tsL, tsR)), std::max(std::max(v.gL, v.gR), std::max(v.sL, v.sR)));
        if (loud < 1e-5f) {  // inaudible: just advance
            v.pos += (double)v.pitch * n;
            v.gL = tgL; v.gR = tgR; v.sL = tsL; v.sR = tsR; v.dL = tdL; v.dR = tdR; v.hL = thL; v.hR = thR; v.air = tAir;
            if (v.pos + 1.0 >= (double)v.len || v.stopping) v.active = false;
            return;
        }
        float inv = 1.0f / (float)n;
        float dgL = (tgL - v.gL) * inv, dgR = (tgR - v.gR) * inv, dsL = (tsL - v.sL) * inv, dsR = (tsR - v.sR) * inv;
        const float* d = v.data;
        uint32_t last = v.len - 1;
        if (!v.spatial) {
            for (uint32_t i = 0; i < n; i++) {
                uint32_t idx = (uint32_t)v.pos;
                if (idx >= last) { v.active = false; break; }
                float s = d[idx] + (d[idx + 1] - d[idx]) * (float)(v.pos - (double)idx);
                v.pos += v.pitch;
                dryL[i] += s * v.gL;
                dryR[i] += s * v.gR;
                sendL[i] += s * v.sL;
                sendR[i] += s * v.sR;
                v.gL += dgL; v.gR += dgR; v.sL += dsL; v.sR += dsR;
            }
        } else {
            float ddL = (tdL - v.dL) * inv, ddR = (tdR - v.dR) * inv, dhL = (thL - v.hL) * inv, dhR = (thR - v.hR) * inv;
            float dAir = (tAir - v.air) * inv;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t idx = (uint32_t)v.pos;
                if (idx >= last) { v.active = false; break; }
                float s = d[idx] + (d[idx + 1] - d[idx]) * (float)(v.pos - (double)idx);
                v.pos += v.pitch;
                v.a1 += v.air * (s - v.a1);
                v.a2 += v.air * (v.a1 - v.a2);
                s = v.a2;
                v.ring[v.w] = s;
                float xl = readRing(v, v.dL), xr = readRing(v, v.dR);
                v.w = (v.w + 1) & (kRing - 1);
                v.lpL += kShadowA * (xl - v.lpL);
                v.lpR += kShadowA * (xr - v.lpR);
                dryL[i] += (v.lpL + v.hL * (xl - v.lpL)) * v.gL;
                dryR[i] += (v.lpR + v.hR * (xr - v.lpR)) * v.gR;
                sendL[i] += s * v.sL;
                sendR[i] += s * v.sR;
                v.gL += dgL; v.gR += dgR; v.sL += dsL; v.sR += dsR;
                v.dL += ddL; v.dR += ddR; v.hL += dhL; v.hR += dhR; v.air += dAir;
            }
        }
        v.gL = tgL; v.gR = tgR; v.sL = tsL; v.sR = tsR; v.dL = tdL; v.dR = tdR; v.hL = thL; v.hR = thR; v.air = tAir;
        if (v.stopping) v.active = false;
    }

    void render(float* out, uint32_t n) {
        std::memset(dryL, 0, sizeof(float) * n);
        std::memset(dryR, 0, sizeof(float) * n);
        std::memset(sendL, 0, sizeof(float) * n);
        std::memset(sendR, 0, sizeof(float) * n);
        Listener L = listener();
        bool headphones = hrtf.load(std::memory_order_relaxed);
        for (int c = 0; c < SC_COUNT; c++) catCur[c] = std::max(0.0f, catVol[c].load(std::memory_order_relaxed));
        for (auto& v : voices)
            if (v.active) renderVoice(v, n, L, headphones);
        reverb.process(sendL, sendR, dryL, dryR, n);
        float mt = std::max(0.0f, master.load(std::memory_order_relaxed)), dm = (mt - masterCur) / (float)n;
        for (uint32_t i = 0; i < n; i++) {
            float g = masterCur + dm * (float)(i + 1), l = dryL[i] * g, r = dryR[i] * g;
            limiter.run(l, r);
            out[i * 2] = l;
            out[i * 2 + 1] = r;
        }
        masterCur = mt;
    }

    // Real-time safe: no locks, no allocation.
    void mix(float* out, uint32_t frames) {
#if CS2P_FLUSH_DENORMALS
        _mm_setcsr(_mm_getcsr() | 0x8040);  // flush-to-zero + denormals-are-zero for decaying filter states
#endif
        drain();
        while (frames > 0) {
            uint32_t n = std::min(frames, kBlock);
            render(out, n);
            out += n * 2;
            frames -= n;
        }
    }

    void synthesize(unsigned threads = 0) {
        auto t0 = std::chrono::steady_clock::now();
        struct Job {
            int id, v;
        };
        std::vector<Job> jobs;
        for (int id = 0; id < SND_COUNT; id++) {
            bank[id].assign((size_t)variantCount(id), Buf());
            for (int v = 0; v < variantCount(id); v++) jobs.push_back({id, v});
        }
        std::atomic<size_t> next{0};
        auto worker = [&]() {
            for (size_t j; (j = next.fetch_add(1)) < jobs.size();) bank[jobs[j].id][(size_t)jobs[j].v] = renderSound(jobs[j].id, jobs[j].v);
        };
        if (threads == 0) threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
        std::vector<std::thread> pool;
        for (unsigned k = 1; k < threads; k++) pool.emplace_back(worker);
        worker();
        for (auto& t : pool) t.join();
        synthMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    void resetMixer() {
        for (auto& v : voices) v = Voice();
        reverb.clear();
        limiter.reset();
        masterCur = master.load();
        qTail.store(qHead.load());
        rng = Rng(0x5eed);
        std::fill(lastVariant, lastVariant + SND_COUNT, 0);
    }
};

#if CS2P_AUDIO
static void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
    static_cast<Audio::Impl*>(dev->pUserData)->mix((float*)out, frames);
}
#endif

Audio::Audio() : m(new Impl) {}
Audio::~Audio() { shutdown(); }

bool Audio::init() {
    m->synthesize();
    size_t bufs = 0;
    for (auto& b : m->bank) bufs += b.size();
    logInfo("Audio: synthesized %d sounds (%d buffers) in %.0f ms", (int)SND_COUNT, (int)bufs, m->synthMs);
#if CS2P_AUDIO
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = kRate;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = m.get();
    if (ma_device_init(nullptr, &cfg, &m->device) != MA_SUCCESS) {
        logInfo("Audio: no output device, running silent");
        return false;
    }
    if (ma_device_start(&m->device) != MA_SUCCESS) {
        ma_device_uninit(&m->device);
        return false;
    }
    m->ok = true;
    logInfo("Audio: %s", m->device.playback.name);
    return true;
#else
    return false;
#endif
}

void Audio::shutdown() {
#if CS2P_AUDIO
    if (m && m->ok) {
        ma_device_uninit(&m->device);
        m->ok = false;
    }
#endif
}

bool Audio::available() const { return m->ok; }

void Audio::play(int id, float volume, float pitch) {
    if (!m->ok || id < 0 || id >= SND_COUNT) return;
    Cmd c;
    c.id = id;
    c.vol = volume;
    c.pitch = pitch;
    m->push(c);
}

void Audio::play3D(int id, vec3 pos, float volume, float pitch, float ref) {
    if (!m->ok || id < 0 || id >= SND_COUNT) return;
    Cmd c;
    c.id = id;
    c.spatial = true;
    c.pos = pos;
    c.vol = volume;
    c.pitch = pitch;
    c.ref = ref;
    m->push(c);
}

void Audio::setListener(vec3 pos, vec3 left) {
    const float v[6] = {pos.x, pos.y, pos.z, left.x, left.y, left.z};
    for (int k = 0; k < 6; k++) m->lis[k].store(v[k], std::memory_order_relaxed);
}

void Audio::setVolume(float master) { m->master.store(clampf(master, 0.0f, 4.0f)); }

void Audio::setCategoryVolume(int category, float volume) {
    if (category >= 0 && category < SC_COUNT) m->catVol[category].store(clampf(volume, 0.0f, 4.0f));
}

void Audio::setSpatial(bool headphones3D) { m->hrtf.store(headphones3D); }

int soundCategory(int id) {
    switch (id) {
        case SND_AK: case SND_M4: case SND_AWP: case SND_DEAGLE: case SND_GLOCK: case SND_USP:
        case SND_KNIFE_SWING: case SND_KNIFE_HIT: case SND_KNIFE_STAB: case SND_EMPTY: case SND_RELOAD_OUT:
        case SND_RELOAD_IN: case SND_BOLT: case SND_DEPLOY: case SND_ZOOM: case SND_EXPLOSION: case SND_SMOKE:
        case SND_FLASH: case SND_BOUNCE: case SND_THROW: case SND_SHELL:
            return SC_WEAPONS;
        case SND_ROUND_START: case SND_ROUND_WIN: case SND_ROUND_LOSE:
            return SC_MUSIC;
        case SND_UI_HOVER: case SND_UI_CLICK: case SND_BUY: case SND_KILL:
            return SC_UI;
        default:
            return SC_WORLD;
    }
}

void Audio::stopAll() {
    if (!m->ok) return;
    Cmd c;
    c.type = CMD_STOP;
    m->push(c);
}

// ------------------------------------------------------------------------------------------------ offline dump

namespace {

struct SceneEvent {
    float t;
    int id;
    bool spatial;
    vec3 pos;
    float vol, ref;
};

// Renders events through the real mixer in 10 ms device-sized chunks; `step(time)` can move the listener.
template <typename Step>
std::vector<float> renderScene(Audio::Impl& im, float seconds, std::vector<SceneEvent> ev, Step step) {
    im.resetMixer();
    std::vector<float> out(smp(seconds) * 2, 0.0f);
    const uint32_t chunk = kRate / 100;
    size_t frames = out.size() / 2, next = 0;
    std::sort(ev.begin(), ev.end(), [](const SceneEvent& a, const SceneEvent& b) { return a.t < b.t; });
    for (size_t f = 0; f < frames; f += chunk) {
        float t = (float)f / kRate;
        step(t);
        for (; next < ev.size() && ev[next].t <= t; next++) {
            Cmd c;
            c.id = ev[next].id;
            c.spatial = ev[next].spatial;
            c.pos = ev[next].pos;
            c.vol = ev[next].vol;
            c.ref = ev[next].ref;
            im.push(c);
        }
        im.mix(out.data() + f * 2, (uint32_t)std::min<size_t>(chunk, frames - f));
    }
    return out;
}

void setListenerYaw(Audio::Impl& im, vec3 pos, float yawDeg) {
    float a = yawDeg * kDeg;
    const float v[6] = {pos.x, pos.y, pos.z, -std::sin(a), std::cos(a), 0.0f};
    for (int k = 0; k < 6; k++) im.lis[k].store(v[k]);
}

double rms(const std::vector<float>& s) {
    double e = 0.0;
    for (float v : s) e += (double)v * v;
    return std::sqrt(e / (double)std::max<size_t>(1, s.size()));
}

}  // namespace

bool Audio::dumpSounds(const std::string& dir) {
#if defined(_WIN32)
    _mkdir(dir.c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
    std::unique_ptr<Impl> im(new Impl);
    // Best of three: the wall clock is noisy on shared machines.
    double single = 1e9, multi = 1e9;
    for (int k = 0; k < 3; k++) {
        im->synthesize(1);
        single = std::min(single, im->synthMs);
        im->synthesize();
        multi = std::min(multi, im->synthMs);
    }
    size_t bufs = 0, samples = 0;
    for (auto& b : im->bank)
        for (auto& v : b) bufs++, samples += v.size();
    std::printf("synthesis: %d sounds, %d buffers, %.1f s of audio: %.1f ms on 1 thread, %.1f ms on %u threads\n", (int)SND_COUNT,
                (int)bufs, (double)samples / kRate, single, multi, std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    bool ok = true;
    for (int id = 0; id < SND_COUNT; id++)
        for (size_t v = 0; v < im->bank[id].size(); v++) {
            char name[256];
            std::snprintf(name, sizeof(name), "%s/%02d_%s_v%d.wav", dir.c_str(), id, kSoundNames[id], (int)v);
            ok &= writeWav(name, im->bank[id][v].data(), im->bank[id][v].size(), 1);
        }

    auto save = [&](const char* file, const std::vector<float>& s) {
        ok &= writeWav(dir + "/" + file, s.data(), s.size() / 2, 2);
        float pk = 0.0f;
        for (float v : s) pk = std::max(pk, std::fabs(v));
        std::printf("%-22s peak %.3f  rms %.4f\n", file, pk, rms(s));
    };
    auto still = [&](float) {};
    vec3 origin(0, 0, 0);
    setListenerYaw(*im, origin, 0.0f);

    // Same AK shot at growing distances straight ahead.
    im->hrtf = true;
    std::vector<SceneEvent> ev;
    const float dists[5] = {200, 600, 1200, 2400, 4000};
    for (int k = 0; k < 5; k++) ev.push_back({0.05f + 1.6f * (float)k, SND_AK, true, vec3(dists[k], 0, 0), 1.0f, 380.0f});
    save("mix_distance.wav", renderScene(*im, 8.5f, ev, still));

    // Click from +90 (left), +45, 0, -45, -90 (right) and 180 degrees, in both panning modes.
    ev.clear();
    const float az[6] = {90, 45, 0, -45, -90, 180};
    for (int k = 0; k < 6; k++)
        ev.push_back({0.1f + 0.5f * (float)k, SND_UI_CLICK, true, vec3(150 * std::cos(az[k] * kDeg), 150 * std::sin(az[k] * kDeg), 0), 1.0f, 180.0f});
    save("mix_itd_hrtf.wav", renderScene(*im, 3.2f, ev, still));
    im->hrtf = false;
    save("mix_itd_pan.wav", renderScene(*im, 3.2f, ev, still));
    im->hrtf = true;

    // Listener turns a full circle in 3 s while a smoke grenade hisses in front: must stay click-free.
    ev = {{0.0f, SND_SMOKE, true, vec3(300, 0, 0), 1.0f, 180.0f}};
    save("mix_turn.wav", renderScene(*im, 3.2f, ev, [&](float t) { setListenerYaw(*im, origin, 360.0f * saturate(t / 3.0f)); }));
    setListenerYaw(*im, origin, 0.0f);

    // Worst case: dozens of simultaneous close shots plus an explosion must not clip.
    ev.clear();
    Rng r(99);
    for (int k = 0; k < 30; k++) ev.push_back({0.1f + 0.1f * (float)k, SND_AK, false, origin, 1.0f, 180.0f});
    for (int k = 0; k < 24; k++) {
        int ids[3] = {SND_AWP, SND_DEAGLE, SND_M4};
        ev.push_back({r.range(0.1f, 0.4f), ids[k % 3], true, vec3(r.range(-150, 150), r.range(-150, 150), 0), 1.0f, 380.0f});
    }
    ev.push_back({0.2f, SND_EXPLOSION, true, vec3(150, 50, 0), 1.0f, 700.0f});
    ev.push_back({0.25f, SND_FLASH, true, vec3(100, -80, 0), 1.0f, 600.0f});
    im->master = 1.0f;
    save("mix_stress.wav", renderScene(*im, 4.0f, ev, still));
    im->master = 0.8f;

    // Category sliders: weapons at 25% must drop the AK by 12 dB and leave the UI click alone.
    ev = {{0.0f, SND_AK, false, origin, 0.75f, 180.0f}};
    std::vector<SceneEvent> ui = {{0.0f, SND_UI_CLICK, false, origin, 0.5f, 180.0f}};
    double ak1 = rms(renderScene(*im, 1.2f, ev, still)), ui1 = rms(renderScene(*im, 0.2f, ui, still));
    im->catVol[SC_WEAPONS] = 0.25f;
    double ak2 = rms(renderScene(*im, 1.2f, ev, still)), ui2 = rms(renderScene(*im, 0.2f, ui, still));
    im->catVol[SC_WEAPONS] = 1.0f;
    std::printf("category test: weapons 100%%->25%%: AK %.2f dB, UI click %.2f dB\n", 20.0 * std::log10(ak2 / ak1), 20.0 * std::log10(ui2 / ui1));

    // Reverb impulse response (send bus only).
    {
        im->resetMixer();
        size_t n = smp(2.0f);
        std::vector<float> inL(n, 0.0f), inR(n, 0.0f), oL(n, 0.0f), oR(n, 0.0f), st(n * 2);
        inL[0] = inR[0] = 1.0f;
        for (size_t o = 0; o < n; o += kBlock) {
            uint32_t c = (uint32_t)std::min<size_t>(kBlock, n - o);
            im->reverb.process(&inL[o], &inR[o], &oL[o], &oR[o], c);
        }
        for (size_t i = 0; i < n; i++) st[i * 2] = oL[i], st[i * 2 + 1] = oR[i];
        save("reverb_ir.wav", st);
    }

    // Mixer cost with every voice busy on spatial gunshots.
    {
        ev.clear();
        for (int k = 0; k < 400; k++)
            ev.push_back({0.05f * (float)k, SND_AWP, true, vec3(r.range(-2000, 2000), r.range(-2000, 2000), 0), 1.0f, 380.0f});
        double ms = 1e9;
        for (int k = 0; k < 3; k++) {
            auto t0 = std::chrono::steady_clock::now();
            renderScene(*im, 20.0f, ev, [&](float t) { setListenerYaw(*im, origin, 40.0f * t); });
            ms = std::min(ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        int active = 0;
        for (auto& v : im->voices) active += v.active;
        std::printf("mixer: %d voices busy, %.2f ms per second of audio (%.2f%% of one core, best of 3)\n", active, ms / 20.0, ms / 200.0);
    }
    return ok;
}
