#include "game/weapons.h"

#include <cstring>

namespace {

// name, key, slot, cls, price, team, dmg, armorPen, rangeMod, pen, cycle, auto, mag, reserve, reload, deploy,
// speed, scopedSpeed, spread stand/crouch/move/air/fire/maxFire, recovery, headMult, reward, pattern, recoilScale, silenced
const WeaponDef kDefs[W_COUNT] = {
    {"Нож", "knife", SLOT_KNIFE, WC_KNIFE, 0, 0, 40, 0.85f, 1, 0, 0.5f, true, 0, 0, 0, 0.75f, 250, 250,
     0, 0, 0, 0, 0, 0, 0, 1, 1500, -1, 0, false},
    {"Glock-18", "glock", SLOT_SECONDARY, WC_PISTOL, 200, 1, 30, 0.47f, 0.85f, 1, 0.15f, false, 20, 120, 2.27f, 0.8f, 240, 240,
     0.45f, 0.35f, 2.2f, 6, 0.9f, 3.5f, 0.32f, 4, 300, 3, 1, false},
    {"USP-S", "usp", SLOT_SECONDARY, WC_PISTOL, 200, 2, 35, 0.505f, 0.91f, 1, 0.17f, false, 12, 24, 2.2f, 0.8f, 240, 240,
     0.25f, 0.2f, 2.0f, 6, 1.0f, 4.0f, 0.3f, 4, 300, 2, 1, true},
    {"Desert Eagle", "deagle", SLOT_SECONDARY, WC_PISTOL, 700, 0, 53, 0.932f, 0.81f, 2, 0.225f, false, 7, 35, 2.2f, 0.9f, 230, 230,
     0.35f, 0.28f, 3.5f, 8, 2.2f, 6.0f, 0.45f, 4, 300, 4, 1, false},
    {"AK-47", "ak47", SLOT_PRIMARY, WC_RIFLE, 2700, 1, 36, 0.775f, 0.98f, 2, 0.1f, true, 30, 90, 2.43f, 1.0f, 215, 215,
     0.3f, 0.22f, 7.0f, 12, 0.35f, 2.2f, 0.36f, 4, 300, 0, 1, false},
    {"M4A4", "m4a4", SLOT_PRIMARY, WC_RIFLE, 3100, 2, 33, 0.70f, 0.97f, 2, 0.09f, true, 30, 90, 3.07f, 1.0f, 225, 225,
     0.25f, 0.18f, 5.5f, 10, 0.3f, 1.9f, 0.32f, 4, 300, 1, 1, false},
    {"AWP", "awp", SLOT_PRIMARY, WC_SNIPER, 4750, 0, 115, 0.975f, 0.99f, 3, 1.46f, false, 5, 30, 3.67f, 1.25f, 200, 100,
     4.5f, 4.0f, 11.0f, 16, 0, 0, 1.4f, 4, 100, -1, 1, false},
    {"HE граната", "he", SLOT_GRENADE, WC_GRENADE, 300, 0, 98, 0.57f, 1, 0, 1.0f, false, 1, 0, 0, 0.6f, 245, 245,
     0, 0, 0, 0, 0, 0, 0, 1, 300, -1, 0, false},
    {"Дымовая", "smoke", SLOT_GRENADE, WC_GRENADE, 300, 0, 0, 1, 1, 0, 1.0f, false, 1, 0, 0, 0.6f, 245, 245,
     0, 0, 0, 0, 0, 0, 0, 1, 300, -1, 0, false},
    {"Световая", "flash", SLOT_GRENADE, WC_GRENADE, 200, 0, 0, 1, 1, 0, 1.0f, false, 1, 0, 0, 0.6f, 245, 245,
     0, 0, 0, 0, 0, 0, 0, 1, 300, -1, 0, false},
    {"C4", "c4", SLOT_BOMB, WC_BOMB, 0, 1, 0, 1, 1, 0, 1.0f, false, 1, 0, 0, 0.8f, 250, 250,
     0, 0, 0, 0, 0, 0, 0, 1, 0, -1, 0, false},
};

// Cumulative bullet offsets (yaw right, pitch up) in degrees.
const vec2 kAK[30] = {{0, 0}, {0.0f, 0.42f}, {0.06f, 0.98f}, {-0.05f, 1.62f}, {0.10f, 2.30f}, {0.22f, 2.98f}, {0.06f, 3.58f},
                      {-0.20f, 4.08f}, {-0.36f, 4.48f}, {-0.12f, 4.78f}, {-0.62f, 4.98f}, {-1.22f, 5.08f}, {-1.90f, 5.15f},
                      {-2.48f, 5.28f}, {-2.80f, 5.44f}, {-2.52f, 5.50f}, {-1.62f, 5.55f}, {-0.62f, 5.52f}, {0.40f, 5.60f},
                      {1.30f, 5.70f}, {2.00f, 5.75f}, {2.42f, 5.80f}, {2.12f, 5.86f}, {1.42f, 5.94f}, {0.92f, 6.00f},
                      {1.22f, 6.02f}, {0.62f, 6.06f}, {-0.20f, 6.10f}, {-0.62f, 6.12f}, {-0.92f, 6.16f}};
const vec2 kM4[30] = {{0, 0}, {0.0f, 0.36f}, {0.04f, 0.82f}, {-0.04f, 1.34f}, {0.06f, 1.88f}, {0.16f, 2.40f}, {0.08f, 2.86f},
                      {-0.10f, 3.24f}, {0.10f, 3.52f}, {0.42f, 3.72f}, {0.86f, 3.84f}, {1.28f, 3.92f}, {1.52f, 4.00f},
                      {1.30f, 4.06f}, {0.80f, 4.12f}, {0.20f, 4.16f}, {-0.46f, 4.20f}, {-1.10f, 4.26f}, {-1.60f, 4.30f},
                      {-1.86f, 4.36f}, {-1.62f, 4.40f}, {-1.10f, 4.44f}, {-0.52f, 4.48f}, {0.06f, 4.52f}, {0.52f, 4.56f},
                      {0.80f, 4.60f}, {0.56f, 4.62f}, {0.10f, 4.64f}, {-0.30f, 4.66f}, {-0.52f, 4.68f}};
const vec2 kUSP[12] = {{0, 0}, {0.05f, 1.2f}, {-0.1f, 2.3f}, {0.15f, 3.2f}, {-0.2f, 3.9f}, {0.25f, 4.4f},
                       {-0.3f, 4.8f}, {0.3f, 5.1f}, {-0.3f, 5.3f}, {0.2f, 5.4f}, {-0.2f, 5.5f}, {0.1f, 5.6f}};
const vec2 kGlock[20] = {{0, 0}, {0.05f, 0.9f}, {-0.1f, 1.8f}, {0.1f, 2.6f}, {-0.15f, 3.2f}, {0.2f, 3.7f}, {-0.2f, 4.1f},
                         {0.25f, 4.4f}, {-0.25f, 4.6f}, {0.2f, 4.8f}, {-0.2f, 5.0f}, {0.2f, 5.1f}, {-0.2f, 5.2f},
                         {0.2f, 5.3f}, {-0.2f, 5.4f}, {0.2f, 5.5f}, {-0.2f, 5.6f}, {0.2f, 5.7f}, {-0.2f, 5.8f}, {0.2f, 5.9f}};
const vec2 kDeagle[7] = {{0, 0}, {0.2f, 3.2f}, {-0.4f, 6.0f}, {0.5f, 8.2f}, {-0.5f, 9.8f}, {0.4f, 11.0f}, {-0.3f, 12.0f}};

}  // namespace

const WeaponDef& weaponDef(int id) { return kDefs[id < 0 || id >= W_COUNT ? 0 : id]; }

vec2 recoilOffset(int pattern, int shot) {
    if (shot <= 0 || pattern < 0) return {0, 0};
    const vec2* t = nullptr;
    int n = 0;
    switch (pattern) {
        case 0: t = kAK; n = 30; break;
        case 1: t = kM4; n = 30; break;
        case 2: t = kUSP; n = 12; break;
        case 3: t = kGlock; n = 20; break;
        case 4: t = kDeagle; n = 7; break;
        default: return {0, 0};
    }
    if (shot < n) return t[shot];
    vec2 last = t[n - 1];
    return last + vec2(std::sin(shot * 1.7f) * 0.6f, (shot - n + 1) * 0.05f);
}

int weaponFromKey(const char* key) {
    for (int i = 0; i < W_COUNT; i++)
        if (std::strcmp(kDefs[i].key, key) == 0) return i;
    return W_NONE;
}
