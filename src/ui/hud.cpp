// In-game HUD, buy menu, scoreboard and pause menu (CS2-inspired layout).
#include <cstdio>

#include "app.h"

namespace {

uint32_t teamColor(int team) { return team == TEAM_T ? colors::t : colors::ct; }

std::string timeStr(float secs) {
    int t = (int)std::ceil(std::max(0.0f, secs));
    char b[16];
    std::snprintf(b, sizeof(b), "%d:%02d", t / 60, t % 60);
    return b;
}

float g_dynGap = 0;  // smoothed dynamic crosshair gap, pixels
vec2 g_recoilOff(0, 0);  // smoothed follow-recoil offset, pixels

}  // namespace

void App::drawHud(float dt) {
    // HUD elements follow the HUD scale; crosshair, scope and scoreboard keep the menu scale `ms`.
    float ms = theme.s, s = ms * clampf(settings.hudScale, 0.8f, 1.2f), W = (float)ui.width, H = (float)ui.height;
    const CrosshairColor& hudCc = kHudColors[std::max(0, std::min(settings.hudColor, 4))];
    const uint32_t hudCol = rgba(hudCc.r, hudCc.g, hudCc.b);
    Player& me = game.localPlayer();
    const Player* view = &me;
    if (!me.alive && spectateTarget() >= 0) view = &game.players[spectateTarget()];
    bool competitive = game.cfg.mode == MODE_COMPETITIVE;
    float cx = W * 0.5f, cy = H * 0.5f;

    // AWP scope.
    if (me.alive && me.zoom > 0) {
        float r = H * 0.47f, R = W;
        uint32_t black = rgba(0, 0, 0, 255);
        ui.border(cx - r - R, cy - r - R, (r + R) * 2, (r + R) * 2, black, r + R, R);
        ui.border(cx - r, cy - r, r * 2, r * 2, rgba(0, 0, 0, 150), r, 26 * ms);
        ui.rect(0, cy - 0.5f, W, 1.2f, black);
        ui.rect(cx - 0.5f, 0, 1.2f, H, black);
        ui.rect(cx - r, cy - 1.5f * ms, r * 2, 3 * ms, rgba(0, 0, 0, 220));
        ui.rect(cx - 1.5f * ms, cy - r, 3 * ms, r * 2, rgba(0, 0, 0, 220));
    }
    if (view == &me && me.alive && me.zoom == 0 && !paused && !buyMenuOpen) {
        const WeaponDef& d = weaponDef(me.active);
        float pxPerTan = H * 0.5f / std::tan(lastCam.fovY * 0.5f);  // degrees -> pixels via tan(angle) * pxPerTan
        float gapTarget = 0;
        if (settings.chStyle == 1) {
            // Dynamic: widen by the movement, air and firing inaccuracy terms used by Game::fire.
            float speed = std::sqrt(me.mv.velocity.x * me.mv.velocity.x + me.mv.velocity.y * me.mv.velocity.y);
            float moveSpread = d.spreadMove > 0 ? d.spreadMove : 4.0f, airSpread = d.spreadAir > 0 ? d.spreadAir : 8.0f;
            float inacc = moveSpread * saturate(speed / std::max(1.0f, d.maxSpeed)) + (me.mv.onGround ? 0.0f : airSpread) + me.fireInacc;
            inacc += saturate(1.0f - (game.time - me.lastShot) / 0.2f);  // brief kick per shot
            gapTarget = std::min(std::tan(std::min(inacc, 60.0f) * kDeg) * pxPerTan * 0.5f, 70 * ms);
        }
        g_dynGap = damp(g_dynGap, gapTarget, 16.0f, dt);
        vec2 offTarget(0, 0);
        if (settings.chFollowRecoil) {
            // Next bullet (Game::fire aims at view + pattern offset) relative to the kicked camera (view + recoil).
            vec2 rel = recoilOffset(d.recoilPattern, (int)(me.sprayIndex + 0.5f)) * d.recoilScale - me.recoil;
            offTarget = vec2(std::tan(rel.x * kDeg), -std::tan(rel.y * kDeg)) * pxPerTan;
        }
        g_recoilOff = vec2(damp(g_recoilOff.x, offTarget.x, 25.0f, dt), damp(g_recoilOff.y, offTarget.y, 25.0f, dt));
        if (d.cls != WC_SNIPER) drawCrosshairShape(ui, settings, cx + g_recoilOff.x, cy + g_recoilOff.y, ms, g_dynGap);
        else ui.circle(cx, cy, 1.5f * ms, rgba(255, 255, 255, 200));
    }

    // Damage direction indicators.
    for (size_t i = 0; i < damageMarks.size();) {
        float age = game.time - damageMarks[i].time;
        if (age > 1.4f || !me.alive) { damageMarks.erase(damageMarks.begin() + (long)i); continue; }
        vec3 d = damageMarks[i].from - lastCam.pos;
        float ang = std::atan2(dot(d, -lastCam.left), dot(d, lastCam.fwd));
        float a = 1.0f - age / 1.4f;
        float rad = 150 * s;
        for (int k = -6; k < 6; k++) {
            float a0 = ang + k * 0.045f, a1 = ang + (k + 1) * 0.045f;
            ui.line(cx + std::sin(a0) * rad, cy - std::cos(a0) * rad, cx + std::sin(a1) * rad, cy - std::cos(a1) * rad, 6 * s,
                    rgba(230, 40, 30, (int)(200 * a * (1.0f - std::fabs(k + 0.5f) / 6.5f))));
        }
        i++;
    }

    // Health / armor.
    if (view->alive) {
        float x = 28 * s, y = H - 92 * s, w = 390 * s, h = 64 * s;
        ui.glass(x, y, w, h, rgba(10, 12, 16, 150), 8 * s);
        bool low = view->health <= 20;
        uint32_t hc = low ? rgba(240, 70, 60) : hudCol;
        ui.rect(x + 18 * s, y + 28 * s, 24 * s, 8 * s, hc, 1.5f * s);
        ui.rect(x + 26 * s, y + 20 * s, 8 * s, 24 * s, hc, 1.5f * s);
        char b[16];
        std::snprintf(b, sizeof(b), "%d", view->health);
        ui.textShadowed(fBold, b, x + 54 * s, y + 4 * s, 48 * s, hc);
        ui.rect(x + 130 * s, y + 42 * s, 60 * s, 4 * s, rgba(255, 255, 255, 40), 2 * s);
        ui.rect(x + 130 * s, y + 42 * s, 60 * s * saturate(view->health / 100.0f), 4 * s, hc, 2 * s);
        float ax = x + 214 * s;
        ui.rect(ax, y + 18 * s, 22 * s, 26 * s, rgba(255, 255, 255, 30), 6 * s);
        ui.border(ax, y + 18 * s, 22 * s, 26 * s, hudCol, 6 * s, 2 * s);
        if (view->helmet) ui.rect(ax + 5 * s, y + 10 * s, 12 * s, 6 * s, hudCol, 3 * s);
        std::snprintf(b, sizeof(b), "%d", view->armor);
        ui.textShadowed(fBold, b, ax + 34 * s, y + 4 * s, 48 * s, hudCol);
    }

    // Ammo and weapon.
    if (view->alive && view->active != W_NONE) {
        const WeaponDef& d = weaponDef(view->active);
        float w = 390 * s, h = 64 * s, x = W - 28 * s - w, y = H - 92 * s;
        drawIcon(view->active, W - 40 * s, y - 60 * s, 46 * s, withAlpha(hudCol, 0.82f), true, 200 * s);
        ui.glass(x, y, w, h, rgba(10, 12, 16, 150), 8 * s);
        if (d.magSize > 0 && d.cls != WC_GRENADE) {
            char b[32];
            bool inf = game.cfg.mode == MODE_PRACTICE && !view->bot;
            std::snprintf(b, sizeof(b), "%d", view->clip[view->active]);
            uint32_t cc = view->clip[view->active] <= d.magSize / 5 ? rgba(240, 90, 70) : hudCol;
            float rx = x + w - 30 * s;
            std::string res = inf ? "/ \xE2\x80\x94" : "/ " + std::to_string(view->reserve[view->active]);
            float rw = ui.textWidth(fRegular, res, 30 * s);
            ui.textShadowed(fRegular, res, rx, y + 18 * s, 30 * s, colors::dim, ALIGN_RIGHT);
            ui.textShadowed(fBold, b, rx - rw - 10 * s, y + 4 * s, 48 * s, cc, ALIGN_RIGHT);
            for (int i = 0; i < 3; i++) ui.rect(x + 24 * s + i * 11 * s, y + 18 * s, 6 * s, 28 * s, rgba(236, 200, 120, 220), 3 * s);
        } else {
            ui.textShadowed(fBold, d.name, x + w - 30 * s, y + 14 * s, 32 * s, hudCol, ALIGN_RIGHT);
        }
    }

    // Top center: timer and scores.
    {
        float tw = 132 * s, th = 52 * s, tx = cx - tw * 0.5f, ty = 16 * s;
        ui.glass(tx, ty, tw, th, rgba(10, 12, 16, 170), 6 * s);
        float left = game.roundTimeLeft();
        bool bomb = game.bombPlanted && !game.bombDefused && !game.bombExploded;
        uint32_t tc = (bomb || (left < 10 && game.phase == PH_LIVE)) ? rgba(240, 80, 70) : hudCol;
        if (bomb) {
            bool blink = std::fmod(game.time, 1.0f) < 0.5f;
            ui.rect(tx + 12 * s, ty + 14 * s, 26 * s, 24 * s, blink ? rgba(220, 50, 40) : rgba(120, 30, 25), 4 * s);
            ui.text(fBold, "C4", tx + 25 * s, ty + 17 * s, 16 * s, colors::text, ALIGN_CENTER);
            ui.textShadowed(fBold, timeStr(left), tx + tw * 0.5f + 16 * s, ty + 6 * s, 36 * s, tc, ALIGN_CENTER);
        } else {
            ui.textShadowed(fBold, timeStr(left), cx, ty + 6 * s, 36 * s, tc, ALIGN_CENTER);
        }
        if (competitive) {
            float bw = 78 * s;
            ui.rect(tx - bw - 6 * s, ty, bw, th, rgba(56, 86, 138, 220), 6 * s);
            ui.textShadowed(fBold, std::to_string(game.scoreCT), tx - bw * 0.5f - 6 * s, ty + 6 * s, 36 * s, colors::text, ALIGN_CENTER);
            ui.rect(tx + tw + 6 * s, ty, bw, th, rgba(150, 118, 50, 220), 6 * s);
            ui.textShadowed(fBold, std::to_string(game.scoreT), tx + tw + 6 * s + bw * 0.5f, ty + 6 * s, 36 * s, colors::text, ALIGN_CENTER);
            int ci = 0, ti = 0;
            for (auto& p : game.players) {
                bool isT = p.team == TEAM_T;
                int idx = isT ? ti++ : ci++;
                float px = isT ? tx + tw + bw + 18 * s + idx * 22 * s : tx - bw - 34 * s - idx * 22 * s;
                uint32_t col = p.alive ? teamColor(p.team) : rgba(60, 64, 72, 200);
                ui.rect(px, ty + 6 * s, 16 * s, 40 * s, col, 4 * s);
                if (p.id == game.local) ui.border(px - 2 * s, ty + 4 * s, 20 * s, 44 * s, colors::text, 5 * s, 1.5f * s);
                if (!p.alive) ui.line(px + 2 * s, ty + 10 * s, px + 14 * s, ty + 42 * s, 2 * s, rgba(200, 60, 50));
            }
            char rb[32];
            std::snprintf(rb, sizeof(rb), "РАУНД %d/%d", game.round, game.cfg.maxRounds);
            ui.text(fBold, rb, cx, ty + th + 6 * s, 15 * s, colors::dim, ALIGN_CENTER, 2 * s);
        } else {
            ui.text(fBold, game.cfg.mode == MODE_PRACTICE ? "ТРЕНИРОВКА" : "БОЙ НАСМЕРТЬ", cx, ty + th + 6 * s, 15 * s, colors::dim, ALIGN_CENTER, 2 * s);
            int leader = 0;
            for (auto& p : game.players)
                if (p.kills > game.players[leader].kills) leader = p.id;
            char kb[96];
            std::snprintf(kb, sizeof(kb), "Ваши убийства: %d   ·   Лидер: %s (%d)", me.kills, game.players[leader].name.c_str(), game.players[leader].kills);
            ui.textShadowed(fRegular, kb, cx, ty + th + 28 * s, 18 * s, colors::text, ALIGN_CENTER);
        }
    }

    // Radar.
    {
        float rs = 260 * s, rx = 28 * s, ry = 24 * s;
        ui.shadow(rx, ry, rs, rs, 12 * s, 16 * s, rgba(0, 0, 0, 120));
        ui.rect(rx, ry, rs, rs, rgba(8, 10, 14, 220), 12 * s);
        vec3 P = lastCam.pos;
        float viewYawRad = std::atan2(lastCam.fwd.y, lastCam.fwd.x);
        float yaw = settings.radarRotate ? viewYawRad : kPi * 0.5f;  // fixed radar: +Y (north) up
        vec3 f(std::cos(yaw), std::sin(yaw), 0), r(std::sin(yaw), -std::cos(yaw), 0);
        const float R = 1300.0f / clampf(settings.radarZoom, 0.6f, 1.6f);
        vec3 rc = radarCenter[gameMap];
        float rh = radarHalf[gameMap];
        auto toUV = [&](vec3 w) { return vec2((w.x - (rc.x - rh)) / (2 * rh), (w.y - (rc.y - rh)) / (2 * rh)); };
        vec2 uv[4] = {toUV(P + f * R - r * R), toUV(P + f * R + r * R), toUV(P - f * R + r * R), toUV(P - f * R - r * R)};
        ui.imageUV(radars[gameMap], rx, ry, rs, rs, uv, rgba(255, 255, 255, 235), 12 * s);
        ui.border(rx, ry, rs, rs, rgba(255, 255, 255, 40), 12 * s, 1.5f * s);
        auto toScreen = [&](vec3 w, vec2& out) {
            vec3 d = w - P;
            float sx = dot(d, r) / R, sy = -dot(d, f) / R;
            out = vec2(rx + rs * 0.5f + sx * rs * 0.5f, ry + rs * 0.5f + sy * rs * 0.5f);
            return std::fabs(sx) < 0.96f && std::fabs(sy) < 0.96f;
        };
        vec2 sp;
        for (auto& site : maps[gameMap]->sites) {
            if (toScreen(site.box.center(), sp)) ui.textShadowed(fTitle, std::string(1, site.letter), sp.x, sp.y - 12 * s, 24 * s, rgba(230, 200, 120, 200), ALIGN_CENTER);
        }
        for (auto& p : game.players) {
            if (!p.alive || p.id == view->id) continue;
            bool friendly = !game.isEnemy(me, p);
            if (!friendly && !spotted[p.id % 32]) continue;
            if (!toScreen(p.mv.origin, sp)) continue;
            uint32_t col = friendly ? teamColor(p.team) : rgba(235, 60, 50);
            ui.circle(sp.x, sp.y, 6 * s, rgba(0, 0, 0, 160));
            ui.circle(sp.x, sp.y, 4.5f * s, col);
        }
        if (game.bombPlanted && toScreen(game.bombPos, sp)) {
            if (std::fmod(game.time, 0.8f) < 0.5f) ui.rect(sp.x - 6 * s, sp.y - 5 * s, 12 * s, 10 * s, rgba(230, 60, 40), 2 * s);
        }
        for (auto& it : game.items)
            if (it.weapon == W_C4 && competitive && me.team == TEAM_T && toScreen(it.pos, sp)) ui.rect(sp.x - 6 * s, sp.y - 5 * s, 12 * s, 10 * s, rgba(240, 160, 40), 2 * s);
        vec2 c(rx + rs * 0.5f, ry + rs * 0.5f);
        vec3 vf(std::cos(viewYawRad), std::sin(viewYawRad), 0);
        vec2 dir(dot(vf, r), -dot(vf, f));
        ui.circle(c.x, c.y, 7 * s, rgba(0, 0, 0, 180));
        ui.circle(c.x, c.y, 5 * s, hudCol);
        ui.line(c.x, c.y, c.x + dir.x * 16 * s, c.y + dir.y * 16 * s, 3 * s, hudCol);
        if (competitive || view->money > 0) {
            char mb[32];
            std::snprintf(mb, sizeof(mb), "$ %d", view->money);
            if (competitive) ui.textShadowed(fBold, mb, rx + 4 * s, ry + rs + 10 * s, 30 * s, colors::greenHi);
        }
    }

    // Kill feed.
    {
        float y = 24 * s;
        for (auto it = game.killfeed.begin(); it != game.killfeed.end(); ++it) {
            float age = game.time - it->time;
            if (age > 7.0f) continue;
            float a = saturate((7.0f - age) / 0.5f);
            float fs = 19 * s, ih = 26 * s;
            float kw = it->killer.empty() ? 0 : ui.textWidth(fBold, it->killer, fs);
            float vw = ui.textWidth(fBold, it->victim, fs);
            float iw = std::min(ih * iconAspect[it->weapon], 110 * s);
            float extra = (it->headshot ? 30 * s : 0) + (it->wallbang ? 30 * s : 0);
            float w = kw + vw + iw + extra + 56 * s, h = 38 * s, x = W - 28 * s - w;
            ui.rect(x, y, w, h, rgba(10, 12, 16, (int)(170 * a)), 5 * s);
            if (it->involvesLocal) ui.border(x, y, w, h, rgba(220, 60, 50, (int)(230 * a)), 5 * s, 2 * s);
            float px = x + 14 * s;
            if (!it->killer.empty()) {
                ui.text(fBold, it->killer, px, y + 8 * s, fs, withAlpha(teamColor(it->killerTeam), a));
                px += kw + 12 * s;
            }
            drawIcon(it->weapon, px, y + 6 * s, ih, rgba(255, 255, 255, (int)(230 * a)), false, 110 * s);
            px += iw + 10 * s;
            if (it->wallbang) {
                ui.rect(px, y + 10 * s, 6 * s, 18 * s, rgba(255, 255, 255, (int)(200 * a)), 1 * s);
                ui.rect(px + 9 * s, y + 10 * s, 6 * s, 18 * s, rgba(255, 255, 255, (int)(120 * a)), 1 * s);
                px += 30 * s;
            }
            if (it->headshot) {
                ui.ring(px + 9 * s, y + 19 * s, 8 * s, 2 * s, rgba(255, 255, 255, (int)(230 * a)));
                ui.circle(px + 9 * s, y + 19 * s, 3 * s, rgba(255, 255, 255, (int)(230 * a)));
                px += 30 * s;
            }
            ui.text(fBold, it->victim, px, y + 8 * s, fs, withAlpha(teamColor(it->victimTeam), a));
            y += 44 * s;
        }
    }

    // Centre messages.
    if (competitive && game.phase == PH_FREEZE) {
        char b[64];
        std::snprintf(b, sizeof(b), "ЗАКУПКА  ·  %d", (int)std::ceil(game.phaseEnd - game.time));
        ui.textShadowed(fBold, b, cx, 118 * s, 28 * s, colors::text, ALIGN_CENTER, 2 * s);
        ui.textShadowed(fRegular, "Нажмите B, чтобы открыть меню закупки", cx, 154 * s, 19 * s, colors::dim, ALIGN_CENTER);
    } else if (game.canBuy(me) && competitive && game.phase == PH_LIVE && !buyMenuOpen) {
        ui.textShadowed(fRegular, "B — закупка", cx, 118 * s, 18 * s, colors::dim, ALIGN_CENTER);
    }
    if (game.bombPlanted && game.phase == PH_LIVE && !game.bombDefused)
        ui.textShadowed(fBold, game.roundMessage, cx, 140 * s, 22 * s, rgba(240, 90, 70), ALIGN_CENTER, 1.5f * s);
    if (me.alive && me.hasBomb && game.siteAt(me.mv.origin) >= 0 && me.plantStart < 0 && game.phase == PH_LIVE)
        ui.textShadowed(fBold, "Удерживайте E, чтобы заложить бомбу", cx, H * 0.66f, 24 * s, colors::accent, ALIGN_CENTER);
    if (me.alive && game.bombPlanted && me.team == TEAM_CT && me.defuseStart < 0 && length(me.mv.origin - game.bombPos) < 64 && !game.bombDefused)
        ui.textShadowed(fBold, "Удерживайте E, чтобы обезвредить", cx, H * 0.66f, 24 * s, colors::ct, ALIGN_CENTER);
    float prog = -1;
    const char* progLabel = "";
    if (me.plantStart >= 0) { prog = (game.time - me.plantStart) / 3.2f; progLabel = "УСТАНОВКА БОМБЫ"; }
    if (me.defuseStart >= 0) { prog = (game.time - me.defuseStart) / (me.kit ? 5.0f : 10.0f); progLabel = "ОБЕЗВРЕЖИВАНИЕ"; }
    if (prog >= 0) {
        float bw = 420 * s, bx = cx - bw * 0.5f, by = H * 0.7f;
        ui.textShadowed(fBold, progLabel, cx, by - 34 * s, 20 * s, colors::text, ALIGN_CENTER, 2 * s);
        ui.rect(bx, by, bw, 10 * s, rgba(0, 0, 0, 160), 5 * s);
        ui.rect(bx, by, bw * saturate(prog), 10 * s, colors::accent, 5 * s);
    }
    if (!me.alive && game.phase != PH_MATCH_END) {
        std::string killer;
        int kw = W_KNIFE;
        for (auto& k : game.killfeed)
            if (k.victim == me.name) { killer = k.killer; kw = k.weapon; }
        float bw = 520 * s, bh = 70 * s, bx = cx - bw * 0.5f, by = H - 170 * s;
        ui.glass(bx, by, bw, bh, rgba(40, 10, 10, 160), 8 * s);
        ui.text(fBold, killer.empty() ? "ВЫ ПОГИБЛИ" : "ВАС УБИЛ: " + killer, bx + 24 * s, by + 20 * s, 26 * s, colors::text);
        drawIcon(kw, bx + bw - 24 * s, by + 16 * s, 38 * s, colors::text, true, 150 * s);
        if (spectateTarget() >= 0) {
            ui.textShadowed(fBold, "НАБЛЮДЕНИЕ: " + game.players[spectateTarget()].name, cx, 110 * s, 24 * s, colors::text, ALIGN_CENTER, 1.5f * s);
            ui.textShadowed(fRegular, "ЛКМ — следующий игрок", cx, 142 * s, 17 * s, colors::dim, ALIGN_CENTER);
        }
        if (!competitive && me.respawnAt > 0) {
            char b[64];
            std::snprintf(b, sizeof(b), "Возрождение через %d", (int)std::ceil(std::max(0.0f, me.respawnAt - game.time)));
            ui.textShadowed(fBold, b, cx, H * 0.4f, 30 * s, colors::text, ALIGN_CENTER);
        }
    }
    if (game.time - killBannerTime < 0.35f && me.alive) {
        float a = 1.0f - (game.time - killBannerTime) / 0.35f;
        ui.line(cx - 14 * s, cy - 14 * s, cx - 6 * s, cy - 6 * s, 2.5f * s, rgba(240, 60, 50, (int)(255 * a)));
        ui.line(cx + 14 * s, cy - 14 * s, cx + 6 * s, cy - 6 * s, 2.5f * s, rgba(240, 60, 50, (int)(255 * a)));
        ui.line(cx - 14 * s, cy + 14 * s, cx - 6 * s, cy + 6 * s, 2.5f * s, rgba(240, 60, 50, (int)(255 * a)));
        ui.line(cx + 14 * s, cy + 14 * s, cx + 6 * s, cy + 6 * s, 2.5f * s, rgba(240, 60, 50, (int)(255 * a)));
    }

    // Round / match end banners.
    if (game.phase == PH_END || game.phase == PH_MATCH_END) {
        bool matchEnd = game.phase == PH_MATCH_END;
        int winner = matchEnd ? game.matchWinner : game.roundWinner;
        float bw = 980 * s, bh = matchEnd ? 150 * s : 110 * s, bx = cx - bw * 0.5f, by = H * 0.18f;
        ui.shadow(bx, by, bw, bh, 10 * s, 30 * s, rgba(0, 0, 0, 150));
        ui.glass(bx, by, bw, bh, rgba(10, 12, 16, 180), 10 * s);
        ui.rect(bx, by, bw, 5 * s, winner ? teamColor(winner) : colors::accent, 2 * s);
        std::string title = game.roundMessage;
        if (matchEnd && competitive) title = winner == me.team ? "ПОБЕДА" : winner == 0 ? "НИЧЬЯ" : "ПОРАЖЕНИЕ";
        ui.textShadowed(fTitle, title, cx, by + 22 * s, matchEnd ? 56 * s : 38 * s, colors::text, ALIGN_CENTER, 2 * s);
        char b[96];
        if (competitive) std::snprintf(b, sizeof(b), "Спецназ %d : %d Террористы", game.scoreCT, game.scoreT);
        else std::snprintf(b, sizeof(b), "%s", game.roundMessage.c_str());
        ui.text(fRegular, b, cx, by + bh - 40 * s, 20 * s, colors::dim, ALIGN_CENTER);
        if (matchEnd) drawScoreboard(cx - 560 * ms, by + bh + 24 * s, 1120 * ms);
    }
    if (win.input.down(GLFW_KEY_TAB) && game.phase != PH_MATCH_END && !paused) drawScoreboard(cx - 560 * ms, 170 * ms, 1120 * ms);
    if (settings.showFps) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.0f FPS", fps);
        ui.text(fRegular, b, W - 12 * s, H - 26 * s, 15 * s, rgba(160, 230, 120, 200), ALIGN_RIGHT);
    }
}

void App::drawScoreboard(float x, float y, float w) {
    float s = theme.s;
    bool competitive = game.cfg.mode == MODE_COMPETITIVE;
    Player& me = game.localPlayer();
    auto header = [&](float yy, const std::string& name, uint32_t col, int score) {
        ui.rect(x, yy, w, 44 * s, withAlpha(col, 0.35f), 6 * s);
        ui.text(fTitle, name, x + 20 * s, yy + 8 * s, 26 * s, colors::text, ALIGN_LEFT, 1.5f * s);
        if (score >= 0) ui.text(fTitle, std::to_string(score), x + w - 20 * s, yy + 6 * s, 30 * s, colors::text, ALIGN_RIGHT);
        const char* cols[] = {"ДЕНЬГИ", "У", "С", "MVP", "ПИНГ"};
        const float cx[] = {w - 470 * s, w - 330 * s, w - 260 * s, w - 190 * s, w - 90 * s};
        for (int i = 0; i < 5; i++) ui.text(fBold, cols[i], x + cx[i], yy + 50 * s, 15 * s, colors::dim, ALIGN_CENTER, 1.5f * s);
    };
    auto rowFn = [&](float yy, const Player& p) {
        bool isMe = p.id == me.id;
        ui.rect(x, yy, w, 40 * s, isMe ? rgba(222, 178, 84, 40) : rgba(255, 255, 255, 10), 4 * s);
        uint32_t tc = p.alive ? colors::text : colors::faint;
        ui.rect(x + 12 * s, yy + 8 * s, 6 * s, 24 * s, p.alive ? teamColor(p.team) : rgba(70, 70, 70), 2 * s);
        ui.text(fBold, p.name, x + 32 * s, yy + 8 * s, 21 * s, tc);
        char b[32];
        if (!competitive || p.team == me.team) {
            std::snprintf(b, sizeof(b), "$%d", p.money);
            ui.text(fRegular, b, x + w - 470 * s, yy + 9 * s, 19 * s, competitive ? colors::greenHi : colors::faint, ALIGN_CENTER);
        }
        ui.text(fBold, std::to_string(p.kills), x + w - 330 * s, yy + 9 * s, 20 * s, tc, ALIGN_CENTER);
        ui.text(fBold, std::to_string(p.deaths), x + w - 260 * s, yy + 9 * s, 20 * s, tc, ALIGN_CENTER);
        ui.text(fBold, p.mvps ? std::to_string(p.mvps) : "", x + w - 190 * s, yy + 9 * s, 20 * s, colors::accent, ALIGN_CENTER);
        ui.text(fRegular, p.bot ? "BOT" : "0", x + w - 90 * s, yy + 9 * s, 18 * s, colors::dim, ALIGN_CENTER);
    };
    float total = competitive ? (2 * (44 + 30) * s + game.players.size() * 44 * s + 40 * s) : ((44 + 30) * s + game.players.size() * 44 * s + 30 * s);
    ui.shadow(x - 20 * s, y - 20 * s, w + 40 * s, total + 20 * s, 12 * s, 30 * s, rgba(0, 0, 0, 150));
    ui.glass(x - 20 * s, y - 20 * s, w + 40 * s, total + 20 * s, rgba(10, 12, 16, 190), 12 * s);
    float yy = y;
    if (competitive) {
        for (int team : {TEAM_CT, TEAM_T}) {
            header(yy, team == TEAM_CT ? "СПЕЦНАЗ" : "ТЕРРОРИСТЫ", teamColor(team), team == TEAM_CT ? game.scoreCT : game.scoreT);
            yy += 74 * s;
            std::vector<const Player*> list;
            for (auto& p : game.players)
                if (p.team == team) list.push_back(&p);
            std::sort(list.begin(), list.end(), [](const Player* a, const Player* b) { return a->kills > b->kills; });
            for (auto* p : list) {
                rowFn(yy, *p);
                yy += 44 * s;
            }
            yy += 20 * s;
        }
    } else {
        header(yy, "ТАБЛИЦА", colors::accent, -1);
        yy += 74 * s;
        std::vector<const Player*> list;
        for (auto& p : game.players) list.push_back(&p);
        std::sort(list.begin(), list.end(), [](const Player* a, const Player* b) { return a->kills > b->kills; });
        for (auto* p : list) {
            rowFn(yy, *p);
            yy += 44 * s;
        }
    }
}

void App::drawBuyMenu() {
    float s = theme.s, W = (float)ui.width, H = (float)ui.height;
    Player& me = game.localPlayer();
    bool free = game.cfg.mode != MODE_COMPETITIVE;
    ui.rect(0, 0, W, H, rgba(0, 0, 0, 120));
    float pw = 1320 * s, ph = 700 * s, px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
    ui.shadow(px, py, pw, ph, 14 * s, 40 * s, rgba(0, 0, 0, 160));
    ui.glass(px, py, pw, ph, rgba(12, 14, 19, 200), 14 * s);
    ui.border(px, py, pw, ph, colors::line, 14 * s, 1);
    ui.text(fTitle, "ЗАКУПКА", px + 36 * s, py + 26 * s, 38 * s, colors::text, ALIGN_LEFT, 2 * s);
    char b[64];
    std::snprintf(b, sizeof(b), free ? "Бесплатно" : "$ %d", me.money);
    ui.text(fBold, b, px + pw - 36 * s, py + 24 * s, 38 * s, colors::greenHi, ALIGN_RIGHT);
    if (!free) {
        std::snprintf(b, sizeof(b), "Осталось времени: %d с", (int)std::ceil(game.buyTimeLeft()));
        ui.text(fRegular, b, px + pw - 36 * s, py + 72 * s, 18 * s, colors::dim, ALIGN_RIGHT);
    }
    int pistol = me.team == TEAM_T ? W_GLOCK : W_USP, rifle = me.team == TEAM_T ? W_AK47 : W_M4A4;
    struct Col {
        const char* title;
        std::vector<int> items;
    };
    std::vector<Col> cols = {{"ПИСТОЛЕТЫ", {pistol, W_DEAGLE}}, {"ВИНТОВКИ", {rifle, W_AWP}}, {"СНАРЯЖЕНИЕ", {BUY_KEVLAR, BUY_HELMET}}, {"ГРАНАТЫ", {W_HE, W_SMOKE, W_FLASH}}};
    if (me.team == TEAM_CT && !free) cols[2].items.push_back(BUY_KIT);
    int id = 700;
    for (size_t ci = 0; ci < cols.size(); ci++) {
        float cx = px + 36 * s + ci * 316 * s, cy = py + 120 * s, cw = 296 * s;
        sectionTitle(ui, theme, cx, cy, cols[ci].title);
        for (size_t ii = 0; ii < cols[ci].items.size(); ii++) {
            int item = cols[ci].items[ii];
            float x = cx, y = cy + 34 * s + ii * 138 * s, w = cw, h = 126 * s;
            int price = game.itemPrice(me, item);
            bool afford = me.money >= price || free;
            bool owned = item == BUY_KEVLAR ? me.armor >= 100 : item == BUY_HELMET ? (me.armor >= 100 && me.helmet) : item == BUY_KIT ? me.kit
                       : item < W_COUNT && weaponDef(item).cls == WC_GRENADE ? me.grenades[grenadeIndex(item)] > 0 : (item < W_COUNT && me.slots[weaponDef(item).slot] == item);
            bool hot = ui.hover(x, y, w, h);
            float& a = widgetAnim(id++);
            a = approach(a, hot ? 1.0f : 0.0f, theme.dt * 8);
            ui.rect(x, y, w, h, lerpColor(rgba(255, 255, 255, 12), rgba(255, 255, 255, 30), a), 8 * s);
            ui.border(x, y, w, h, owned ? colors::accent : rgba(255, 255, 255, (int)(26 + 60 * a)), 8 * s, owned ? 2 * s : 1 * s);
            std::string name;
            if (item == BUY_KEVLAR) name = "Кевлар";
            else if (item == BUY_HELMET) name = "Кевлар + шлем";
            else if (item == BUY_KIT) name = "Набор сапёра";
            else name = weaponDef(item).name;
            if (item < W_COUNT) {
                { float iw = std::min(60 * s * iconAspect[item], w - 40 * s); drawIcon(item, x + (w - iw) * 0.5f, y + 14 * s, 60 * s, withAlpha(colors::text, afford ? 0.95f : 0.35f), false, w - 40 * s); }
            } else {
                float ix = x + w * 0.5f - 18 * s;
                ui.rect(ix, y + 20 * s, 36 * s, 44 * s, rgba(255, 255, 255, 30), 10 * s);
                ui.border(ix, y + 20 * s, 36 * s, 44 * s, withAlpha(colors::text, afford ? 1.0f : 0.4f), 10 * s, 2.5f * s);
                if (item == BUY_HELMET) ui.rect(ix + 6 * s, y + 10 * s, 24 * s, 10 * s, colors::text, 5 * s);
                if (item == BUY_KIT) ui.text(fBold, "+", ix + 18 * s, y + 22 * s, 30 * s, colors::text, ALIGN_CENTER);
            }
            ui.text(fBold, name, x + 16 * s, y + h - 40 * s, 21 * s, afford ? colors::text : colors::faint);
            std::snprintf(b, sizeof(b), free ? "—" : "$%d", price);
            ui.text(fBold, owned ? "ЕСТЬ" : b, x + w - 16 * s, y + h - 40 * s, 21 * s, owned ? colors::accent : afford ? colors::greenHi : colors::red, ALIGN_RIGHT);
            if (hot && ui.mouseReleased) {
                if (game.buy(me, item)) ui.clickSound++;
            }
        }
    }
    ui.text(fRegular, "B или Esc — закрыть", px + pw * 0.5f, py + ph - 42 * s, 18 * s, colors::dim, ALIGN_CENTER);
}

void App::drawPauseMenu() {
    float s = theme.s, W = (float)ui.width, H = (float)ui.height;
    ui.rect(0, 0, W, H, rgba(0, 0, 0, 110));
    float px = 56 * s, py = 104 * s, pw = 420 * s, ph = 560 * s;
    ui.shadow(px, py, pw, ph, 12 * s, 30 * s, rgba(0, 0, 0, 150));
    ui.glass(px, py, pw, ph, rgba(12, 14, 19, 190), 12 * s);
    ui.border(px, py, pw, ph, colors::line, 12 * s, 1);
    ui.text(fTitle, "ПАУЗА", px + 28 * s, py + 22 * s, 38 * s, colors::text, ALIGN_LEFT, 2 * s);
    char b[128];
    const char* modes[] = {"Соревновательный", "Бой насмерть", "Тренировка"};
    std::snprintf(b, sizeof(b), "%s  ·  %s", mapList()[gameMap].title, modes[game.cfg.mode]);
    ui.text(fRegular, b, px + 28 * s, py + 76 * s, 19 * s, colors::dim);
    if (game.cfg.mode == MODE_COMPETITIVE) {
        std::snprintf(b, sizeof(b), "Раунд %d  ·  Спецназ %d : %d Террористы", game.round, game.scoreCT, game.scoreT);
        ui.text(fRegular, b, px + 28 * s, py + 104 * s, 19 * s, colors::dim);
    }
    float bx = px + 28 * s, bw = pw - 56 * s, bh = 62 * s;
    if (button(ui, theme, 800, bx, py + 160 * s, bw, bh, "ПРОДОЛЖИТЬ", 1)) paused = false;
    if (button(ui, theme, 801, bx, py + 240 * s, bw, bh, "НАСТРОЙКИ", 0, pauseSettings)) pauseSettings = !pauseSettings;
    if (button(ui, theme, 802, bx, py + 320 * s, bw, bh, "ВЫЙТИ В ЛОББИ", 0)) quitToLobby();
    if (button(ui, theme, 803, bx, py + 400 * s, bw, bh, "ВЫЙТИ ИЗ ИГРЫ", 0)) quit = true;
    if (shot.enabled && shot.tab == 2) pauseSettings = true;  // automation: --tab 2 opens settings, as in the lobby
    if (pauseSettings) {
        float sx = px + pw + 24 * s;
        drawSettingsPanel(sx, py, std::min(1380 * s, W - sx - 40 * s), std::min(900 * s, H - py - 40 * s));
    }
}
