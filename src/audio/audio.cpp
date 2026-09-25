#include "audio/audio.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "core/common.h"

#if CS2P_AUDIO
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

struct Synth {
    std::vector<float> s;
    Rng rng;
    explicit Synth(float seconds, uint64_t seed) : s((size_t)(seconds * kRate), 0.0f), rng(seed) {}
    float t(size_t i) const { return (float)i / kRate; }
    float noise() { return rng.range(-1.0f, 1.0f); }
};

float env(float t, float attack, float decay) { return t < attack ? t / attack : std::exp(-(t - attack) / decay); }

// One-pole filters applied in place.
void lowpass(std::vector<float>& s, float cutoff) {
    float a = 1.0f - std::exp(-kTwoPi * cutoff / kRate), y = 0;
    for (auto& v : s) { y += a * (v - y); v = y; }
}
void highpass(std::vector<float>& s, float cutoff) {
    float a = 1.0f - std::exp(-kTwoPi * cutoff / kRate), y = 0;
    for (auto& v : s) { y += a * (v - y); v = v - y; }
}
void normalize(std::vector<float>& s, float peak) {
    float m = 1e-6f;
    for (float v : s) m = std::max(m, std::fabs(v));
    for (auto& v : s) v *= peak / m;
}

struct GunSpec {
    float len, crack, crackDecay, thumpHz, thumpDecay, body, bodyDecay, bodyCut, tail, tailDecay, tailCut;
};

std::vector<float> gunshot(const GunSpec& g, uint64_t seed) {
    Synth sy(g.len, seed);
    std::vector<float> crack(sy.s.size()), body(sy.s.size()), tail(sy.s.size());
    for (size_t i = 0; i < sy.s.size(); i++) {
        float t = sy.t(i);
        crack[i] = sy.noise() * env(t, 0.0005f, g.crackDecay);
        body[i] = sy.noise() * env(t, 0.001f, g.bodyDecay);
        tail[i] = sy.noise() * env(t, 0.02f, g.tailDecay);
    }
    highpass(crack, 1800.0f);
    lowpass(body, g.bodyCut);
    lowpass(body, g.bodyCut);
    lowpass(tail, g.tailCut);
    lowpass(tail, g.tailCut);
    float phase = 0;
    for (size_t i = 0; i < sy.s.size(); i++) {
        float t = sy.t(i);
        float f = g.thumpHz * (1.0f + 1.5f * std::exp(-t * 40.0f));
        phase += kTwoPi * f / kRate;
        float thump = std::sin(phase) * env(t, 0.001f, g.thumpDecay);
        sy.s[i] = crack[i] * g.crack + body[i] * g.body + thump * 0.9f + tail[i] * g.tail;
    }
    for (auto& v : sy.s) v = std::tanh(v * 1.6f);
    normalize(sy.s, 0.95f);
    return sy.s;
}

std::vector<float> clickSound(float freq, float len, float decay, float noiseAmt, uint64_t seed) {
    Synth sy(len, seed);
    for (size_t i = 0; i < sy.s.size(); i++) {
        float t = sy.t(i);
        sy.s[i] = (std::sin(kTwoPi * freq * t) * 0.7f + sy.noise() * noiseAmt) * env(t, 0.0003f, decay);
    }
    highpass(sy.s, 300.0f);
    normalize(sy.s, 0.7f);
    return sy.s;
}

std::vector<float> ringSound(std::initializer_list<float> freqs, float len, float decay, float thud, uint64_t seed) {
    Synth sy(len, seed);
    std::vector<float> th(sy.s.size());
    for (size_t i = 0; i < sy.s.size(); i++) th[i] = sy.noise() * env(sy.t(i), 0.001f, 0.03f);
    lowpass(th, 600.0f);
    for (size_t i = 0; i < sy.s.size(); i++) {
        float t = sy.t(i), v = 0;
        int k = 0;
        for (float f : freqs) {
            v += std::sin(kTwoPi * f * t + k) * env(t, 0.0005f, decay / (1.0f + (k + 1) * 0.3f));
            k++;
        }
        sy.s[i] = v * 0.4f + th[i] * thud;
    }
    normalize(sy.s, 0.8f);
    return sy.s;
}

std::vector<float> thud(float len, float cut, float sineHz, float decay, uint64_t seed, float noiseAmt = 1.0f) {
    Synth sy(len, seed);
    for (size_t i = 0; i < sy.s.size(); i++) sy.s[i] = sy.noise() * noiseAmt * env(sy.t(i), 0.001f, decay);
    lowpass(sy.s, cut);
    lowpass(sy.s, cut);
    for (size_t i = 0; i < sy.s.size(); i++) {
        float t = sy.t(i);
        sy.s[i] = sy.s[i] * 3.0f + std::sin(kTwoPi * sineHz * t) * env(t, 0.002f, decay) * 0.8f;
    }
    normalize(sy.s, 0.8f);
    return sy.s;
}

std::vector<float> whoosh(float len, float f0, float f1, uint64_t seed) {
    Synth sy(len, seed);
    float y1 = 0, y2 = 0;
    for (size_t i = 0; i < sy.s.size(); i++) {
        float t = sy.t(i), u = t / len;
        float f = lerpf(f0, f1, u);
        float a = 1.0f - std::exp(-kTwoPi * f / kRate);
        float x = sy.noise();
        y1 += a * (x - y1);
        y2 += a * (y1 - y2);
        sy.s[i] = (y1 - y2) * std::sin(kPi * u) * 4.0f;
    }
    normalize(sy.s, 0.6f);
    return sy.s;
}

std::vector<float> tones(std::initializer_list<std::pair<float, float>> notes, float noteLen, float decay) {
    size_t n = notes.size();
    Synth sy(noteLen * n + decay * 3, 1);
    size_t k = 0;
    for (auto& nt : notes) {
        float start = noteLen * k++;
        for (size_t i = (size_t)(start * kRate); i < sy.s.size(); i++) {
            float t = sy.t(i) - start;
            float v = std::sin(kTwoPi * nt.first * t) + 0.3f * std::sin(kTwoPi * nt.first * 2.0f * t);
            sy.s[i] += v * env(t, 0.005f, decay) * nt.second;
        }
    }
    normalize(sy.s, 0.5f);
    return sy.s;
}

std::vector<float> concat(std::vector<float> a, const std::vector<float>& b, float gap) {
    size_t off = a.size() + (size_t)(gap * kRate);
    a.resize(off + b.size(), 0.0f);
    for (size_t i = 0; i < b.size(); i++) a[off + i] += b[i];
    return a;
}

}  // namespace

struct Audio::Impl {
    std::vector<float> buf[SND_COUNT];
    struct Voice {
        const std::vector<float>* data = nullptr;
        double pos = 0;
        float pitch = 1, vol = 1;
        bool spatial = false;
        vec3 p;
        float ref = 180;
        float gl = 1, gr = 1;
        bool active = false;
    };
    Voice voices[64];
    std::mutex mtx;
    vec3 lpos, lleft{0, 1, 0};
    float master = 0.8f;
    float catVol[SC_COUNT] = {1, 1, 1, 1};
    bool hrtf = true;
    bool ok = false;
#if CS2P_AUDIO
    ma_device device;
#endif

    void computeGains(Voice& v) {
        if (!v.spatial) {
            v.gl = v.gr = v.vol;
            return;
        }
        vec3 d = v.p - lpos;
        float dist = length(d);
        float g = v.ref / std::max(v.ref, dist);
        g = std::pow(g, 1.15f) * saturate(1.0f - dist / 5000.0f);
        float pan = dist > 1.0f ? dot(d / dist, lleft) : 0.0f;
        v.gl = v.vol * g * std::sqrt(0.5f * (1.0f + pan * 0.8f));
        v.gr = v.vol * g * std::sqrt(0.5f * (1.0f - pan * 0.8f));
    }

    void mix(float* out, uint32_t frames) {
        std::memset(out, 0, sizeof(float) * frames * 2);
        std::unique_lock<std::mutex> lock(mtx, std::try_to_lock);
        if (!lock.owns_lock()) return;
        for (auto& v : voices) {
            if (!v.active || !v.data) continue;
            const std::vector<float>& d = *v.data;
            for (uint32_t i = 0; i < frames; i++) {
                size_t idx = (size_t)v.pos;
                if (idx + 1 >= d.size()) { v.active = false; break; }
                float f = (float)(v.pos - idx);
                float s = d[idx] + (d[idx + 1] - d[idx]) * f;
                out[i * 2] += s * v.gl * master;
                out[i * 2 + 1] += s * v.gr * master;
                v.pos += v.pitch;
            }
        }
        for (uint32_t i = 0; i < frames * 2; i++) out[i] = std::tanh(out[i]);
    }

    void synthesize() {
        buf[SND_AK] = gunshot({0.9f, 0.9f, 0.012f, 70, 0.07f, 1.1f, 0.05f, 2200, 0.55f, 0.28f, 900}, 11);
        buf[SND_M4] = gunshot({0.8f, 1.1f, 0.009f, 85, 0.05f, 0.9f, 0.04f, 3200, 0.45f, 0.24f, 1100}, 12);
        buf[SND_AWP] = gunshot({1.6f, 1.2f, 0.015f, 55, 0.12f, 1.3f, 0.08f, 1800, 0.7f, 0.55f, 700}, 13);
        buf[SND_DEAGLE] = gunshot({1.0f, 1.2f, 0.012f, 75, 0.08f, 1.1f, 0.06f, 2600, 0.55f, 0.35f, 900}, 14);
        buf[SND_GLOCK] = gunshot({0.45f, 0.9f, 0.007f, 110, 0.035f, 0.7f, 0.03f, 3500, 0.3f, 0.12f, 1400}, 15);
        buf[SND_USP] = gunshot({0.3f, 0.12f, 0.004f, 140, 0.03f, 0.9f, 0.025f, 900, 0.12f, 0.06f, 700}, 16);
        buf[SND_KNIFE_SWING] = whoosh(0.28f, 600, 2400, 21);
        buf[SND_KNIFE_HIT] = thud(0.2f, 900, 140, 0.04f, 22);
        buf[SND_KNIFE_STAB] = thud(0.3f, 700, 90, 0.06f, 23);
        buf[SND_EMPTY] = clickSound(2600, 0.08f, 0.012f, 0.4f, 24);
        buf[SND_RELOAD_OUT] = concat(clickSound(1400, 0.1f, 0.02f, 0.6f, 25), clickSound(900, 0.12f, 0.03f, 0.5f, 26), 0.08f);
        buf[SND_RELOAD_IN] = concat(clickSound(1100, 0.1f, 0.02f, 0.6f, 27), clickSound(1800, 0.1f, 0.015f, 0.5f, 28), 0.05f);
        buf[SND_BOLT] = concat(clickSound(700, 0.12f, 0.03f, 0.7f, 29), clickSound(1200, 0.12f, 0.02f, 0.7f, 30), 0.25f);
        buf[SND_DEPLOY] = concat(whoosh(0.18f, 400, 1200, 31), clickSound(1500, 0.08f, 0.015f, 0.5f, 32), 0.05f);
        buf[SND_ZOOM] = clickSound(3200, 0.05f, 0.008f, 0.3f, 33);
        for (int i = 0; i < 4; i++) buf[SND_STEP0 + i] = thud(0.14f, 500.0f + i * 90.0f, 70.0f + i * 12.0f, 0.03f, 40 + i, 1.4f);
        buf[SND_JUMP] = thud(0.12f, 700, 90, 0.025f, 45);
        buf[SND_LAND] = thud(0.22f, 400, 60, 0.05f, 46, 1.5f);
        buf[SND_HIT_BODY] = thud(0.16f, 800, 110, 0.035f, 50);
        buf[SND_HIT_HEAD] = concat(thud(0.15f, 1200, 160, 0.03f, 51), ringSound({2900, 4100}, 0.2f, 0.05f, 0.2f, 52), 0.0f);
        buf[SND_HELMET] = ringSound({2400, 3150, 4820, 6100}, 0.45f, 0.12f, 0.5f, 53);
        buf[SND_IMPACT_STONE] = concat(clickSound(3800, 0.05f, 0.006f, 1.0f, 60), thud(0.12f, 2500, 200, 0.02f, 61), 0.0f);
        buf[SND_IMPACT_WOOD] = thud(0.14f, 1400, 320, 0.03f, 62);
        buf[SND_IMPACT_METAL] = ringSound({1800, 2710, 4020}, 0.3f, 0.07f, 0.6f, 63);
        buf[SND_IMPACT_SAND] = thud(0.12f, 1800, 120, 0.02f, 64);
        {
            Synth sy(2.4f, 70);
            std::vector<float> n(sy.s.size());
            for (size_t i = 0; i < n.size(); i++) n[i] = sy.noise() * env(sy.t(i), 0.004f, 0.45f);
            lowpass(n, 500);
            lowpass(n, 500);
            for (size_t i = 0; i < n.size(); i++) {
                float t = sy.t(i);
                sy.s[i] = n[i] * 4.0f + std::sin(kTwoPi * 42.0f * t) * env(t, 0.005f, 0.35f) + sy.noise() * env(t, 0.0f, 0.02f) * 0.8f;
            }
            for (auto& v : sy.s) v = std::tanh(v * 1.5f);
            normalize(sy.s, 0.98f);
            buf[SND_EXPLOSION] = sy.s;
        }
        {
            Synth sy(2.0f, 71);
            for (size_t i = 0; i < sy.s.size(); i++) sy.s[i] = sy.noise() * env(sy.t(i), 0.05f, 0.8f);
            highpass(sy.s, 2500);
            normalize(sy.s, 0.5f);
            buf[SND_SMOKE] = sy.s;
        }
        buf[SND_FLASH] = concat(gunshot({0.5f, 1.2f, 0.01f, 90, 0.05f, 1.0f, 0.04f, 3000, 0.4f, 0.2f, 1500}, 72), ringSound({3500}, 0.6f, 0.3f, 0.0f, 73), 0.0f);
        buf[SND_BOUNCE] = thud(0.1f, 1600, 260, 0.02f, 74);
        buf[SND_THROW] = whoosh(0.3f, 300, 1400, 75);
        buf[SND_BEEP] = ringSound({2050}, 0.12f, 0.05f, 0.0f, 80);
        buf[SND_PLANTED] = tones({{880, 1}, {660, 1}, {880, 1}}, 0.12f, 0.1f);
        buf[SND_DEFUSED] = tones({{660, 1}, {990, 1}}, 0.12f, 0.15f);
        buf[SND_ROUND_START] = tones({{523, 0.8f}, {784, 1}}, 0.14f, 0.25f);
        buf[SND_ROUND_WIN] = tones({{523, 1}, {659, 1}, {784, 1}, {1046, 1}}, 0.11f, 0.35f);
        buf[SND_ROUND_LOSE] = tones({{440, 1}, {349, 1}, {294, 1}}, 0.16f, 0.35f);
        buf[SND_UI_HOVER] = clickSound(4200, 0.03f, 0.004f, 0.1f, 90);
        buf[SND_UI_CLICK] = clickSound(1900, 0.06f, 0.01f, 0.2f, 91);
        buf[SND_BUY] = tones({{1320, 1}, {1760, 1}}, 0.06f, 0.08f);
        buf[SND_PICKUP] = concat(clickSound(900, 0.08f, 0.02f, 0.5f, 92), clickSound(1400, 0.08f, 0.02f, 0.4f, 93), 0.06f);
        buf[SND_SHELL] = ringSound({5200, 7400}, 0.12f, 0.03f, 0.1f, 94);
        buf[SND_KILL] = ringSound({1600}, 0.15f, 0.04f, 0.0f, 95);
        for (auto& b : buf)
            if (b.size() < 4) b.assign(4, 0.0f);
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
    std::lock_guard<std::mutex> lock(m->mtx);
    Impl::Voice* slot = nullptr;
    for (auto& v : m->voices)
        if (!v.active) { slot = &v; break; }
    if (!slot) slot = &m->voices[0];
    *slot = Impl::Voice();
    slot->data = &m->buf[id];
    slot->pitch = pitch;
    slot->vol = volume * m->catVol[soundCategory(id)];
    slot->active = true;
    m->computeGains(*slot);
}

void Audio::play3D(int id, vec3 pos, float volume, float pitch, float ref) {
    if (!m->ok || id < 0 || id >= SND_COUNT) return;
    std::lock_guard<std::mutex> lock(m->mtx);
    Impl::Voice* slot = nullptr;
    for (auto& v : m->voices)
        if (!v.active) { slot = &v; break; }
    if (!slot) return;
    *slot = Impl::Voice();
    slot->data = &m->buf[id];
    slot->pitch = pitch;
    slot->vol = volume * m->catVol[soundCategory(id)];
    slot->spatial = true;
    slot->p = pos;
    slot->ref = ref;
    slot->active = true;
    m->computeGains(*slot);
}

void Audio::setListener(vec3 pos, vec3 left) {
    if (!m->ok) return;
    std::lock_guard<std::mutex> lock(m->mtx);
    m->lpos = pos;
    m->lleft = left;
    for (auto& v : m->voices)
        if (v.active && v.spatial) m->computeGains(v);
}

void Audio::setVolume(float master) { m->master = master; }
void Audio::setCategoryVolume(int category, float volume) {
    if (category >= 0 && category < SC_COUNT) m->catVol[category] = volume;
}
void Audio::setSpatial(bool headphones3D) { m->hrtf = headphones3D; }

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
    std::lock_guard<std::mutex> lock(m->mtx);
    for (auto& v : m->voices) v.active = false;
}
