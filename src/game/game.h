// Match simulation: players, weapons, damage, grenades, bomb, rounds and economy.
#pragma once
#include <string>
#include <vector>

#include "game/agent.h"
#include "game/effects.h"
#include "game/movement.h"
#include "game/weapons.h"
#include "world/map.h"

enum GameMode { MODE_COMPETITIVE = 0, MODE_DEATHMATCH, MODE_PRACTICE };
enum RoundPhase { PH_FREEZE = 0, PH_LIVE, PH_END, PH_MATCH_END };
enum BuyExtra { BUY_KEVLAR = 100, BUY_HELMET = 101, BUY_KIT = 102 };

struct UserCmd {
    float forward = 0, side = 0;
    bool jump = false, duck = false, walk = false;
    bool attack = false, attack2 = false, reload = false, use = false, inspect = false, drop = false;
    int slot = -1;  // requested slot, -1 none; 10 = next, 11 = prev, 12 = last used
    float pitch = 0, yaw = 0;
};

struct MatchConfig {
    std::string mapId = "dust";
    int mode = MODE_COMPETITIVE;
    int playerTeam = TEAM_CT;
    int teamSize = 5;
    int difficulty = 1;  // 0 easy .. 3 expert
    int maxRounds = 16;  // MR16 -> first to 9
    int dmMinutes = 8;
    std::string playerName = "Игрок";
};

struct BotBrain {
    std::vector<vec3> path;
    size_t pathIdx = 0;
    vec3 goal;
    bool hasGoal = false;
    float repathAt = 0;
    float stuckCheckAt = 0;
    vec3 stuckPos;
    int target = -1;
    float reactAt = 0;
    vec2 aimError;
    bool aimHead = false;
    vec3 lastKnown;
    float lastKnownTime = -100;
    float strafeDir = 1, strafeUntil = 0;
    float burstUntil = 0, pauseUntil = 0;
    float nextThink = 0;
    float lookYaw = 0, lookUntil = 0;
    int site = 0;
    float jumpUntil = 0;
};

struct Player {
    int id = 0;
    std::string name;
    int team = TEAM_CT;
    bool bot = true;
    bool alive = false;
    MoveState mv;
    vec3 prevOrigin;
    float pitch = 0, yaw = 0;
    int health = 100, armor = 0;
    bool helmet = false, kit = false;
    int money = 800;
    int kills = 0, deaths = 0, assists = 0, score = 0, damageDealt = 0, mvps = 0;
    int slots[SLOT_COUNT];
    int grenades[3] = {0, 0, 0};  // HE, smoke, flash
    int clip[W_COUNT] = {}, reserve[W_COUNT] = {};
    int active = W_KNIFE, lastWeapon = W_NONE;
    float deployAt = -10, nextAttack = 0, reloadStart = -1, inspectStart = -1, lastShot = -10;
    float sprayIndex = 0;
    vec2 recoil;        // camera kick (degrees yaw-right, pitch-up)
    vec2 recoilTarget;
    float fireInacc = 0;
    bool attackHeld = false, attack2Held = false, useHeld = false;
    int zoom = 0, rezoom = 0;
    float rezoomAt = -1;
    float boltStart = -10;
    float attackStart = -10;
    int attackKind = 0;
    bool attackDone = true;
    float throwStart = -1;
    bool thrown = false;
    float plantStart = -1, defuseStart = -1;
    bool hasBomb = false;
    float walkPhase = 0;
    float lastHurt = -10;
    vec3 lastHurtFrom;
    int lastAttacker = -1;
    float flashUntil = 0, flashDur = 1;
    float deathTime = -10, respawnAt = -1;
    float tagUntil = 0;
    bool survivedRound = false;
    Ragdoll ragdoll;
    AgentPose pose;
    Hitbox hitboxes[kMaxHitboxes];
    int numHitboxes = 0;
    UserCmd cmd;
    BotBrain brain;
    // Viewmodel-only animation hints.
    int shotsFired = 0;
};

struct Grenade {
    int type = W_HE;
    int owner = -1;
    vec3 pos, vel;
    quat rot;
    vec3 angVel;
    float time = 0;
    bool active = true;
};

struct WorldItem {
    int weapon = W_NONE;
    int clip = 0, reserve = 0;
    vec3 pos, vel;
    quat rot;
    bool resting = false;
    float time = 0;
    int droppedBy = -1;
};

struct KillFeedEntry {
    std::string killer, victim;
    int killerTeam = 0, victimTeam = 0;
    int weapon = 0;
    bool headshot = false, wallbang = false;
    float time = 0;
    bool involvesLocal = false;
};

enum GameEventType {
    EV_SHOT, EV_EMPTY, EV_RELOAD, EV_DEPLOY, EV_ZOOM, EV_BOLT, EV_HIT_BODY, EV_HIT_HEAD, EV_HIT_HELMET, EV_KILL,
    EV_IMPACT, EV_FOOTSTEP, EV_JUMP, EV_LAND, EV_KNIFE_SWING, EV_KNIFE_HIT, EV_GRENADE_THROW, EV_GRENADE_BOUNCE,
    EV_EXPLOSION, EV_SMOKE, EV_FLASH, EV_BOMB_PLANTED, EV_BOMB_BEEP, EV_BOMB_DEFUSED, EV_BOMB_EXPLODED, EV_ROUND_START,
    EV_ROUND_END, EV_BUY, EV_PICKUP, EV_PLANT_START, EV_DEFUSE_START, EV_LOCAL_HIT, EV_LOCAL_KILL, EV_LOCAL_DEATH
};

struct GameEvent {
    int type = 0;
    vec3 pos;
    int player = -1;
    int weapon = -1;
    int surface = 0;
    float value = 0;
};

class Game {
public:
    MatchConfig cfg;
    GameMap* map = nullptr;
    const WeaponModel* weaponModels = nullptr;  // array of W_COUNT
    std::vector<Player> players;
    int local = 0;
    float time = 0;
    RoundPhase phase = PH_FREEZE;
    float phaseEnd = 0;
    float roundEndTime = 0;
    int round = 0;
    int scoreT = 0, scoreCT = 0;
    int lossT = 0, lossCT = 0;
    int roundWinner = 0;
    std::string roundMessage;
    bool bombPlanted = false, bombDefused = false, bombExploded = false;
    vec3 bombPos;
    float bombExplodeAt = 0, nextBeep = 0;
    int bombSite = 0;
    Effects fx;
    std::vector<Grenade> grenades;
    std::vector<WorldItem> items;
    std::vector<KillFeedEntry> killfeed;
    std::vector<GameEvent> events;
    vec3 localMuzzle;  // world-space muzzle of the local viewmodel (for tracers)
    bool localFirstPerson = true;
    float hitMarkerTime = -10;
    bool hitMarkerHead = false;
    bool matchOver = false;
    int matchWinner = 0;
    Rng rng{12345};

    void start(const MatchConfig& c, GameMap* m, const WeaponModel* models);
    void tick(float dt);

    Player& localPlayer() { return players[local]; }
    const Player& localPlayer() const { return players[local]; }
    vec3 eyePos(const Player& p) const;
    float maxSpeed(const Player& p) const;
    bool canBuy(const Player& p) const;
    bool buy(Player& p, int item);
    int itemPrice(const Player& p, int item) const;
    bool isEnemy(const Player& a, const Player& b) const;
    int aliveCount(int team) const;
    float roundTimeLeft() const;
    float buyTimeLeft() const;
    bool inBuyZone(const Player& p) const;
    int siteAt(vec3 pos) const;
    void selectBestWeapon(Player& p);

private:
    void startRound();
    void endRound(int winner, const std::string& msg, bool bombWin);
    void spawnPlayer(Player& p, bool keepWeapons);
    void giveWeapon(Player& p, int w);
    void selectSlot(Player& p, int slot);
    void updatePlayer(Player& p, float dt);
    void updateWeapons(Player& p, float dt);
    void fireWeapon(Player& p);
    void fireBullet(Player& p, vec3 start, vec3 dir, float damage, int weapon);
    void knifeHit(Player& p);
    void throwGrenade(Player& p);
    void applyDamage(Player& v, Player* att, float dmg, int group, int weapon, vec3 dir, bool wallbang, vec3 hitPos);
    void killPlayer(Player& v, Player* att, int weapon, bool headshot, vec3 dir, bool wallbang, vec3 hitPos);
    void dropWeapon(Player& p, int slot, vec3 dir);
    void updateItems(float dt);
    void updateGrenades(float dt);
    void explodeHE(const Grenade& g);
    void updateBomb(float dt);
    void updatePose(Player& p, float dt);
    void separatePlayers();
    void checkRoundEnd();
    void emit(int type, vec3 pos, int player = -1, int weapon = -1, float value = 0, int surface = 0);
    void updateBots(float dt);
    void botThink(Player& p, float dt);
    void botBuy(Player& p);
    vec3 muzzleWorld(const Player& p) const;
    vec3 findFreeSpot(vec3 pos) const;
};

int grenadeIndex(int weapon);
