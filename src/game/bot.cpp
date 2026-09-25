// Bot AI: perception, aiming with human-like error, navigation and round objectives.
#include "game/game.h"

namespace {

struct Skill {
    float react, turnSpeed, errDeg, headChance, sprayComp, burstLen;
};
const Skill kSkills[4] = {
    {0.62f, 170.0f, 5.5f, 0.10f, 0.15f, 0.18f},
    {0.40f, 280.0f, 3.2f, 0.25f, 0.45f, 0.26f},
    {0.26f, 440.0f, 1.8f, 0.45f, 0.75f, 0.34f},
    {0.17f, 640.0f, 0.9f, 0.65f, 0.92f, 0.45f},
};

float yawTo(vec3 from, vec3 to) { return std::atan2(to.y - from.y, to.x - from.x) / kDeg; }
float pitchTo(vec3 from, vec3 to) {
    vec3 d = to - from;
    return std::atan2(d.z, length(vec2(d.x, d.y))) / kDeg;
}
float turn(float cur, float target, float maxStep, float dt) {
    float d = wrapAngle(target - cur);
    float step = d * std::min(1.0f, 9.0f * dt);
    step += (d > 0 ? 1.0f : -1.0f) * std::min(std::fabs(d) - std::fabs(step), maxStep * 0.35f) * (std::fabs(d) > std::fabs(step) ? 1.0f : 0.0f);
    return cur + clampf(step, -maxStep, maxStep);
}

}  // namespace

void Game::updateBots(float dt) {
    for (auto& p : players)
        if (p.bot && p.alive) botThink(p, dt);
}

void Game::botBuy(Player& p) {
    if (cfg.mode != MODE_COMPETITIVE) return;
    int rifle = p.team == TEAM_T ? W_AK47 : W_M4A4;
    if (p.slots[SLOT_PRIMARY] == W_NONE) {
        if (p.money >= 4750 + 1000 && rng.chance(0.15f)) buy(p, W_AWP);
        else if (p.money >= weaponDef(rifle).price + 650) buy(p, rifle);
        else if (p.money >= 1700 && rng.chance(0.5f) && p.slots[SLOT_SECONDARY] != W_DEAGLE) buy(p, W_DEAGLE);
    }
    if (p.money >= 1000 && !(p.armor >= 100 && p.helmet)) buy(p, BUY_HELMET);
    else if (p.money >= 650 && p.armor < 100) buy(p, BUY_KEVLAR);
    if (p.team == TEAM_CT && p.money >= 400 && rng.chance(0.5f)) buy(p, BUY_KIT);
    if (p.money >= 300 && rng.chance(0.45f)) buy(p, W_HE);
    if (p.money >= 300 && rng.chance(0.3f)) buy(p, W_SMOKE);
    if (p.money >= 200 && rng.chance(0.3f)) buy(p, W_FLASH);
    p.active = W_NONE;
    selectBestWeapon(p);
}

void Game::botThink(Player& p, float dt) {
    BotBrain& b = p.brain;
    const Skill& sk = kSkills[std::max(0, std::min(cfg.difficulty, 3))];
    UserCmd c;
    c.pitch = p.pitch;
    c.yaw = p.yaw;
    const bool passive = cfg.mode == MODE_PRACTICE;
    vec3 eye = eyePos(p);
    bool flashed = time < p.flashUntil - 0.4f;

    // Perception at ~10 Hz.
    if (time >= b.nextThink) {
        b.nextThink = time + 0.1f + rng.range(0.0f, 0.04f);
        int best = -1;
        float bestD = 1e9f;
        vec3 fwd = angleForward(0, p.yaw);
        for (auto& o : players) {
            if (!o.alive || !isEnemy(p, o) || flashed) continue;
            vec3 head = (o.hitboxes[0].a + o.hitboxes[0].b) * 0.5f;
            vec3 to = head - eye;
            float d = length(to);
            if (d > 3800.0f) continue;
            bool known = b.target == o.id || (time - p.lastHurt < 1.2f && p.lastAttacker == o.id);
            if (!known && dot(normalize(vec3(to.x, to.y, 0)), fwd) < 0.25f) continue;
            vec3 chest = (o.hitboxes[1].a + o.hitboxes[1].b) * 0.5f;
            bool vis = false;
            for (vec3 tp : {head, chest}) {
                TraceResult tr = map->world.traceRay(eye, tp, MASK_SHOT);
                if (tr.fraction >= 1.0f && !fx.smokeBlocks(eye, tp)) { vis = true; break; }
            }
            if (vis && d < bestD) { bestD = d; best = o.id; }
        }
        if (best != b.target) {
            if (best >= 0) {
                b.reactAt = time + sk.react * rng.range(0.8f, 1.35f);
                float e = sk.errDeg * rng.range(0.6f, 1.4f) * clampf(bestD / 1200.0f, 0.5f, 1.4f);
                float a = rng.range(0, kTwoPi);
                b.aimError = vec2(std::cos(a), std::sin(a)) * e;
                b.aimHead = rng.chance(sk.headChance);
            }
            b.target = best;
        }
        if (best >= 0) {
            b.lastKnown = players[best].mv.origin;
            b.lastKnownTime = time;
        } else {
            for (auto& o : players)
                if (o.alive && isEnemy(p, o) && time - o.lastShot < 0.3f && length(o.mv.origin - p.mv.origin) < 1800.0f) {
                    b.lastKnown = o.mv.origin;
                    b.lastKnownTime = time;
                }
            if (time - p.lastHurt < 0.2f) {
                b.lastKnown = p.lastHurtFrom;
                b.lastKnownTime = time;
            }
        }
    }
    b.aimError = b.aimError * std::exp(-dt * 2.2f);

    // Weapon choice.
    if (p.throwStart < 0 && p.plantStart < 0) {
        int want = SLOT_KNIFE;
        if (p.slots[SLOT_PRIMARY] != W_NONE && p.clip[p.slots[SLOT_PRIMARY]] + p.reserve[p.slots[SLOT_PRIMARY]] > 0) want = SLOT_PRIMARY;
        else if (p.slots[SLOT_SECONDARY] != W_NONE && p.clip[p.slots[SLOT_SECONDARY]] + p.reserve[p.slots[SLOT_SECONDARY]] > 0) want = SLOT_SECONDARY;
        if (p.active != p.slots[want] && !(p.hasBomb && p.active == W_C4 && siteAt(p.mv.origin) >= 0)) c.slot = want;
    }
    const WeaponDef& d = weaponDef(p.active);

    Player* tgt = (b.target >= 0 && players[b.target].alive) ? &players[b.target] : nullptr;
    if (phase == PH_FREEZE || phase == PH_MATCH_END) tgt = nullptr;
    if (tgt && !passive && !flashed) {
        const Hitbox& hb = tgt->hitboxes[b.aimHead ? 0 : 1];
        vec3 aim = (hb.a + hb.b) * 0.5f + tgt->mv.velocity * 0.04f;
        float dist = length(aim - eye);
        float ty = yawTo(eye, aim) + b.aimError.x, tp = pitchTo(eye, aim) + b.aimError.y;
        vec2 rec = recoilOffset(d.recoilPattern, (int)(p.sprayIndex + 0.5f)) * sk.sprayComp;
        ty += rec.x;
        tp -= rec.y;
        float maxTurn = sk.turnSpeed * dt;
        c.yaw = turn(p.yaw, ty, maxTurn, dt);
        c.pitch = clampf(p.pitch + clampf((tp - p.pitch) * std::min(1.0f, 9.0f * dt), -maxTurn, maxTurn), -89, 89);
        float err = std::sqrt(std::pow(wrapAngle(ty - p.yaw), 2.0f) + std::pow(tp - p.pitch, 2.0f));
        float tol = std::max(1.0f, std::atan2(9.0f, dist) / kDeg * 1.6f);
        bool ready = time >= b.reactAt && err < tol;
        if (d.cls == WC_SNIPER && p.zoom == 0 && time - p.lastShot > 1.35f && time >= b.reactAt - 0.2f) c.attack2 = !p.attack2Held;
        if (ready) {
            if (d.cls == WC_RIFLE && dist > 900.0f) {
                if (time > b.pauseUntil) {
                    if (b.burstUntil < time - 0.05f) b.burstUntil = time + sk.burstLen;
                    c.attack = true;
                    if (time >= b.burstUntil) b.pauseUntil = time + 0.32f;
                }
            } else if (d.automatic) {
                c.attack = true;
            } else {
                c.attack = !p.attackHeld;
            }
            if (d.cls == WC_SNIPER && p.zoom == 0) c.attack = false;
        }
        if (time > b.strafeUntil) {
            b.strafeDir = rng.chance(0.5f) ? 1.0f : -1.0f;
            b.strafeUntil = time + rng.range(0.3f, 0.9f);
        }
        bool still = (c.attack && dist > 650.0f && cfg.difficulty >= 1) || d.cls == WC_SNIPER;
        c.side = still ? 0.0f : b.strafeDir;
        c.forward = d.cls == WC_KNIFE ? 1.0f : 0.0f;
        c.duck = cfg.difficulty >= 2 && c.attack && dist > 1300.0f && d.cls == WC_RIFLE;
        if (d.magSize > 0 && p.clip[p.active] == 0) c.reload = true;
        p.cmd = c;
        return;
    }

    // Objectives.
    vec3 goal = p.mv.origin;
    bool wantMove = phase == PH_LIVE;
    bool atObjective = false;
    if (cfg.mode == MODE_COMPETITIVE) {
        if (bombPlanted) {
            goal = bombPos;
            if (p.team == TEAM_T) goal += vec3(std::cos(p.id * 1.7f) * 220.0f, std::sin(p.id * 1.7f) * 220.0f, 0);
        } else {
            int dropped = -1;
            for (size_t i = 0; i < items.size(); i++)
                if (items[i].weapon == W_C4) dropped = (int)i;
            if (p.team == TEAM_T && dropped >= 0) {
                goal = items[dropped].pos;
            } else if (!map->sites.empty()) {
                const BombSite& s = map->sites[b.site % map->sites.size()];
                if (!b.hasGoal || !s.box.contains(b.goal + vec3(0, 0, 8))) {
                    vec3 rp;
                    if (map->nav.randomPoint(s.box, rng, rp)) goal = rp;
                    else goal = s.box.center();
                } else {
                    goal = b.goal;
                }
            }
        }
        if (time - b.lastKnownTime < 3.0f && length(b.lastKnown - p.mv.origin) < 1400.0f && !p.hasBomb && !bombPlanted) goal = b.lastKnown;
    } else {
        if (!b.hasGoal || length(b.goal - p.mv.origin) < 80.0f || time > b.repathAt + 8.0f) {
            vec3 rp;
            if (time - b.lastKnownTime < 4.0f) goal = b.lastKnown;
            else if (map->nav.randomPoint(map->bounds, rng, rp)) goal = rp;
        } else {
            goal = b.goal;
        }
    }
    float goalDist = length(vec2(goal.x - p.mv.origin.x, goal.y - p.mv.origin.y));
    atObjective = goalDist < 72.0f;

    // Plant / defuse.
    if (cfg.mode == MODE_COMPETITIVE && phase == PH_LIVE) {
        if (p.hasBomb && siteAt(p.mv.origin) >= 0 && (atObjective || p.plantStart >= 0 || rng.chance(0.02f))) {
            c.use = true;
            wantMove = false;
        }
        if (p.team == TEAM_CT && bombPlanted && length(p.mv.origin - bombPos) < 56.0f) {
            c.use = true;
            wantMove = false;
        }
    }

    if (wantMove && !atObjective) {
        if (!b.hasGoal || time > b.repathAt || length(goal - b.goal) > 96.0f) {
            b.goal = goal;
            b.hasGoal = true;
            map->nav.findPath(p.mv.origin, goal, b.path);
            map->nav.smoothPath(b.path, map->world);
            b.pathIdx = b.path.size() > 1 ? 1 : 0;
            b.repathAt = time + rng.range(4.0f, 7.0f);
        }
        if (b.pathIdx < b.path.size()) {
            vec3 wp = b.path[b.pathIdx];
            vec2 dd(wp.x - p.mv.origin.x, wp.y - p.mv.origin.y);
            if (length(dd) < 40.0f && b.pathIdx + 1 < b.path.size()) {
                b.pathIdx++;
                wp = b.path[b.pathIdx];
                dd = vec2(wp.x - p.mv.origin.x, wp.y - p.mv.origin.y);
            }
            float want = std::atan2(dd.y, dd.x) / kDeg;
            c.yaw = turn(p.yaw, want, sk.turnSpeed * 0.7f * dt, dt);
            float yerr = std::fabs(wrapAngle(want - p.yaw));
            c.forward = yerr < 50.0f ? 1.0f : 0.25f;
            c.pitch = p.pitch + (0.0f - p.pitch) * std::min(1.0f, 4.0f * dt);
        } else {
            float want = yawTo(p.mv.origin, goal);
            c.yaw = turn(p.yaw, want, sk.turnSpeed * 0.7f * dt, dt);
            c.forward = 1.0f;
        }
        if (time >= b.stuckCheckAt) {
            if (length(p.mv.origin - b.stuckPos) < 14.0f && c.forward > 0.5f) {
                b.jumpUntil = time + 0.2f;
                b.repathAt = 0;
                b.strafeDir = rng.chance(0.5f) ? 1.0f : -1.0f;
                b.strafeUntil = time + 0.6f;
            }
            b.stuckPos = p.mv.origin;
            b.stuckCheckAt = time + 1.0f;
        }
        if (time < b.jumpUntil) c.jump = true;
        if (time < b.strafeUntil && time < b.jumpUntil + 0.5f) c.side = b.strafeDir;
    } else {
        // Hold position and scan.
        if (time > b.lookUntil) {
            b.lookYaw = time - b.lastKnownTime < 5.0f ? yawTo(p.mv.origin, b.lastKnown) : p.yaw + rng.range(-120.0f, 120.0f);
            b.lookUntil = time + rng.range(1.2f, 2.8f);
        }
        c.yaw = turn(p.yaw, b.lookYaw, sk.turnSpeed * 0.4f * dt, dt);
        c.pitch = p.pitch * (1.0f - std::min(1.0f, 3.0f * dt));
    }
    if (!tgt && d.magSize > 0 && p.clip[p.active] < d.magSize * 0.35f && p.reserve[p.active] > 0 && time - b.lastKnownTime > 2.0f) c.reload = true;
    if (passive) c.attack = c.attack2 = false;
    p.cmd = c;
}
