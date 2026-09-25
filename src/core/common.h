#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

void logInfo(const char* fmt, ...);
void logError(const char* fmt, ...);

// Finds the assets directory (next to the executable, working dir, or source tree).
std::string assetPath(const std::string& relative);
void setExecutableDir(const std::string& dir);
bool readFileBytes(const std::string& path, std::vector<unsigned char>& out);

// Small fast PRNG (PCG32).
struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL;
    explicit Rng(uint64_t seed = 1) { seedWith(seed); }
    void seedWith(uint64_t seed) { state = seed * 6364136223846793005ULL + 1442695040888963407ULL; next(); }
    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t xs = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xs >> rot) | (xs << ((32 - rot) & 31));
    }
    float f01() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f01(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }  // inclusive
    bool chance(float p) { return f01() < p; }
    vec3 unitVec() {
        float z = range(-1, 1), a = range(0, kTwoPi), r = std::sqrt(1 - z * z);
        return {r * std::cos(a), r * std::sin(a), z};
    }
};

struct Color {
    float r, g, b, a;
};

inline uint32_t packRGBA(float r, float g, float b, float a) {
    auto c = [](float v) { return (uint32_t)(saturate(v) * 255.0f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}
