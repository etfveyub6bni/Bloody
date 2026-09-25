#include "game/game.h"

#include "assets/textures.h"

namespace {

const char* kBotNames[] = {"Волк", "Сокол", "Гром", "Тень", "Кобра", "Медведь", "Ястреб", "Барс", "Вихрь", "Рысь", "Шторм", "Кречет"};

float horizSpeed(const MoveState& m) { return std::sqrt(m.velocity.x * m.velocity.x + m.velocity.y * m.velocity.y); }

}  // namespace

int grenadeIndex(int w) { return w == W_HE ? 0 : w == W_SMOKE ? 1 : w == W_FLASH ? 2 : -1; }

void Game::emit(int type, vec3 pos, int player, int weapon, float value, int surface) {
    GameEvent e;
    e.type = type;
    e.pos = pos;
    e.player = player;
    e.weapon = weapon;
    e.value = value;
    e.surface = surface;
    events.push_back(e);
}

void Game::start(const MatchConfig& c, GameMap* m, const WeaponModel* models) {
    cfg = c;
    map = m;
    weaponModels = models;
    players.clear();
    fx.clear();
    grenades.clear();
    items.clear();
    killfeed.clear();
    events.clear();
    time = 0;
    round = 0;
    scoreT = scoreCT = lossT = lossCT = 0;
    matchOver = false;
    int teamSize = std::max(1, std::min(cfg.teamSize, 5));
    Player me;
    me.id = 0;
    me.name = cfg.playerName;
    me.team = cfg.playerTeam;
    me.bot = false;
    players.push_back(me);
    int nameIdx = (int)(rng.next() % 12);
    auto addBot = [&](int team) {
        Player b;
        b.id = (int)players.size();
        b.team = team;
        b.bot = true;
        b.name = kBotNames[nameIdx++ % 12];
        players.push_back(b);
    };
    int other = cfg.playerTeam == TEAM_T ? TEAM_CT : TEAM_T;
    if (cfg.mode == MODE_COMPETITIVE) {
        for (int i = 1; i < teamSize; i++) addBot(cfg.playerTeam);
        for (int i = 0; i < teamSize; i++) addBot(other);
    } else {
        for (int i = 0; i < teamSize * 2 - 1; i++) addBot(i % 2 == 0 ? other : cfg.playerTeam);
    }
    for (auto& p : players) {
        for (int s = 0; s < SLOT_COUNT; s++) p.slots[s] = W_NONE;
        p.money = cfg.mode == MODE_COMPETITIVE ? 800 : 16000;
    }
    local = 0;
    startRound();
}

bool Game::isEnemy(const Player& a, const Player& b) const {
    if (a.id == b.id) return false;
    if (cfg.mode != MODE_COMPETITIVE) return true;
    return a.team != b.team;
}

int Game::aliveCount(int team) const {
    int n = 0;
    for (const auto& p : players) n += (p.alive && p.team == team) ? 1 : 0;
    return n;
}

vec3 Game::eyePos(const Player& p) const { return p.mv.origin + vec3(0, 0, eyeHeight(p.mv)); }

float Game::maxSpeed(const Player& p) const {
    const WeaponDef& d = weaponDef(p.active);
    float s = (d.cls == WC_SNIPER && p.zoom > 0) ? d.scopedSpeed : d.maxSpeed;
    if (time < p.tagUntil) s *= 0.55f;
    return s;
}

float Game::roundTimeLeft() const {
    if (bombPlanted) return std::max(0.0f, bombExplodeAt - time);
    if (phase == PH_FREEZE) return std::max(0.0f, phaseEnd - time);
    return std::max(0.0f, roundEndTime - time);
}

float Game::buyTimeLeft() const {
    if (cfg.mode != MODE_COMPETITIVE) return 999;
    float buyEnd = (phase == PH_FREEZE ? phaseEnd : roundEndTime - 115.0f) + 20.0f;
    return std::max(0.0f, buyEnd - time);
}

bool Game::inBuyZone(const Player& p) const {
    if (cfg.mode != MODE_COMPETITIVE) return true;
    for (const auto& s : map->spawns)
        if (s.team == p.team && length(s.pos - p.mv.origin) < 750.0f) return true;
    return false;
}

bool Game::canBuy(const Player& p) const {
    if (!p.alive || phase == PH_MATCH_END) return false;
    if (cfg.mode != MODE_COMPETITIVE) return true;
    return inBuyZone(p) && buyTimeLeft() > 0 && phase != PH_END;
}

int Game::itemPrice(const Player& p, int item) const {
    if (cfg.mode != MODE_COMPETITIVE) return 0;
    if (item == BUY_KEVLAR) return 650;
    if (item == BUY_HELMET) return p.armor >= 100 ? 350 : 1000;
    if (item == BUY_KIT) return 400;
    return weaponDef(item).price;
}

bool Game::buy(Player& p, int item) {
    if (!canBuy(p)) return false;
    int price = itemPrice(p, item);
    if (p.money < price) return false;
    if (item == BUY_KEVLAR) {
        if (p.armor >= 100) return false;
        p.armor = 100;
    } else if (item == BUY_HELMET) {
        if (p.armor >= 100 && p.helmet) return false;
        p.armor = 100;
        p.helmet = true;
    } else if (item == BUY_KIT) {
        if (p.team != TEAM_CT || p.kit) return false;
        p.kit = true;
    } else {
        const WeaponDef& d = weaponDef(item);
        if (d.team != 0 && d.team != p.team && cfg.mode == MODE_COMPETITIVE) return false;
        if (d.cls == WC_GRENADE) {
            int gi = grenadeIndex(item);
            if (gi < 0) return false;
            int total = p.grenades[0] + p.grenades[1] + p.grenades[2];
            if (p.grenades[gi] >= (item == W_FLASH ? 2 : 1) || total >= 4) return false;
            p.grenades[gi]++;
            if (p.slots[SLOT_GRENADE] == W_NONE) p.slots[SLOT_GRENADE] = item;
        } else {
            if (p.slots[d.slot] == item) {
                p.reserve[item] = d.reserve;
                p.clip[item] = d.magSize;
                p.money -= price;
                return true;
            }
            if (p.slots[d.slot] != W_NONE) dropWeapon(p, d.slot, angleForward(0, p.yaw));
            giveWeapon(p, item);
            selectSlot(p, d.slot);
        }
    }
    p.money -= price;
    emit(EV_BUY, p.mv.origin, p.id, item);
    return true;
}

void Game::giveWeapon(Player& p, int w) {
    const WeaponDef& d = weaponDef(w);
    if (d.cls == WC_GRENADE) {
        int gi = grenadeIndex(w);
        if (gi < 0) return;
        p.grenades[gi]++;
        if (p.slots[SLOT_GRENADE] == W_NONE) p.slots[SLOT_GRENADE] = w;
        return;
    }
    p.slots[d.slot] = w;
    p.clip[w] = d.magSize;
    p.reserve[w] = d.reserve;
}

void Game::selectSlot(Player& p, int slot) {
    int w = W_NONE;
    if (slot == SLOT_GRENADE) {
        const int order[3] = {W_HE, W_FLASH, W_SMOKE};
        int start = 0;
        if (p.active == p.slots[SLOT_GRENADE] && weaponDef(p.active).cls == WC_GRENADE) {
            for (int i = 0; i < 3; i++)
                if (order[i] == p.active) start = i + 1;
        }
        for (int k = 0; k < 3; k++) {
            int cand = order[(start + k) % 3];
            if (p.grenades[grenadeIndex(cand)] > 0) { w = cand; break; }
        }
        if (w == W_NONE) return;
        p.slots[SLOT_GRENADE] = w;
    } else {
        w = p.slots[slot];
    }
    if (w == W_NONE || (w == p.active && slot != SLOT_GRENADE)) return;
    if (w == p.active) return;
    p.lastWeapon = p.active;
    p.active = w;
    p.deployAt = time;
    p.nextAttack = time + weaponDef(w).deployTime * 0.7f;
    p.reloadStart = p.inspectStart = -1;
    p.zoom = 0;
    p.rezoomAt = -1;
    p.throwStart = -1;
    p.plantStart = -1;
    p.sprayIndex = 0;
    emit(EV_DEPLOY, p.mv.origin, p.id, w);
}

void Game::selectBestWeapon(Player& p) {
    const int order[] = {SLOT_PRIMARY, SLOT_SECONDARY, SLOT_KNIFE};
    for (int s : order) {
        int w = p.slots[s];
        if (w == W_NONE) continue;
        if (s != SLOT_KNIFE && p.clip[w] + p.reserve[w] <= 0) continue;
        if (p.active != w) {
            p.active = w;
            p.deployAt = time;
            p.nextAttack = time + weaponDef(w).deployTime * 0.7f;
            p.reloadStart = p.inspectStart = -1;
            p.zoom = 0;
        }
        return;
    }
}

void Game::startRound() {
    round++;
    bombPlanted = bombDefused = bombExploded = false;
    grenades.clear();
    items.clear();
    fx.smokes.clear();
    roundMessage.clear();
    std::vector<const SpawnPoint*> sp[3];
    for (const auto& s : map->spawns) sp[s.team].push_back(&s);
    for (int t = 1; t <= 2; t++)
        for (size_t i = sp[t].size(); i > 1; i--) std::swap(sp[t][i - 1], sp[t][rng.next() % i]);
    size_t idx[3] = {0, 0, 0};
    for (auto& p : players) {
        bool keep = p.alive && cfg.mode == MODE_COMPETITIVE && round > 1;
        spawnPlayer(p, keep);
        if (cfg.mode == MODE_COMPETITIVE && !sp[p.team].empty()) {
            const SpawnPoint* s = sp[p.team][idx[p.team]++ % sp[p.team].size()];
            p.mv.origin = findFreeSpot(s->pos + vec3(0, 0, 1));
            p.prevOrigin = p.mv.origin;
            p.yaw = s->yaw;
        }
    }
    int tSite = map->sites.empty() ? 0 : (int)(rng.next() % map->sites.size());
    if (cfg.mode == MODE_COMPETITIVE) {
        std::vector<int> ts;
        for (auto& p : players)
            if (p.team == TEAM_T) ts.push_back(p.id);
        if (!ts.empty()) {
            Player& c = players[ts[rng.next() % ts.size()]];
            c.hasBomb = true;
            c.slots[SLOT_BOMB] = W_C4;
        }
        phase = PH_FREEZE;
        phaseEnd = time + (round == 1 ? 7.0f : 5.0f);
        roundEndTime = phaseEnd + 115.0f;
        for (auto& p : players)
            if (p.bot) botBuy(p);
    } else {
        phase = PH_LIVE;
        phaseEnd = time + cfg.dmMinutes * 60.0f;
        roundEndTime = phaseEnd;
    }
    int ctSplit = 0;
    for (auto& p : players) {
        p.brain = BotBrain();
        p.brain.site = p.team == TEAM_T ? tSite : (int)((ctSplit++) % std::max<size_t>(1, map->sites.size()));
        p.survivedRound = false;
    }
    emit(EV_ROUND_START, vec3(0));
}

vec3 Game::findFreeSpot(vec3 pos) const {
    auto freeAt = [&](vec3 p) {
        if (map->world.trace(p, p, hullMins(), hullMaxs(false), MASK_PLAYER).startSolid) return false;
        return map->world.trace(p, p - vec3(0, 0, 64), hullMins(), hullMaxs(false), MASK_PLAYER).fraction < 1.0f;
    };
    if (freeAt(pos)) return pos;
    for (float r = 24.0f; r <= 240.0f; r += 24.0f)
        for (int k = 0; k < 12; k++) {
            float a = k * kTwoPi / 12;
            vec3 c = pos + vec3(std::cos(a) * r, std::sin(a) * r, 0);
            if (freeAt(c)) return c;
        }
    return pos;
}

void Game::spawnPlayer(Player& p, bool keep) {
    const SpawnPoint* sp = nullptr;
    if (cfg.mode == MODE_COMPETITIVE) {
        sp = map->randomSpawn(p.team, rng);
    } else {
        // Deathmatch: spawn at the candidate farthest from living enemies.
        float best = -1;
        for (int tries = 0; tries < 8; tries++) {
            const SpawnPoint* c = map->randomSpawn(TEAM_NONE, rng);
            vec3 pos = c ? c->pos : vec3(0);
            vec3 rp;
            if (tries >= 3 && map->nav.randomPoint(map->bounds, rng, rp)) pos = rp;
            float dmin = 1e9f;
            for (auto& o : players)
                if (o.alive && o.id != p.id) dmin = std::min(dmin, length(o.mv.origin - pos));
            if (dmin > best) {
                best = dmin;
                static SpawnPoint tmp;
                tmp = SpawnPoint{pos, rng.range(-180, 180), TEAM_NONE};
                sp = &tmp;
            }
        }
    }
    p.mv = MoveState();
    if (sp) {
        p.mv.origin = findFreeSpot(sp->pos + vec3(0, 0, 1));
        p.yaw = sp->yaw;
    }
    p.prevOrigin = p.mv.origin;
    p.pitch = 0;
    p.alive = true;
    p.health = 100;
    p.ragdoll.active = false;
    p.respawnAt = -1;
    p.flashUntil = 0;
    p.plantStart = p.defuseStart = p.reloadStart = p.inspectStart = p.throwStart = -1;
    p.zoom = 0;
    p.rezoomAt = -1;
    p.sprayIndex = 0;
    p.recoil = p.recoilTarget = vec2(0, 0);
    p.fireInacc = 0;
    p.tagUntil = 0;
    p.attackHeld = p.attack2Held = false;
    if (!keep) {
        for (int s = 0; s < SLOT_COUNT; s++) p.slots[s] = W_NONE;
        p.grenades[0] = p.grenades[1] = p.grenades[2] = 0;
        p.hasBomb = false;
        if (cfg.mode == MODE_COMPETITIVE) {
            p.armor = 0;
            p.helmet = false;
            p.kit = false;
        }
    }
    p.slots[SLOT_BOMB] = p.hasBomb ? W_C4 : W_NONE;
    p.slots[SLOT_KNIFE] = W_KNIFE;
    if (p.slots[SLOT_SECONDARY] == W_NONE) giveWeapon(p, p.team == TEAM_T ? W_GLOCK : W_USP);
    if (cfg.mode != MODE_COMPETITIVE) {
        p.armor = 100;
        p.helmet = true;
        if (p.slots[SLOT_PRIMARY] == W_NONE) {
            int options[] = {W_AK47, W_M4A4, W_AK47, W_M4A4, W_AWP};
            int w = p.bot ? options[rng.next() % 5] : (p.team == TEAM_T ? W_AK47 : W_M4A4);
            giveWeapon(p, w);
        }
        if (p.bot && rng.chance(0.4f)) giveWeapon(p, W_DEAGLE);
    }
    for (int s = 0; s < SLOT_COUNT; s++) {
        int w = p.slots[s];
        if (w == W_NONE || s == SLOT_GRENADE || s == SLOT_BOMB) continue;
        const WeaponDef& d = weaponDef(w);
        p.reserve[w] = d.reserve;
        if (p.clip[w] <= 0) p.clip[w] = d.magSize;
    }
    p.active = W_NONE;
    selectBestWeapon(p);
    p.nextAttack = time + 0.3f;
}

void Game::endRound(int winner, const std::string& msg, bool bombWin) {
    if (phase == PH_END || phase == PH_MATCH_END) return;
    phase = PH_END;
    phaseEnd = time + 5.0f;
    roundWinner = winner;
    roundMessage = msg;
    if (winner == TEAM_T) { scoreT++; lossT = 0; lossCT = std::min(lossCT + 1, 4); }
    else if (winner == TEAM_CT) { scoreCT++; lossCT = 0; lossT = std::min(lossT + 1, 4); }
    for (auto& p : players) {
        p.survivedRound = p.alive;
        int bonus;
        if (p.team == winner) bonus = bombWin ? 3500 : 3250;
        else bonus = 1400 + 500 * std::max(0, (p.team == TEAM_T ? lossT : lossCT) - 1);
        if (p.team == TEAM_T && winner == TEAM_CT && bombPlanted) bonus += 800;
        p.money = std::min(16000, p.money + bonus);
    }
    // MVP: most kills this match among winners (approximation).
    int mvp = -1, best = -1;
    for (auto& p : players)
        if (p.team == winner && p.kills > best) { best = p.kills; mvp = p.id; }
    if (mvp >= 0) players[mvp].mvps++;
    emit(EV_ROUND_END, vec3(0), -1, -1, (float)winner);
    logInfo("Round %d: %s  (T %d : CT %d)", round, msg.c_str(), scoreT, scoreCT);
    int need = cfg.maxRounds / 2 + 1;
    if (scoreT >= need || scoreCT >= need || round >= cfg.maxRounds) {
        matchOver = true;
        matchWinner = scoreT > scoreCT ? TEAM_T : scoreCT > scoreT ? TEAM_CT : 0;
    }
}

void Game::checkRoundEnd() {
    if (cfg.mode != MODE_COMPETITIVE) {
        if (phase == PH_LIVE) {
            int top = 0;
            for (auto& p : players) top = std::max(top, p.kills);
            if (time >= phaseEnd || top >= 40) {
                phase = PH_MATCH_END;
                phaseEnd = time + 8.0f;
                matchOver = true;
                int bestId = 0;
                for (auto& p : players)
                    if (p.kills > players[bestId].kills) bestId = p.id;
                roundMessage = "Победитель: " + players[bestId].name;
                matchWinner = players[bestId].team;
            }
        }
        return;
    }
    if (phase != PH_LIVE) return;
    int t = aliveCount(TEAM_T), ct = aliveCount(TEAM_CT);
    if (bombExploded) endRound(TEAM_T, "Бомба взорвана — террористы победили", true);
    else if (bombDefused) endRound(TEAM_CT, "Бомба обезврежена — спецназ победил", true);
    else if (ct == 0) endRound(TEAM_T, "Террористы победили", false);
    else if (t == 0 && !bombPlanted) endRound(TEAM_CT, "Спецназ победил", false);
    else if (!bombPlanted && time >= roundEndTime) endRound(TEAM_CT, "Время вышло — спецназ победил", false);
}

void Game::tick(float dt) {
    time += dt;
    // Phase transitions.
    if (phase == PH_FREEZE && time >= phaseEnd) phase = PH_LIVE;
    if (phase == PH_END && time >= phaseEnd) {
        if (matchOver) {
            phase = PH_MATCH_END;
            phaseEnd = time + 8.0f;
            roundMessage = matchWinner == TEAM_T ? "Матч окончен — победа террористов" : matchWinner == TEAM_CT ? "Матч окончен — победа спецназа" : "Матч окончен — ничья";
        } else {
            startRound();
        }
    }
    updateBots(dt);
    for (auto& p : players) {
        if (p.alive) {
            updatePlayer(p, dt);
        } else {
            if (p.ragdoll.active) p.ragdoll.step(dt, map->world);
            if (cfg.mode != MODE_COMPETITIVE && p.respawnAt > 0 && time >= p.respawnAt && phase == PH_LIVE) spawnPlayer(p, false);
        }
    }
    separatePlayers();
    for (auto& p : players) updatePose(p, dt);
    updateGrenades(dt);
    updateItems(dt);
    updateBomb(dt);
    fx.update(dt, map->world);
    checkRoundEnd();
    while (killfeed.size() > 6) killfeed.erase(killfeed.begin());
}

void Game::updatePlayer(Player& p, float dt) {
    UserCmd& c = p.cmd;
    p.pitch = clampf(c.pitch, -89.0f, 89.0f);
    p.yaw = c.yaw;
    bool frozen = phase == PH_FREEZE || phase == PH_MATCH_END || p.plantStart >= 0 || p.defuseStart >= 0;
    MoveInput mi;
    mi.forward = c.forward;
    mi.side = c.side;
    mi.jump = c.jump;
    mi.duck = c.duck || p.defuseStart >= 0 || p.plantStart >= 0;
    mi.walk = c.walk;
    mi.yaw = p.yaw;
    mi.frozen = frozen;
    mi.maxSpeed = maxSpeed(p);
    p.prevOrigin = p.mv.origin;
    playerMove(p.mv, mi, dt, map->world);
    float spd = horizSpeed(p.mv);
    if (p.mv.stepDistance > 76.0f) {
        p.mv.stepDistance = 0;
        if (spd > 150.0f && !c.walk && !p.mv.ducked) emit(EV_FOOTSTEP, p.mv.origin, p.id);
    }
    if (p.mv.justJumped) emit(EV_JUMP, p.mv.origin, p.id);
    if (p.mv.justLanded) {
        emit(EV_LAND, p.mv.origin, p.id, -1, p.mv.landSpeed);
        if (p.mv.landSpeed > 300.0f) fx.dust(p.mv.origin, saturate((p.mv.landSpeed - 300.0f) / 300.0f) + 0.5f);
        if (p.mv.landSpeed > 580.0f) {
            float dmg = (p.mv.landSpeed - 580.0f) * 0.25f;
            applyDamage(p, nullptr, dmg, HG_LEG, W_KNIFE, vec3(0, 0, -1), false, p.mv.origin);
            if (!p.alive) return;
        }
    }
    if (p.mv.origin.z < -600.0f) {  // fell out of the world
        killPlayer(p, nullptr, W_KNIFE, false, vec3(0, 0, -1), false, p.mv.origin);
        return;
    }
    updateWeapons(p, dt);

    // Bomb plant / defuse.
    if (cfg.mode == MODE_COMPETITIVE && phase == PH_LIVE) {
        bool wantPlant = p.hasBomb && ((p.active == W_C4 && c.attack) || c.use) && p.mv.onGround && siteAt(p.mv.origin) >= 0;
        if (wantPlant && !bombPlanted) {
            if (p.plantStart < 0) {
                p.plantStart = time;
                if (p.active != W_C4) {
                    p.lastWeapon = p.active;
                    p.active = W_C4;
                    p.deployAt = time;
                }
                emit(EV_PLANT_START, p.mv.origin, p.id);
            }
            if (time - p.plantStart >= 3.2f) {
                bombPlanted = true;
                bombPos = p.mv.origin + vec3(0, 0, 1);
                bombSite = siteAt(p.mv.origin);
                bombExplodeAt = time + 40.0f;
                nextBeep = time;
                p.hasBomb = false;
                p.slots[SLOT_BOMB] = W_NONE;
                p.plantStart = -1;
                p.money += 300;
                p.active = W_NONE;
                selectBestWeapon(p);
                roundMessage = std::string("Бомба заложена на точке ") + (char)('A' + bombSite);
                emit(EV_BOMB_PLANTED, bombPos, p.id);
            }
        } else if (p.plantStart >= 0) {
            p.plantStart = -1;
        }
        bool nearBomb = bombPlanted && !bombDefused && !bombExploded && p.team == TEAM_CT && length(p.mv.origin - bombPos) < 64.0f;
        if (nearBomb && c.use && p.mv.onGround) {
            if (p.defuseStart < 0) {
                p.defuseStart = time;
                emit(EV_DEFUSE_START, p.mv.origin, p.id);
            }
            float need = p.kit ? 5.0f : 10.0f;
            if (time - p.defuseStart >= need && time < bombExplodeAt) {
                bombDefused = true;
                p.defuseStart = -1;
                p.money += 300;
                emit(EV_BOMB_DEFUSED, bombPos, p.id);
            }
        } else {
            p.defuseStart = -1;
        }
    }

    // Item pickup.
    for (size_t i = 0; i < items.size(); i++) {
        WorldItem& it = items[i];
        if (length(it.pos - (p.mv.origin + vec3(0, 0, 16))) > 44.0f) continue;
        if (it.droppedBy == p.id && time - it.time < 1.5f) continue;
        if (it.weapon == W_C4) {
            if (p.team != TEAM_T) continue;
            p.hasBomb = true;
            p.slots[SLOT_BOMB] = W_C4;
        } else {
            const WeaponDef& d = weaponDef(it.weapon);
            if (p.slots[d.slot] != W_NONE) continue;
            p.slots[d.slot] = it.weapon;
            p.clip[it.weapon] = it.clip;
            p.reserve[it.weapon] = it.reserve;
        }
        emit(EV_PICKUP, it.pos, p.id, it.weapon);
        items.erase(items.begin() + (long)i);
        break;
    }
}

void Game::updateWeapons(Player& p, float dt) {
    UserCmd& c = p.cmd;
    if (c.slot >= 0) {
        if (c.slot == 12) {
            if (p.lastWeapon != W_NONE) {
                int s = weaponDef(p.lastWeapon).slot;
                if (p.slots[s] == p.lastWeapon || s == SLOT_GRENADE) selectSlot(p, s);
            }
        } else if (c.slot == 10 || c.slot == 11) {
            int cur = weaponDef(p.active).slot;
            for (int k = 1; k <= SLOT_COUNT; k++) {
                int s = (cur + (c.slot == 10 ? k : SLOT_COUNT - k)) % SLOT_COUNT;
                bool has = s == SLOT_GRENADE ? (p.grenades[0] + p.grenades[1] + p.grenades[2]) > 0 : p.slots[s] != W_NONE;
                if (has) { selectSlot(p, s); break; }
            }
        } else if (c.slot < SLOT_COUNT) {
            selectSlot(p, c.slot);
        }
    }
    if (c.drop && weaponDef(p.active).cls != WC_KNIFE && weaponDef(p.active).cls != WC_GRENADE) {
        int s = weaponDef(p.active).slot;
        if (p.active == W_C4 && p.hasBomb) {
            WorldItem it;
            it.weapon = W_C4;
            it.pos = eyePos(p) + angleForward(p.pitch, p.yaw) * 20.0f;
            it.vel = angleForward(p.pitch, p.yaw) * 250.0f;
            it.time = time;
            it.droppedBy = p.id;
            items.push_back(it);
            p.hasBomb = false;
            p.slots[SLOT_BOMB] = W_NONE;
        } else {
            dropWeapon(p, s, angleForward(p.pitch, p.yaw));
        }
        p.active = W_NONE;
        selectBestWeapon(p);
    }
    int w = p.active;
    if (w == W_NONE) {
        selectBestWeapon(p);
        return;
    }
    const WeaponDef& d = weaponDef(w);
    bool infinite = cfg.mode == MODE_PRACTICE && !p.bot;

    if (c.inspect && p.reloadStart < 0 && time >= p.nextAttack && p.inspectStart < 0 && time - p.lastShot > 0.3f) p.inspectStart = time;
    if (p.inspectStart >= 0 && time - p.inspectStart > (d.cls == WC_KNIFE ? 2.5f : d.cls == WC_PISTOL ? 3.0f : 3.6f)) p.inspectStart = -1;

    // Reload.
    if (p.reloadStart >= 0 && time >= p.reloadStart + d.reloadTime) {
        int need = d.magSize - p.clip[w];
        int take = infinite ? need : std::min(need, p.reserve[w]);
        p.clip[w] += take;
        if (!infinite) p.reserve[w] -= take;
        p.reloadStart = -1;
    }
    bool wantReload = c.reload || (p.clip[w] == 0 && c.attack && !p.attackHeld);
    if (wantReload && d.magSize > 0 && p.clip[w] < d.magSize && (p.reserve[w] > 0 || infinite) && p.reloadStart < 0 &&
        time >= p.deployAt + 0.3f && d.cls != WC_GRENADE && time >= p.nextAttack - 0.05f) {
        p.reloadStart = time;
        p.zoom = 0;
        p.rezoomAt = -1;
        p.inspectStart = -1;
        emit(EV_RELOAD, p.mv.origin, p.id, w);
    }

    // Recoil and inaccuracy recovery.
    if (time - p.lastShot > d.cycleTime * 1.4f) {
        p.sprayIndex = std::max(0.0f, p.sprayIndex - dt * 11.0f);
        p.recoilTarget = p.recoilTarget * std::exp(-dt * 7.0f);
    }
    p.recoil = damp(p.recoil, p.recoilTarget, 22.0f, dt);
    p.fireInacc *= std::exp(-dt / std::max(0.05f, d.recovery * 0.45f));
    if (p.rezoomAt > 0 && time >= p.rezoomAt) {
        if (p.active == W_AWP && p.reloadStart < 0) p.zoom = p.rezoom;
        p.rezoomAt = -1;
    }

    bool canFire = time >= p.nextAttack && p.reloadStart < 0 && phase != PH_FREEZE && phase != PH_MATCH_END && p.plantStart < 0;
    if (c.attack) {
        if (d.cls == WC_KNIFE) {
            if (canFire) {
                bool first = time - p.attackStart > 0.8f;
                p.attackStart = time;
                p.attackKind = first ? 0 : 1 - p.attackKind;
                p.attackDone = false;
                p.nextAttack = time + (first ? 0.5f : 0.4f);
                p.inspectStart = -1;
                emit(EV_KNIFE_SWING, eyePos(p), p.id, w);
            }
        } else if (d.cls == WC_GRENADE) {
            if (canFire && p.throwStart < 0) {
                p.throwStart = time;
                p.thrown = false;
                p.nextAttack = time + 1.0f;
                p.inspectStart = -1;
            }
        } else if (d.cls != WC_BOMB && canFire && (d.automatic || !p.attackHeld)) {
            if (p.clip[w] > 0) {
                fireWeapon(p);
            } else if (!p.attackHeld) {
                emit(EV_EMPTY, p.mv.origin, p.id, w);
                p.nextAttack = time + 0.2f;
            }
        }
    }
    if (c.attack2 && !p.attack2Held) {
        if (d.cls == WC_SNIPER && p.reloadStart < 0 && time >= p.deployAt + 0.3f) {
            p.zoom = (p.zoom + 1) % 3;
            p.rezoomAt = -1;
            emit(EV_ZOOM, p.mv.origin, p.id, w);
        } else if (d.cls == WC_KNIFE && canFire) {
            p.attackStart = time;
            p.attackKind = 2;
            p.attackDone = false;
            p.nextAttack = time + 1.0f;
            p.inspectStart = -1;
            emit(EV_KNIFE_SWING, eyePos(p), p.id, w, 1);
        }
    }
    p.attackHeld = c.attack;
    p.attack2Held = c.attack2;
    if (!p.attackDone && time >= p.attackStart + (p.attackKind == 2 ? 0.32f : 0.12f)) {
        p.attackDone = true;
        knifeHit(p);
    }
    if (p.throwStart >= 0 && !p.thrown && time >= p.throwStart + 0.38f) {
        p.thrown = true;
        throwGrenade(p);
    }
    if (p.throwStart >= 0 && time >= p.throwStart + 0.75f) {
        p.throwStart = -1;
        int gi = grenadeIndex(p.active);
        if (gi >= 0 && p.grenades[gi] > 0) p.grenades[gi]--;
        if (p.grenades[0] + p.grenades[1] + p.grenades[2] == 0) p.slots[SLOT_GRENADE] = W_NONE;
        int last = p.lastWeapon;
        p.active = W_NONE;
        if (last != W_NONE && weaponDef(last).cls != WC_GRENADE && p.slots[weaponDef(last).slot] == last) {
            p.active = last;
            p.deployAt = time;
            p.nextAttack = time + 0.4f;
        } else {
            selectBestWeapon(p);
        }
    }
}

vec3 Game::muzzleWorld(const Player& p) const {
    if (p.id == local && localFirstPerson) return localMuzzle;
    if (!weaponModels || p.active == W_NONE) return eyePos(p);
    Xform world(p.mv.origin, qaxis({0, 0, 1}, p.yaw * kDeg));
    return apply(world, apply(p.pose.weapon, weaponModels[p.active].muzzle));
}

void Game::fireWeapon(Player& p) {
    int w = p.active;
    const WeaponDef& d = weaponDef(w);
    bool infinite = cfg.mode == MODE_PRACTICE && !p.bot;
    if (!infinite) p.clip[w]--;
    p.nextAttack = time + d.cycleTime;
    p.lastShot = time;
    p.inspectStart = -1;
    p.shotsFired++;
    float speed = horizSpeed(p.mv);
    float mfrac = saturate((speed - d.maxSpeed * 0.34f) / (d.maxSpeed * 0.66f));
    float air = p.mv.onGround ? 0.0f : d.spreadAir;
    float inacc;
    if (d.cls == WC_SNIPER) {
        inacc = p.zoom > 0 ? 0.02f + d.spreadMove * mfrac * 0.8f + air : d.spreadStand + d.spreadMove * mfrac + air;
        if (p.zoom > 0 && speed > 20.0f && speed < d.maxSpeed * 0.34f) inacc += 0.25f;
    } else {
        inacc = (p.mv.ducked ? d.spreadCrouch : d.spreadStand) + d.spreadMove * mfrac + air + p.fireInacc;
    }
    vec2 off = recoilOffset(d.recoilPattern, (int)(p.sprayIndex + 0.5f)) * d.recoilScale;
    p.sprayIndex += 1.0f;
    p.fireInacc = std::min(p.fireInacc + d.spreadFire, d.spreadMaxFire);
    p.recoilTarget = off * 0.45f + vec2(rng.range(-0.05f, 0.05f), 0.08f);
    if (d.cls == WC_PISTOL || d.cls == WC_SNIPER) p.recoilTarget.y += d.cls == WC_SNIPER ? 1.6f : 0.35f;
    float yaw = p.yaw - off.x, pitch = p.pitch + off.y;
    float a = rng.range(0, kTwoPi), r = inacc * std::pow(rng.f01(), 0.85f);
    yaw += std::cos(a) * r;
    pitch += std::sin(a) * r;
    vec3 dir = angleForward(pitch, yaw);
    fireBullet(p, eyePos(p), dir, d.damage, w);
    emit(EV_SHOT, eyePos(p), p.id, w);
    // Muzzle flash and casing for third-person shooters (local viewmodel effects come from the app).
    if (!(p.id == local && localFirstPerson)) {
        vec3 mz = muzzleWorld(p);
        fx.muzzleFlash(mz, dir, d.silenced);
        if (weaponModels) {
            Xform world(p.mv.origin, qaxis({0, 0, 1}, p.yaw * kDeg));
            Xform wx = world * p.pose.weapon;
            vec3 ep = apply(wx, weaponModels[w].eject);
            vec3 ed = rotate(wx.q, weaponModels[w].ejectDir);
            fx.shell(ep, ed * rng.range(90, 140) + vec3(0, 0, 60), d.cls == WC_PISTOL ? 0.8f : 1.0f);
        }
    }
    if (d.cls == WC_SNIPER) {
        p.boltStart = time;
        if (p.zoom > 0) {
            p.rezoom = p.zoom;
            p.zoom = 0;
            p.rezoomAt = time + 1.3f;
        }
        emit(EV_BOLT, p.mv.origin, p.id, w);
    }
}

void Game::fireBullet(Player& shooter, vec3 start, vec3 dir, float damage, int weapon) {
    const WeaponDef& d = weaponDef(weapon);
    const float range = 8192.0f;
    vec3 pos = start;
    float traveled = 0;
    int pens = 0;
    bool wallbang = false;
    std::vector<int> hit;
    vec3 endPoint = start + dir * range;
    for (int iter = 0; iter < 6 && traveled < range; iter++) {
        float remain = range - traveled;
        TraceResult tr = map->world.traceRay(pos, pos + dir * remain, MASK_SHOT);
        float wallDist = tr.fraction * remain;
        int bestP = -1, bestG = 0;
        float bestT = wallDist;
        for (auto& o : players) {
            if (!o.alive || o.id == shooter.id || std::find(hit.begin(), hit.end(), o.id) != hit.end()) continue;
            vec3 c = o.mv.origin + vec3(0, 0, 36);
            float along = dot(c - pos, dir);
            if (along < -50 || along > bestT + 50) continue;
            if (length2(pos + dir * along - c) > 60 * 60) continue;
            for (int h = 0; h < o.numHitboxes; h++) {
                float t;
                const Hitbox& hb = o.hitboxes[h];
                if (rayCapsule(pos, dir, hb.a, hb.b, hb.r, t) && t < bestT) {
                    bestT = t;
                    bestP = o.id;
                    bestG = hb.group;
                }
            }
        }
        if (bestP >= 0) {
            vec3 hp = pos + dir * bestT;
            float dist = traveled + bestT;
            float dmg = damage * std::pow(d.rangeMod, dist / 500.0f);
            Player& v = players[bestP];
            bool enemy = isEnemy(shooter, v);
            if (enemy) fx.blood(hp, dir, bestG == HG_HEAD, map->world);
            applyDamage(v, &shooter, dmg, bestG, weapon, dir, wallbang, hp);
            hit.push_back(bestP);
            damage *= 0.5f;
            pos = hp + dir * 2.0f;
            traveled += bestT + 2.0f;
            endPoint = hp;
            if (damage < 5.0f) break;
            continue;
        }
        if (tr.fraction >= 1.0f) {
            endPoint = pos + dir * remain;
            break;
        }
        vec3 hp = tr.endpos;
        endPoint = hp;
        const Brush& b = map->world.brushes[tr.brush];
        fx.impact(hp, tr.normal, b.surface);
        emit(EV_IMPACT, hp, shooter.id, weapon, 0, b.surface);
        if (pens >= 2 || d.penetration <= 0) break;
        vec3 exitPos;
        float thick = map->world.solidThickness(hp + dir * 0.3f, dir, 80.0f, &exitPos);
        float maxThick = d.penetration * 14.0f / b.penetrationScale;
        if (thick > maxThick) break;
        damage *= (1.0f - thick / maxThick) * 0.8f;
        if (damage < 3.0f) break;
        fx.impact(exitPos, dir, b.surface);
        pos = exitPos + dir * 0.5f;
        traveled += wallDist + thick + 0.5f;
        pens++;
        wallbang = true;
    }
    if (shooter.shotsFired % 2 == 0 || weaponDef(weapon).cls == WC_SNIPER) fx.tracer(muzzleWorld(shooter), endPoint);
}

void Game::knifeHit(Player& p) {
    vec3 eye = eyePos(p);
    vec3 dir = angleForward(p.pitch, p.yaw);
    float reach = p.attackKind == 2 ? 48.0f : 64.0f;
    TraceResult tr = map->world.traceRay(eye, eye + dir * reach, MASK_SHOT);
    float wallT = tr.fraction * reach;
    int bestP = -1, bestG = HG_CHEST;
    float bestT = wallT;
    for (auto& o : players) {
        if (!o.alive || o.id == p.id) continue;
        for (int h = 0; h < o.numHitboxes; h++) {
            float t;
            const Hitbox& hb = o.hitboxes[h];
            if (rayCapsule(eye, dir, hb.a, hb.b, hb.r + 4.0f, t) && t < bestT) {
                bestT = t;
                bestP = o.id;
                bestG = hb.group;
            }
        }
    }
    if (bestP >= 0) {
        Player& v = players[bestP];
        vec3 vf = angleForward(0, v.yaw);
        bool back = dot(vf, normalize(vec3(dir.x, dir.y, 0))) > 0.5f;
        float dmg = p.attackKind == 2 ? (back ? 180.0f : 65.0f) : (back ? 90.0f : (p.attackKind == 0 ? 40.0f : 25.0f));
        vec3 hp = eye + dir * bestT;
        if (isEnemy(p, v)) fx.blood(hp, dir, false, map->world);
        applyDamage(v, &p, dmg, bestG == HG_HEAD ? HG_CHEST : bestG, W_KNIFE, dir, false, hp);
        emit(EV_KNIFE_HIT, hp, p.id, W_KNIFE, 1);
    } else if (tr.fraction < 1.0f) {
        fx.impact(tr.endpos, tr.normal, map->world.brushes[tr.brush].surface);
        emit(EV_KNIFE_HIT, tr.endpos, p.id, W_KNIFE, 0);
    }
}

void Game::throwGrenade(Player& p) {
    Grenade g;
    g.type = p.active;
    g.owner = p.id;
    vec3 dir = angleForward(std::min(89.0f, p.pitch + 8.0f), p.yaw);
    vec3 eye = eyePos(p);
    TraceResult tr = map->world.trace(eye, eye + dir * 16.0f, vec3(-2), vec3(2), MASK_SHOT);
    g.pos = tr.endpos;
    g.vel = dir * 760.0f + p.mv.velocity * 1.1f;
    g.rot = quat();
    g.angVel = rng.unitVec() * 12.0f;
    g.time = 0;
    grenades.push_back(g);
    emit(EV_GRENADE_THROW, eye, p.id, p.active);
}

void Game::updateGrenades(float dt) {
    for (auto& g : grenades) {
        if (!g.active) continue;
        g.time += dt;
        g.vel.z -= 800.0f * dt;
        vec3 np = g.pos + g.vel * dt;
        TraceResult tr = map->world.trace(g.pos, np, vec3(-2), vec3(2), MASK_SHOT);
        if (tr.fraction < 1.0f && !tr.startSolid) {
            g.pos = tr.endpos;
            float vn = dot(g.vel, tr.normal);
            vec3 vt = g.vel - tr.normal * vn;
            g.vel = vt * (tr.normal.z > 0.7f ? 0.55f : 0.75f) - tr.normal * vn * 0.45f;
            g.angVel *= 0.7f;
            if (std::fabs(vn) > 60.0f) emit(EV_GRENADE_BOUNCE, g.pos, g.owner, g.type);
        } else {
            g.pos = np;
        }
        float av = length(g.angVel);
        if (av > 1e-3f) g.rot = qnormalize(qaxis(g.angVel / av, av * dt) * g.rot);
        bool slow = length(g.vel) < 12.0f;
        if (g.type == W_HE && g.time >= 1.6f) {
            explodeHE(g);
            g.active = false;
        } else if (g.type == W_FLASH && g.time >= 1.6f) {
            fx.flashbang(g.pos);
            emit(EV_FLASH, g.pos, g.owner, g.type);
            for (auto& p : players) {
                if (!p.alive) continue;
                vec3 eye = eyePos(p);
                TraceResult los = map->world.traceRay(g.pos, eye, MASK_SHOT);
                if (los.fraction < 1.0f) continue;
                vec3 to = g.pos - eye;
                float dist = length(to);
                float facing = dot(angleForward(p.pitch, p.yaw), to / std::max(dist, 1.0f));
                float k = facing > 0.5f ? 1.0f : facing > 0.0f ? 0.6f : 0.25f;
                k *= saturate(1.0f - dist / 2200.0f);
                if (k <= 0.05f) continue;
                float dur = 0.5f + 4.3f * k;
                p.flashUntil = std::max(p.flashUntil, time + dur);
                p.flashDur = dur;
            }
            g.active = false;
        } else if (g.type == W_SMOKE && ((slow && g.time > 0.6f) || g.time > 3.5f)) {
            fx.smoke(g.pos);
            emit(EV_SMOKE, g.pos, g.owner, g.type);
            g.active = false;
        }
    }
    grenades.erase(std::remove_if(grenades.begin(), grenades.end(), [](const Grenade& g) { return !g.active; }), grenades.end());
}

void Game::explodeHE(const Grenade& g) {
    fx.explosion(g.pos);
    emit(EV_EXPLOSION, g.pos, g.owner, W_HE);
    Player* owner = g.owner >= 0 && g.owner < (int)players.size() ? &players[g.owner] : nullptr;
    for (auto& p : players) {
        if (!p.alive) continue;
        vec3 c = p.mv.origin + vec3(0, 0, 36);
        float dist = length(c - g.pos);
        if (dist > 350.0f) continue;
        TraceResult los = map->world.traceRay(g.pos + vec3(0, 0, 4), c, MASK_SHOT);
        if (los.fraction < 1.0f) continue;
        float dmg = 98.0f * std::pow(1.0f - dist / 350.0f, 1.4f);
        if (owner && !isEnemy(*owner, p) && owner->id != p.id) continue;
        applyDamage(p, owner, dmg, HG_CHEST, W_HE, normalize(c - g.pos), false, c);
    }
}

void Game::updateBomb(float dt) {
    if (!bombPlanted || bombDefused || bombExploded) return;
    float left = bombExplodeAt - time;
    if (time >= nextBeep) {
        emit(EV_BOMB_BEEP, bombPos);
        float interval = clampf(left / 40.0f, 0.0f, 1.0f);
        nextBeep = time + 0.12f + interval * 0.88f;
    }
    if (left <= 0) {
        bombExploded = true;
        fx.explosion(bombPos);
        fx.explosion(bombPos + vec3(40, 0, 30));
        fx.explosion(bombPos + vec3(-30, 30, 60));
        emit(EV_BOMB_EXPLODED, bombPos);
        for (auto& p : players) {
            if (!p.alive) continue;
            float dist = length(p.mv.origin - bombPos);
            if (dist > 1750.0f) continue;
            float dmg = 500.0f * std::exp(-dist * dist / (2 * 580.0f * 580.0f));
            if (dmg > 1) applyDamage(p, nullptr, dmg, HG_CHEST, W_C4, normalize(p.mv.origin - bombPos), false, p.mv.origin);
        }
    }
}

int Game::siteAt(vec3 pos) const {
    for (size_t i = 0; i < map->sites.size(); i++)
        if (map->sites[i].box.contains(pos + vec3(0, 0, 8))) return (int)i;
    return -1;
}

void Game::applyDamage(Player& v, Player* att, float dmg, int group, int weapon, vec3 dir, bool wallbang, vec3 hitPos) {
    if (!v.alive || phase == PH_MATCH_END) return;
    if (att && att != &v && !isEnemy(*att, v)) return;
    if (cfg.mode == MODE_PRACTICE && !v.bot && att && att->bot) return;
    const WeaponDef& d = weaponDef(weapon);
    float mult = group == HG_HEAD ? d.headMult : group == HG_STOMACH ? 1.25f : group == HG_LEG ? 0.75f : 1.0f;
    float total = dmg * mult;
    bool armored = v.armor > 0 && group != HG_LEG && (group != HG_HEAD || v.helmet);
    float healthDmg = total;
    if (armored) {
        healthDmg = total * d.armorPen;
        float armorDmg = (total - healthDmg) * 0.5f;
        if (armorDmg > v.armor) {
            healthDmg += (armorDmg - v.armor) * 2.0f;
            armorDmg = (float)v.armor;
        }
        v.armor = std::max(0, v.armor - (int)armorDmg);
    }
    int hd = std::max(1, (int)healthDmg);
    int before = v.health;
    v.health -= hd;
    v.lastHurt = time;
    v.lastHurtFrom = hitPos - dir * 100.0f;
    v.lastAttacker = att ? att->id : -1;
    v.tagUntil = time + 0.4f;
    if (att && att != &v) att->damageDealt += std::min(hd, before);
    int evType = group == HG_HEAD ? (v.helmet && armored ? EV_HIT_HELMET : EV_HIT_HEAD) : EV_HIT_BODY;
    emit(evType, hitPos, v.id, weapon);
    if (att && att->id == local && att != &v) {
        hitMarkerTime = time;
        hitMarkerHead = group == HG_HEAD;
        emit(EV_LOCAL_HIT, hitPos, v.id, weapon, group == HG_HEAD ? 1.0f : 0.0f);
    }
    if (v.health <= 0) killPlayer(v, att, weapon, group == HG_HEAD, dir, wallbang, hitPos);
}

void Game::killPlayer(Player& v, Player* att, int weapon, bool hs, vec3 dir, bool wallbang, vec3 hitPos) {
    if (!v.alive) return;
    v.alive = false;
    v.health = 0;
    v.deaths++;
    v.deathTime = time;
    v.reloadStart = v.plantStart = v.defuseStart = -1;
    v.zoom = 0;
    if (att && att != &v) {
        if (isEnemy(*att, v)) {
            att->kills++;
            att->score += 2;
            att->money = std::min(16000, att->money + weaponDef(weapon).killReward);
        } else {
            att->kills--;
            att->money = std::max(0, att->money - 300);
        }
    }
    // Assist: whoever else damaged the victim recently is not tracked per-attacker; keep it simple.
    KillFeedEntry k;
    k.killer = att ? att->name : "";
    k.killerTeam = att ? att->team : 0;
    k.victim = v.name;
    k.victimTeam = v.team;
    k.weapon = weapon;
    k.headshot = hs;
    k.wallbang = wallbang;
    k.time = time;
    k.involvesLocal = v.id == local || (att && att->id == local);
    killfeed.push_back(k);
    emit(EV_KILL, hitPos, v.id, weapon, hs ? 1.0f : 0.0f);
    if (att && att->id == local && att != &v) emit(EV_LOCAL_KILL, hitPos, v.id, weapon, hs ? 1.0f : 0.0f);
    if (v.id == local) emit(EV_LOCAL_DEATH, hitPos, att ? att->id : -1, weapon);
    Xform world(v.mv.origin, qaxis({0, 0, 1}, v.yaw * kDeg));
    v.ragdoll.init(v.pose, world, v.mv.velocity, dir * (hs ? 240.0f : 170.0f) + vec3(0, 0, 40), hitPos);
    if (v.slots[SLOT_PRIMARY] != W_NONE) dropWeapon(v, SLOT_PRIMARY, dir);
    else if (v.slots[SLOT_SECONDARY] != W_NONE && v.slots[SLOT_SECONDARY] != W_GLOCK && v.slots[SLOT_SECONDARY] != W_USP) dropWeapon(v, SLOT_SECONDARY, dir);
    if (v.hasBomb) {
        WorldItem it;
        it.weapon = W_C4;
        it.pos = v.mv.origin + vec3(0, 0, 30);
        it.vel = dir * 80.0f + vec3(0, 0, 60);
        it.time = time;
        items.push_back(it);
        v.hasBomb = false;
        v.slots[SLOT_BOMB] = W_NONE;
    }
    if (cfg.mode != MODE_COMPETITIVE) v.respawnAt = time + 2.0f;
}

void Game::dropWeapon(Player& p, int slot, vec3 dir) {
    int w = p.slots[slot];
    if (w == W_NONE || slot == SLOT_KNIFE || slot == SLOT_GRENADE) return;
    WorldItem it;
    it.weapon = w;
    it.clip = p.clip[w];
    it.reserve = p.reserve[w];
    it.pos = eyePos(p) - vec3(0, 0, 14);
    it.vel = normalize(vec3(dir.x, dir.y, 0) + vec3(0, 0, 0.35f)) * 200.0f + p.mv.velocity * 0.5f;
    it.rot = qaxis({0, 0, 1}, p.yaw * kDeg);
    it.time = time;
    it.droppedBy = p.id;
    items.push_back(it);
    p.slots[slot] = W_NONE;
    if (items.size() > 24) items.erase(items.begin());
}

void Game::updateItems(float dt) {
    for (auto& it : items) {
        if (it.resting) continue;
        it.vel.z -= 800.0f * dt;
        vec3 np = it.pos + it.vel * dt;
        TraceResult tr = map->world.trace(it.pos, np, vec3(-6, -6, -1), vec3(6, 6, 1), MASK_SHOT);
        if (tr.fraction < 1.0f && !tr.startSolid) {
            it.pos = tr.endpos;
            float vn = dot(it.vel, tr.normal);
            it.vel = (it.vel - tr.normal * vn) * 0.5f - tr.normal * vn * 0.25f;
            if (tr.normal.z > 0.7f && length(it.vel) < 30.0f) it.resting = true;
        } else {
            it.pos = np;
        }
    }
}

void Game::updatePose(Player& p, float dt) {
    if (!p.alive) return;
    AgentAnimInput ai;
    ai.time = time;
    float cy = std::cos(p.yaw * kDeg), sy = std::sin(p.yaw * kDeg);
    vec3 v = p.mv.velocity;
    ai.velLocal = vec3(v.x * cy + v.y * sy, -v.x * sy + v.y * cy, 0);
    ai.onGround = p.mv.onGround;
    ai.duck = p.mv.duckAmount;
    ai.aimPitch = p.pitch;
    p.walkPhase += dt * horizSpeed(p.mv) / 36.0f;
    ai.walkPhase = p.walkPhase;
    if (weaponModels && p.active != W_NONE && !(p.throwStart >= 0 && p.thrown)) {
        ai.wm = &weaponModels[p.active];
        ai.weaponClass = weaponDef(p.active).cls;
    }
    ai.fireT = time - p.lastShot;
    ai.reloadT = p.reloadStart >= 0 ? time - p.reloadStart : -1;
    ai.reloadDur = weaponDef(p.active).reloadTime;
    ai.planting = p.plantStart >= 0 || p.defuseStart >= 0;
    animateAgent(ai, p.pose);
    Xform world(p.mv.origin, qaxis({0, 0, 1}, p.yaw * kDeg));
    p.numHitboxes = agentHitboxes(p.pose, world, p.hitboxes);
}

void Game::separatePlayers() {
    for (size_t i = 0; i < players.size(); i++)
        for (size_t j = i + 1; j < players.size(); j++) {
            Player& a = players[i];
            Player& b = players[j];
            if (!a.alive || !b.alive) continue;
            vec3 d = b.mv.origin - a.mv.origin;
            if (std::fabs(d.z) > 70.0f) continue;
            float dist = length(vec2(d.x, d.y));
            if (dist >= 30.0f) continue;
            vec3 n = dist > 0.01f ? vec3(d.x, d.y, 0) / dist : vec3(1, 0, 0);
            float push = (30.0f - dist) * 0.5f;
            TraceResult ta = map->world.trace(a.mv.origin, a.mv.origin - n * push, hullMins(), hullMaxs(a.mv.ducked), MASK_PLAYER);
            TraceResult tb = map->world.trace(b.mv.origin, b.mv.origin + n * push, hullMins(), hullMaxs(b.mv.ducked), MASK_PLAYER);
            if (!ta.startSolid) a.mv.origin = ta.endpos;
            if (!tb.startSolid) b.mv.origin = tb.endpos;
        }
}
