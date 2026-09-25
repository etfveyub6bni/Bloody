// Weapon definitions with gameplay stats modelled after CS2 values.
#pragma once
#include "core/math.h"

enum WeaponId {
    W_KNIFE = 0,
    W_GLOCK,
    W_USP,
    W_DEAGLE,
    W_AK47,
    W_M4A4,
    W_AWP,
    W_HE,
    W_SMOKE,
    W_FLASH,
    W_C4,
    W_COUNT,
    W_NONE = -1
};

enum WeaponSlot { SLOT_PRIMARY = 0, SLOT_SECONDARY, SLOT_KNIFE, SLOT_GRENADE, SLOT_BOMB, SLOT_COUNT };
enum WeaponClass { WC_KNIFE, WC_PISTOL, WC_RIFLE, WC_SNIPER, WC_GRENADE, WC_BOMB };

struct WeaponDef {
    const char* name;
    const char* key;
    int slot;
    int cls;
    int price;
    int team;  // 0 both, 1 T only, 2 CT only
    float damage;
    float armorPen;     // fraction of damage that goes through armor
    float rangeMod;     // damage multiplier per 500 units
    float penetration;  // wall penetration power (units of stone)
    float cycleTime;    // seconds between shots
    bool automatic;
    int magSize;
    int reserve;
    float reloadTime;
    float deployTime;
    float maxSpeed;
    float scopedSpeed;
    float spreadStand, spreadCrouch, spreadMove, spreadAir, spreadFire, spreadMaxFire, recovery;  // degrees / seconds
    float headMult;
    int killReward;
    int recoilPattern;  // index into recoil tables, -1 none
    float recoilScale;
    bool silenced;
};

const WeaponDef& weaponDef(int id);
// Cumulative bullet offset (degrees yaw-right, pitch-up) for the given shot of a spray.
vec2 recoilOffset(int pattern, int shot);
int weaponFromKey(const char* key);
