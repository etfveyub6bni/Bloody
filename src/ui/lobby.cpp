// Main menu: play setup, arsenal viewer and settings.
#include <cstdio>
#include <cstdlib>

#include "app.h"

namespace {

const char* kModeNames[3] = {"СОРЕВНОВАТЕЛЬНЫЙ", "БОЙ НАСМЕРТЬ", "ТРЕНИРОВКА"};
const char* kModeDesc1[3] = {"5 на 5, бомба и экономика", "Все против всех", "Боты не стреляют"};
const char* kModeDesc2[3] = {"Раунды до победы команды", "Мгновенное возрождение", "Бесконечные патроны"};

void statBar(UI& ui, Theme& th, float x, float y, float w, const char* label, float v, const std::string& value) {
    float s = th.s;
    ui.text(*th.regular, label, x, y, 18 * s, colors::dim);
    ui.text(*th.bold, value, x + w, y, 18 * s, colors::text, ALIGN_RIGHT);
    ui.rect(x, y + 26 * s, w, 5 * s, rgba(255, 255, 255, 30), 2.5f * s);
    ui.rect(x, y + 26 * s, w * saturate(v), 5 * s, colors::accent, 2.5f * s);
}

}  // namespace

void App::drawLobby(float dt) {
    float s = theme.s, W = (float)ui.width, H = (float)ui.height;
    ui.rectGradH(0, 0, W * 0.62f, H, rgba(6, 8, 12, 238), rgba(6, 8, 12, 0));
    ui.rectGrad(0, H * 0.72f, W, H * 0.28f, rgba(0, 0, 0, 0), rgba(0, 0, 0, 150));

    float barH = 72 * s;
    ui.glass(0, 0, W, barH, rgba(10, 12, 17, 150), 0);
    ui.rect(0, barH, W, 1, colors::line);
    float lx = 44 * s;
    float w1 = ui.text(fTitle, "CS2", lx, 10 * s, 46 * s, colors::text, ALIGN_LEFT, 3 * s);
    ui.text(fBold, "PROTOTYPE", lx + w1 + 14 * s, 27 * s, 17 * s, colors::accent, ALIGN_LEFT, 5 * s);
    const char* tabs[3] = {"ИГРАТЬ", "АРСЕНАЛ", "НАСТРОЙКИ"};
    for (int i = 0; i < 3; i++)
        if (button(ui, theme, 100 + i, 440 * s + i * 190 * s, 0, 180 * s, barH, tabs[i], 2, lobbyTab == i)) lobbyTab = i;
    ui.circle(W - 176 * s, barH * 0.5f, 21 * s, rgba(222, 178, 84, 220));
    std::u32string nm = utf8to32(settings.name);
    std::string initial = settings.name.empty() ? "?" : settings.name.substr(0, (nm.size() && nm[0] > 127) ? 2 : 1);
    ui.text(fTitle, initial, W - 176 * s, barH * 0.5f - 14 * s, 24 * s, rgba(20, 20, 20), ALIGN_CENTER);
    ui.text(fBold, settings.name, W - 208 * s, 14 * s, 21 * s, colors::text, ALIGN_RIGHT);
    ui.text(fRegular, "Офлайн · боты", W - 208 * s, 40 * s, 15 * s, colors::dim, ALIGN_RIGHT);
    if (button(ui, theme, 110, W - 132 * s, 16 * s, 108 * s, 40 * s, "ВЫХОД", 0)) quit = true;

    if (lobbyTab == 0) {
        float px = 56 * s, py = 104 * s, pw = 800 * s, ph = 900 * s;
        ui.shadow(px, py, pw, ph, 12 * s, 30 * s, rgba(0, 0, 0, 120));
        ui.glass(px, py, pw, ph, rgba(12, 14, 19, 165), 12 * s);
        ui.border(px, py, pw, ph, colors::line, 12 * s, 1);
        float cx = px + 28 * s;
        ui.text(fTitle, "НОВАЯ ИГРА", cx, py + 22 * s, 34 * s, colors::text, ALIGN_LEFT, 2 * s);
        sectionTitle(ui, theme, cx, py + 84 * s, "РЕЖИМ");
        for (int i = 0; i < 3; i++) {
            float x = cx + i * 252 * s, y = py + 112 * s, w = 238 * s, h = 116 * s;
            bool sel = settings.mode == i, hot = ui.hover(x, y, w, h);
            float& a = widgetAnim(200 + i);
            a = approach(a, hot ? 1.0f : 0.0f, dt * 8);
            ui.rect(x, y, w, h, sel ? rgba(222, 178, 84, 38) : lerpColor(rgba(255, 255, 255, 10), rgba(255, 255, 255, 24), a), 8 * s);
            ui.border(x, y, w, h, sel ? colors::accent : rgba(255, 255, 255, (int)(24 + 40 * a)), 8 * s, sel ? 2 * s : 1 * s);
            ui.text(fBold, kModeNames[i], x + 16 * s, y + 16 * s, 21 * s, sel ? colors::text : lerpColor(colors::dim, colors::text, a), ALIGN_LEFT, 1.5f * s);
            ui.text(fRegular, kModeDesc1[i], x + 16 * s, y + 52 * s, 16 * s, colors::dim);
            ui.text(fRegular, kModeDesc2[i], x + 16 * s, y + 76 * s, 16 * s, colors::dim);
            if (hot && ui.mouseReleased) { settings.mode = i; ui.clickSound++; }
        }
        sectionTitle(ui, theme, cx, py + 256 * s, "КАРТА");
        for (size_t i = 0; i < mapList().size(); i++) {
            float x = cx + i * 376 * s, y = py + 284 * s, w = 360 * s, h = 205 * s;
            bool sel = settings.map == (int)i, hot = ui.hover(x, y, w, h);
            float& a = widgetAnim(220 + (int)i);
            a = approach(a, hot ? 1.0f : 0.0f, dt * 8);
            if (thumbs[i]) ui.image(thumbs[i], x, y, w, h, 0xFFFFFFFFu, vec2(0.04f * a, 1.0f - 0.04f * a), vec2(1.0f - 0.04f * a, 0.04f * a), 8 * s);
            ui.rectGrad(x, y + h * 0.45f, w, h * 0.55f, rgba(0, 0, 0, 0), rgba(0, 0, 0, 200), 0);
            ui.border(x, y, w, h, sel ? colors::accent : rgba(255, 255, 255, (int)(30 + 60 * a)), 8 * s, sel ? 2.5f * s : 1 * s);
            ui.text(fTitle, mapList()[i].title, x + 16 * s, y + h - 70 * s, 28 * s, colors::text, ALIGN_LEFT, 1 * s);
            ui.text(fRegular, mapList()[i].subtitle, x + 16 * s, y + h - 34 * s, 16 * s, colors::dim);
            if (hot && ui.mouseReleased) { settings.map = (int)i; ui.clickSound++; }
        }
        sectionTitle(ui, theme, cx, py + 520 * s, "ПАРАМЕТРЫ");
        float fx = cx, fy = py + 552 * s, fw = pw - 56 * s;
        int teamIdx = settings.team == 1 ? 1 : 0;
        if (selector(ui, theme, 300, fx, fy, fw, "Команда", teamIdx, {"Спецназ", "Террористы"})) settings.team = teamIdx == 1 ? 1 : 2;
        selector(ui, theme, 301, fx, fy + 50 * s, fw, "Боты", settings.difficulty, {"Лёгкие", "Обычные", "Сложные", "Эксперт"});
        int ts = settings.teamSize - 1;
        if (selector(ui, theme, 302, fx, fy + 100 * s, fw, settings.mode == 0 ? "Игроков в команде" : "Участников (x2)", ts, {"1", "2", "3", "4", "5"})) settings.teamSize = ts + 1;
        if (settings.mode == 0) selector(ui, theme, 303, fx, fy + 150 * s, fw, "Длина матча", settings.longMatch, {"Короткий (до 9)", "Длинный (до 13)"});
        float gy = py + ph - 106 * s;
        ui.rect(px + 20 * s, gy - 18 * s, pw - 40 * s, 1, colors::line);
        std::string summary = std::string(mapList()[settings.map].title) + "  ·  " + kModeNames[settings.mode];
        ui.text(fBold, summary, cx, gy + 6 * s, 24 * s, colors::text);
        char info[128];
        if (settings.mode == 0) std::snprintf(info, sizeof(info), "%d на %d  ·  %s", settings.teamSize, settings.teamSize, settings.team == 1 ? "за террористов" : "за спецназ");
        else std::snprintf(info, sizeof(info), "%d участников  ·  8 минут", settings.teamSize * 2);
        ui.text(fRegular, info, cx, gy + 42 * s, 18 * s, colors::dim);
        if (button(ui, theme, 399, px + pw - 320 * s, gy, 292 * s, 76 * s, "НАЧАТЬ", 1)) startMatch();
    } else if (lobbyTab == 1) {
        float px = 56 * s, py = 104 * s, pw = 540 * s, ph = 900 * s;
        ui.glass(px, py, pw, ph, rgba(12, 14, 19, 165), 12 * s);
        ui.border(px, py, pw, ph, colors::line, 12 * s, 1);
        ui.text(fTitle, "АРСЕНАЛ", px + 28 * s, py + 22 * s, 34 * s, colors::text, ALIGN_LEFT, 2 * s);
        const int list[] = {W_AK47, W_M4A4, W_AWP, W_DEAGLE, W_USP, W_GLOCK, W_KNIFE, W_HE, W_SMOKE, W_FLASH, W_C4};
        for (int i = 0; i < 11; i++) {
            int w = list[i];
            float x = px + 16 * s, y = py + 84 * s + i * 72 * s, rw = pw - 32 * s, rh = 64 * s;
            bool sel = arsenalSel == w, hot = ui.hover(x, y, rw, rh);
            float& a = widgetAnim(400 + i);
            a = approach(a, hot ? 1.0f : 0.0f, dt * 8);
            ui.rect(x, y, rw, rh, sel ? rgba(222, 178, 84, 40) : lerpColor(rgba(255, 255, 255, 0), rgba(255, 255, 255, 18), a), 6 * s);
            if (sel) ui.rect(x, y + 8 * s, 3 * s, rh - 16 * s, colors::accent, 1.5f * s);
            drawIcon(w, x + 18 * s, y + 12 * s, 40 * s, withAlpha(colors::text, sel ? 1.0f : 0.8f), false, 170 * s);
            ui.text(fBold, weaponDef(w).name, x + 210 * s, y + 10 * s, 22 * s, sel ? colors::text : colors::dim);
            char price[32];
            if (weaponDef(w).price > 0) std::snprintf(price, sizeof(price), "$%d", weaponDef(w).price);
            else std::snprintf(price, sizeof(price), "стандартное");
            ui.text(fRegular, price, x + 210 * s, y + 36 * s, 16 * s, colors::green);
            if (hot && ui.mouseReleased) { arsenalSel = w; ui.clickSound++; }
        }
        const WeaponDef& d = weaponDef(arsenalSel);
        float sx = W - 540 * s, sy = H - 420 * s, sw = 480 * s, sh = 370 * s;
        ui.glass(sx, sy, sw, sh, rgba(12, 14, 19, 170), 12 * s);
        ui.border(sx, sy, sw, sh, colors::line, 12 * s, 1);
        ui.text(fTitle, d.name, sx + 28 * s, sy + 20 * s, 34 * s, colors::text, ALIGN_LEFT, 1.5f * s);
        const char* cls[] = {"Холодное оружие", "Пистолет", "Штурмовая винтовка", "Снайперская винтовка", "Граната", "Взрывчатка"};
        ui.text(fRegular, cls[d.cls], sx + 28 * s, sy + 66 * s, 18 * s, colors::accent);
        float bx = sx + 28 * s, bw = sw - 56 * s, by = sy + 110 * s;
        char buf[32];
        if (d.cls == WC_GRENADE || d.cls == WC_BOMB) {
            ui.text(fRegular, d.cls == WC_BOMB ? "Закладывается на точках A и B (E / ЛКМ)." : "Бросок — ЛКМ. Отскакивает от стен.", bx, by, 18 * s, colors::dim);
            ui.text(fRegular, d.cls == WC_BOMB ? "Взрыв через 40 секунд, обезвреживание 10 с." : "HE: урон по площади, дым: завеса 18 с.", bx, by + 30 * s, 18 * s, colors::dim);
        } else {
            std::snprintf(buf, sizeof(buf), "%.0f", d.damage);
            statBar(ui, theme, bx, by, bw, "Урон", d.damage / 115.0f, buf);
            float rpm = d.cycleTime > 0 ? 60.0f / d.cycleTime : 0;
            std::snprintf(buf, sizeof(buf), "%.0f в мин.", rpm);
            statBar(ui, theme, bx, by + 48 * s, bw, "Скорострельность", rpm / 700.0f, buf);
            std::snprintf(buf, sizeof(buf), "%.0f%%", d.armorPen * 100);
            statBar(ui, theme, bx, by + 96 * s, bw, "Бронепробиваемость", d.armorPen, buf);
            std::snprintf(buf, sizeof(buf), "%.0f", d.maxSpeed);
            statBar(ui, theme, bx, by + 144 * s, bw, "Мобильность", d.maxSpeed / 250.0f, buf);
            if (d.magSize > 0) {
                std::snprintf(buf, sizeof(buf), "%d / %d", d.magSize, d.reserve);
                statBar(ui, theme, bx, by + 192 * s, bw, "Магазин", d.magSize / 30.0f, buf);
            }
        }
    } else {
        drawSettingsPanel(56 * s, 104 * s, 1340 * s, 900 * s);
    }
    char foot[96];
    std::snprintf(foot, sizeof(foot), "C++ / OpenGL 3.3  ·  %.0f FPS", fps);
    ui.text(fRegular, foot, W - 28 * s, H - 40 * s, 16 * s, colors::faint, ALIGN_RIGHT);
}

namespace {

// ---- Settings panel (shared by the lobby and the pause menu) ----

constexpr int kTabCount = 6;
const char* kTabNames[kTabCount] = {"ИГРА", "ВИДЕО", "ЗВУК", "ИНТЕРФЕЙС", "ПРИЦЕЛ", "УПРАВЛЕНИЕ"};
const char* kPerfNames[5] = {"нет", "низкая", "средняя", "высокая", "очень высокая"};

struct Help {
    int id = 0;
    std::string title, text;
    int perf = -1;  // GPU cost 0..4 for video options, -1 when not applicable
};

struct PanelState {
    float scroll[kTabCount] = {}, target[kTabCount] = {}, content[kTabCount] = {};
    Help pinned;  // last hovered row, kept while the mouse is over the description column
    int shownId = -1000;
    float fade = 1, time = 0;
    int lastTab = -1;
};
PanelState g_panel;

float snap(float v, float step) {
    float r = std::round(v / step) * step;
    return r == 0.0f ? 0.0f : r;  // no "-0.0"
}

uint32_t perfColor(int level) {
    const uint32_t c[5] = {colors::dim, colors::greenHi, rgba(226, 200, 90), rgba(236, 150, 70), colors::red};
    return c[std::max(0, std::min(level, 4))];
}

void resetTab(Settings& st, int tab) {
    const Settings d;
    switch (tab) {
        case 0:
            st.sensitivity = d.sensitivity;
            st.zoomSensitivity = d.zoomSensitivity;
            st.invertY = d.invertY;
            st.rawInput = d.rawInput;
            st.applyViewmodelPreset(d.vmPreset);
            st.vmBob = d.vmBob;
            break;
        case 1:  // everything except the window mode
            st.applyQualityPreset(d.quality);
            st.renderScale = d.renderScale;
            st.brightness = d.brightness;
            st.saturation = d.saturation;
            st.sharpen = d.sharpen;
            st.filmGrain = d.filmGrain;
            st.chromatic = d.chromatic;
            st.vignette = d.vignette;
            st.vsync = d.vsync;
            st.fpsMax = d.fpsMax;
            break;
        case 2:
            st.volume = d.volume;
            st.volumeWeapons = d.volumeWeapons;
            st.volumeWorld = d.volumeWorld;
            st.volumeUi = d.volumeUi;
            st.volumeMusic = d.volumeMusic;
            st.spatialAudio = d.spatialAudio;
            break;
        case 3:
            st.showFps = d.showFps;
            st.hudScale = d.hudScale;
            st.hudColor = d.hudColor;
            st.radarZoom = d.radarZoom;
            st.radarRotate = d.radarRotate;
            break;
        case 4:
            st.chStyle = d.chStyle;
            st.chTStyle = d.chTStyle;
            st.chFollowRecoil = d.chFollowRecoil;
            st.chSize = d.chSize;
            st.chGap = d.chGap;
            st.chThickness = d.chThickness;
            st.chOutline = d.chOutline;
            st.chDot = d.chDot;
            st.chColor = d.chColor;
            st.chAlpha = d.chAlpha;
            break;
        default: break;
    }
}

// Key caps ('|' separates caps), right-aligned at xr and vertically centred on cy.
void keyCaps(App& app, const std::string& keys, float xr, float cy) {
    UI& ui = app.ui;
    float s = app.theme.s, fs = 17 * s, ch = 30 * s;
    std::vector<std::string> caps;
    size_t p = 0;
    while (p <= keys.size()) {
        size_t q = keys.find('|', p);
        if (q == std::string::npos) q = keys.size();
        caps.push_back(keys.substr(p, q - p));
        p = q + 1;
    }
    float x = xr;
    for (size_t i = caps.size(); i-- > 0;) {
        float cw = std::max(ch, ui.textWidth(app.fBold, caps[i], fs) + 20 * s);
        x -= cw;
        ui.rect(x, cy - ch * 0.5f, cw, ch, rgba(255, 255, 255, 16), 5 * s);
        ui.border(x, cy - ch * 0.5f, cw, ch, rgba(255, 255, 255, 60), 5 * s, 1.2f * s);
        ui.rect(x + 3 * s, cy + ch * 0.5f - 3 * s, cw - 6 * s, 2 * s, rgba(0, 0, 0, 70), 1 * s);
        ui.text(app.fBold, caps[i], x + cw * 0.5f, cy - fs * 0.52f, fs, colors::text, ALIGN_CENTER);
        x -= 8 * s;
    }
}

// "Нагрузка на видеокарту: высокая" with a 4-segment meter. Returns the height used.
float perfMeter(App& app, float x, float y, int level, float alpha) {
    UI& ui = app.ui;
    float s = app.theme.s;
    level = std::max(0, std::min(level, 4));
    float w = ui.text(app.fRegular, "Нагрузка на видеокарту:", x, y, 18 * s, withAlpha(colors::dim, alpha));
    ui.text(app.fBold, kPerfNames[level], x + w + 7 * s, y, 18 * s, withAlpha(perfColor(level), alpha));
    for (int i = 0; i < 4; i++)
        ui.rect(x + i * 44 * s, y + 30 * s, 38 * s, 6 * s, i < level ? withAlpha(perfColor(level), alpha) : rgba(255, 255, 255, (int)(34 * alpha)), 3 * s);
    return 46 * s;
}

// Crosshair on a sky/ground/crate backdrop; simulates a spray when the dynamic style or recoil follow is on.
float crosshairPreview(App& app, float x, float y, float w, float t) {
    UI& ui = app.ui;
    const Settings& st = app.settings;
    float s = app.theme.s, h = 240 * s, r = 8 * s, hz = y + h * 0.55f;
    ui.rectGrad(x, y, w, h * 0.55f + r, rgba(120, 160, 205), rgba(200, 210, 220), r);
    ui.rectGrad(x, hz, w, h * 0.45f, rgba(196, 162, 112), rgba(150, 120, 80), r);
    ui.rect(x + w * 0.64f, y + h * 0.3f, w * 0.22f, h * 0.46f, rgba(92, 64, 38));
    ui.rect(x + w * 0.64f, y + h * 0.3f, w * 0.22f, 4 * s, rgba(120, 88, 54));
    ui.border(x, y, w, h, colors::line, r, 1);
    bool dyn = st.chStyle == 1, rec = st.chFollowRecoil;
    float ox = 0, oy = 0, gap = 0;
    if (dyn || rec) {
        const float period = 2.6f, sprayT = 1.1f;
        float ph = std::fmod(t, period);
        float prog = std::min(ph, sprayT) / sprayT;
        float k = ph < sprayT ? 1.0f : std::exp(-(ph - sprayT) * 5.0f);
        if (rec) {
            oy = -prog * 40 * s * k;
            ox = std::sin(prog * 5.5f) * 12 * s * prog * k;
        }
        if (dyn) gap = (ph < sprayT ? std::min(1.0f, ph * 4.0f) : k) * 18 * s;
        if (ph < sprayT && std::fmod(ph, 0.1f) < 0.04f) ui.circle(x + w - 20 * s, y + 20 * s, 6 * s, rgba(255, 200, 90, 230));
    }
    drawCrosshairShape(ui, st, x + w * 0.5f + ox, y + h * 0.5f + oy, s * 1.5f, gap);
    const char* cap = dyn && rec ? "Имитация стрельбы: разброс и отдача" : dyn ? "Имитация стрельбы: разброс" : rec ? "Имитация стрельбы: отдача" : "Предпросмотр";
    ui.text(app.fRegular, cap, x + w * 0.5f, y + h + 10 * s, 16 * s, colors::dim, ALIGN_CENTER);
    return h + 36 * s;
}

// Miniature HUD in the chosen colour and scale, with a live radar crop of the current map.
float hudPreview(App& app, float x, float y, float w, float t) {
    UI& ui = app.ui;
    const Settings& st = app.settings;
    float s = app.theme.s, h = 240 * s, r = 8 * s;
    ui.rectGrad(x, y, w, h, rgba(70, 82, 98), rgba(34, 38, 46), r);
    ui.border(x, y, w, h, colors::line, r, 1);
    const CrosshairColor& hcol = kHudColors[std::max(0, std::min(st.hudColor, 4))];
    uint32_t hc = rgba(hcol.r, hcol.g, hcol.b);
    float hs = clampf(st.hudScale, 0.8f, 1.2f), k = s * hs * 0.6f, rk = s * hs * 0.48f;
    // Radar.
    int mi = app.state == App::ST_GAME ? app.gameMap : app.settings.map;
    float rs = 260 * rk, rx = x + 12 * s, ry = y + 12 * s;
    ui.rect(rx, ry, rs, rs, rgba(8, 10, 14, 220), 8 * rk);
    if (mi >= 0 && mi < (int)app.radars.size() && app.radars[mi]) {
        vec3 P = app.radarCenter[mi];
        float rh = app.radarHalf[mi], R = 1300.0f / clampf(st.radarZoom, 0.6f, 1.6f);
        float yaw = st.radarRotate ? t * 0.5f : kPi * 0.5f;
        vec3 f(std::cos(yaw), std::sin(yaw), 0), rt(std::sin(yaw), -std::cos(yaw), 0);
        auto toUV = [&](vec3 wp) { return vec2((wp.x - (P.x - rh)) / (2 * rh), (wp.y - (P.y - rh)) / (2 * rh)); };
        vec2 uv[4] = {toUV(P + f * R - rt * R), toUV(P + f * R + rt * R), toUV(P - f * R + rt * R), toUV(P - f * R - rt * R)};
        ui.imageUV(app.radars[mi], rx, ry, rs, rs, uv, rgba(255, 255, 255, 235), 8 * rk);
    }
    ui.border(rx, ry, rs, rs, rgba(255, 255, 255, 40), 8 * rk, 1.2f * s);
    float ccx = rx + rs * 0.5f, ccy = ry + rs * 0.5f;
    vec2 dir = st.radarRotate ? vec2(0, -1) : vec2(std::cos(t * 0.5f), -std::sin(t * 0.5f));
    ui.circle(ccx, ccy, 6 * rk, rgba(0, 0, 0, 180));
    ui.circle(ccx, ccy, 4.5f * rk, hc);
    ui.line(ccx, ccy, ccx + dir.x * 16 * rk, ccy + dir.y * 16 * rk, 3 * rk, hc);
    // Health and ammo boxes.
    float bh = 64 * k, bw = 250 * k, by = y + h - 12 * s - bh;
    ui.rect(x + 12 * s, by, bw, bh, rgba(10, 12, 16, 170), 8 * k);
    ui.rect(x + 12 * s + 18 * k, by + 28 * k, 24 * k, 8 * k, hc, 1.5f * k);
    ui.rect(x + 12 * s + 26 * k, by + 20 * k, 8 * k, 24 * k, hc, 1.5f * k);
    ui.textShadowed(app.fBold, "100", x + 12 * s + 54 * k, by + 4 * k, 48 * k, hc);
    ui.rect(x + 12 * s + 150 * k, by + 42 * k, 70 * k, 4 * k, hc, 2 * k);
    float ax = x + w - 12 * s - bw;
    ui.rect(ax, by, bw, bh, rgba(10, 12, 16, 170), 8 * k);
    float rw = ui.textWidth(app.fRegular, "/ 90", 30 * k);
    ui.textShadowed(app.fRegular, "/ 90", ax + bw - 20 * k, by + 18 * k, 30 * k, colors::dim, ALIGN_RIGHT);
    ui.textShadowed(app.fBold, "30", ax + bw - 30 * k - rw, by + 4 * k, 48 * k, hc, ALIGN_RIGHT);
    for (int i = 0; i < 3; i++) ui.rect(ax + 20 * k + i * 11 * k, by + 18 * k, 6 * k, 28 * k, rgba(236, 200, 120, 220), 3 * k);
    if (st.showFps) ui.text(app.fRegular, "144 FPS", x + w - 12 * s, y + 10 * s, 15 * s, rgba(160, 230, 120, 220), ALIGN_RIGHT);
    return h + 24 * s;
}

struct Bind {
    const char* keys;
    const char* action;
    const char* desc;
};
struct BindGroup {
    const char* title;
    std::vector<Bind> binds;
};
const std::vector<BindGroup>& bindGroups() {
    static const std::vector<BindGroup> g = {
        {"ДВИЖЕНИЕ",
         {{"W|A|S|D", "Движение", "Вперёд, влево, назад и вправо. На бегу разброс оружия резко растёт — перед выстрелом остановитесь."},
          {"Пробел", "Прыжок",
           "В воздухе можно стрейфить: поворачивайте мышь, зажимая A или D. Прыжок с приседом (Пробел, затем Ctrl) позволяет забраться выше."},
          {"Ctrl", "Присесть", "Уменьшает силуэт и разброс оружия. Шаги в приседе не слышны противникам."},
          {"Shift", "Шаг", "Тихая ходьба: противники не слышат шагов, но скорость ниже. Используйте, чтобы подкрасться."}}},
        {"БОЙ",
         {{"ЛКМ", "Огонь / удар ножом",
           "Зажмите для автоматического огня. Короткие очереди точнее длинных. После смерти — переключает наблюдение на следующего игрока."},
          {"ПКМ", "Прицел AWP / сильный удар", "У AWP переключает кратность оптического прицела, у ножа — медленный, но мощный удар."},
          {"R", "Перезарядка", "Перезаряжает оружие. Во время перезарядки стрелять нельзя — перезаряжайтесь в укрытии."},
          {"F", "Осмотр оружия", "Анимация осмотра оружия в руках. Прерывается выстрелом или сменой оружия."},
          {"G", "Выбросить оружие", "Бросает текущее оружие или бомбу перед собой — например, чтобы отдать союзнику. Нож и гранаты выбросить нельзя."}}},
        {"ОРУЖИЕ",
         {{"1|2|3|4|5", "Выбор слота", "1 — основное оружие, 2 — пистолет, 3 — нож, 4 — гранаты, 5 — бомба C4."},
          {"Колесо мыши", "Следующее / предыдущее", "Перебирает доступное оружие по кругу."},
          {"Q", "Последнее оружие", "Быстро переключает на предыдущее оружие — например, с AWP на пистолет и обратно."}}},
        {"ПРОЧЕЕ",
         {{"E", "Бомба",
           "Удерживайте на точке A или B, чтобы заложить C4. Спецназ удерживает E у бомбы, чтобы обезвредить её (с набором сапёра — вдвое быстрее)."},
          {"B", "Меню закупки",
           "Открывает меню закупки. В соревновательном режиме — в зоне закупки, пока идёт время закупки; в остальных режимах — в любой момент и "
           "бесплатно."},
          {"Tab", "Таблица счёта", "Показывает счёт, убийства, смерти и деньги игроков, пока клавиша зажата."},
          {"Esc", "Пауза / закрыть меню", "Открывает меню паузы с настройками или закрывает меню закупки."}}},
    };
    return g;
}

const char* tabTitle(int tab) {
    const char* t[kTabCount] = {"Мышь и оружие", "Графика", "Звук", "Интерфейс", "Прицел", "Управление"};
    return t[tab];
}

const char* tabText(int tab) {
    switch (tab) {
        case 0:
            return "Чувствительность мыши и положение оружия в кадре.\n\nНаведите курсор на любой параметр — здесь появится его подробное описание. "
                   "Все изменения применяются сразу.\n\nСовет: подберите чувствительность так, чтобы разворот на 180° занимал одно уверенное "
                   "движение руки по коврику.";
        case 1:
            return "Начните с пресета «Качество графики»: он настраивает все параметры группы «Качество». Если изменить любой из них вручную, пресет "
                   "станет «Своё».\n\nПараметры групп «Изображение» и «Экран» пресет не меняет. Если игре не хватает FPS, в первую очередь уменьшите "
                   "масштаб рендеринга, тени и затенение.";
        case 2:
            return "Громкость каждой группы звуков — это доля от общей громкости.\n\nЧтобы лучше слышать шаги и направление выстрелов, играйте в "
                   "наушниках с включённым объёмным звуком.";
        case 3: return "Размер и цвет HUD, настройки радара. Пример сверху сразу показывает изменения.";
        case 4:
            return "Классический прицел CS2 из четырёх линий. Предпросмотр сверху показывает его на светлом и тёмном фоне, а при динамическом стиле "
                   "или слежении за отдачей — имитацию стрельбы.";
        default: return "Раскладка клавиш как в CS2. Переназначение клавиш пока не поддерживается.\n\nНаведите курсор на действие, чтобы узнать подробности.";
    }
}

}  // namespace

void App::drawSettingsPanel(float x, float y, float w, float h) {
    float s = theme.s, dt = theme.dt;
    PanelState& P = g_panel;
    P.time += dt;
    int tab = std::max(0, std::min(settingsTab, kTabCount - 1));
    settingsTab = tab;
    if (tab != P.lastTab) {
        P.pinned = Help();
        P.lastTab = tab;
    }
    ui.shadow(x, y, w, h, 12 * s, 30 * s, rgba(0, 0, 0, 120));
    ui.glass(x, y, w, h, rgba(12, 14, 19, 175), 12 * s);
    ui.border(x, y, w, h, colors::line, 12 * s, 1);

    float pad = 28 * s;
    float descW = clampf(w * 0.36f, 380 * s, 500 * s);
    float lx = x + pad, lw = w - pad * 3 - descW;
    float dx = lx + lw + pad, dy = y + 22 * s, dw = descW, dh = h - 22 * s - pad;
    ui.text(fTitle, "НАСТРОЙКИ", lx, y + 22 * s, 34 * s, colors::text, ALIGN_LEFT, 2 * s);

    Help hovered;
    float tx = lx;
    for (int i = 0; i < kTabCount; i++) {
        float tw = std::max(100 * s, ui.textWidth(fBold, kTabNames[i], 18.4f * s, 1.2f * s) + 36 * s);
        if (button(ui, theme, 500 + i, tx, y + 80 * s, tw, 40 * s, kTabNames[i], 3, tab == i)) settingsTab = i;
        tx += tw + 10 * s;
    }
    if (tab != 5) {
        float bw = 150 * s, bh = 36 * s, bx = lx + lw - bw, by = y + 26 * s;
        if (ui.hover(bx, by, bw, bh))
            hovered = {509, "Сбросить вкладку",
                       std::string("Возвращает значения по умолчанию для всех параметров вкладки «") + kTabNames[tab] + "». Другие вкладки не меняются.", -1};
        if (button(ui, theme, 509, bx, by, bw, bh, "СБРОСИТЬ", 0)) resetTab(settings, tab);
    }

    // Scrollable list of rows.
    float ly = y + 140 * s, lh = y + h - pad - ly, rh = 46 * s;
    float maxScroll = std::max(0.0f, P.content[tab] - lh);
    bool scrollable = maxScroll > 0.5f;
    float rw = lw - (scrollable ? 22 * s : 0);
    float& sc = P.scroll[tab];
    float& st = P.target[tab];
    float thumbH = scrollable ? std::max(48 * s, lh * lh / P.content[tab]) : lh;
    float sbX = lx + lw - 5 * s;
    bool sbHot = scrollable && ui.hover(sbX - 9 * s, ly, 18 * s, lh);
    if (ui.scroll != 0 && ui.hover(lx - 12 * s, ly, lw + 12 * s, lh)) st -= ui.scroll * rh * 2.0f;
    if (ui.mousePressed && sbHot) ui.activeId = 590;
    if (ui.activeId == 590) {
        if ((ui.mouseDown || ui.mouseReleased) && scrollable) st = sc = saturate((ui.mouse.y - ly - thumbH * 0.5f) / std::max(1.0f, lh - thumbH)) * maxScroll;
        if (!ui.mouseDown) ui.activeId = 0;
    }
    st = clampf(st, 0, maxScroll);
    sc = clampf(damp(sc, st, 18.0f, dt), 0, maxScroll);
    if (std::fabs(sc - st) < 0.5f) sc = st;

    // The gap and the description column count as "reading" the pinned row.
    bool reading = ui.hover(lx + lw, dy, dx + dw - (lx + lw), dh);
    ui.setClip(lx - 12 * s, ly, lw + 12 * s, lh);
    float cur = 0, ry = 0;
    auto header = [&](const char* t) {
        float hh = cur > 0 ? 46 * s : 32 * s, hy = ly + cur - sc;
        if (hy + hh > ly && hy < ly + lh) {
            float tw = ui.text(fBold, t, lx, hy + hh - 25 * s, 16 * s, colors::accent, ALIGN_LEFT, 2.5f * s);
            ui.rect(lx + tw + 14 * s, hy + hh - 16 * s, rw - tw - 14 * s, 1, rgba(222, 178, 84, 50));
        }
        cur += hh;
    };
    auto row = [&](int id, const char* title, const std::string& text, int perf, float height) -> bool {
        float hh = height > 0 ? height : rh;
        ry = ly + cur - sc;
        cur += hh;
        if (ry + hh <= ly || ry >= ly + lh) return false;
        bool hot = ui.hover(lx - 10 * s, ry, rw + 20 * s, hh);
        float& a = widgetAnim(100000 + id);
        a = approach(a, hot || (reading && P.pinned.id == id) ? 1.0f : 0.0f, dt * 10.0f);
        if (a > 0.01f) {
            ui.rect(lx - 10 * s, ry + 1 * s, rw + 20 * s, hh - 2 * s, rgba(255, 255, 255, (int)(18 * a)), 6 * s);
            ui.rect(lx - 10 * s, ry + 9 * s, 3 * s, hh - 18 * s, withAlpha(colors::accent, a), 1.5f * s);
        }
        if (hot) hovered = {id, title, text, perf};
        return true;
    };
    auto sliderRow = [&](int id, const char* label, const std::string& text, int perf, float& v, float mn, float mx, const char* fmt) {
        return row(id, label, text, perf, 0) && slider(ui, theme, id, lx, ry, rw, label, v, mn, mx, fmt, rh);
    };
    auto toggleRow = [&](int id, const char* label, const std::string& text, int perf, bool& v) {
        return row(id, label, text, perf, 0) && toggle(ui, theme, id, lx, ry, rw, label, v, rh);
    };
    auto selectRow = [&](int id, const char* label, const std::string& text, int perf, int& idx, const std::vector<std::string>& opts, int skip) {
        return row(id, label, text, perf, 0) && selector(ui, theme, id, lx, ry, rw, label, idx, opts, rh, skip);
    };
    auto percentRow = [&](int id, const char* label, const std::string& text, int perf, float& v, float mn, float mx, float step) {
        float pv = v * 100.0f;
        if (!sliderRow(id, label, text, perf, pv, mn, mx, "%.0f%%")) return false;
        v = snap(pv, step) / 100.0f;
        return true;
    };

    if (tab == 0) {
        header("МЫШЬ");
        if (sliderRow(510, "Чувствительность мыши",
                      "Скорость поворота камеры при движении мыши. Шкала как в CS2 (1,00 = 0,022° на отсчёт мыши), поэтому значение из CS2 можно "
                      "перенести без изменений. Уменьшите, если трудно точно навестись; увеличьте, если не хватает скорости разворота.",
                      -1, settings.sensitivity, 0.2f, 6.0f, "%.2f"))
            settings.sensitivity = snap(settings.sensitivity, 0.01f);
        if (sliderRow(511, "Чувствительность в прицеле",
                      "Множитель чувствительности при стрельбе через оптику AWP. При 1,00 скорость уменьшается пропорционально увеличению прицела, как "
                      "в CS2. Уменьшите, если в прицеле мышь кажется слишком быстрой.",
                      -1, settings.zoomSensitivity, 0.3f, 2.0f, "%.2f"))
            settings.zoomSensitivity = snap(settings.zoomSensitivity, 0.01f);
        toggleRow(512, "Инвертировать мышь",
                  "Меняет направление по вертикали: движение мыши вперёд наклоняет взгляд вниз. Включайте, только если привыкли к такому управлению, "
                  "например в авиасимуляторах.",
                  -1, settings.invertY);
        std::string raw =
            "Читает движение мыши напрямую с устройства, без ускорения и масштабирования указателя операционной системы. Рекомендуется держать "
            "включённым: одинаковое движение руки всегда даёт одинаковый поворот.";
        raw += win.rawMouseSupported() ? "\n\nВаша система поддерживает прямой ввод."
                                       : "\n\nВаша система не поддерживает прямой ввод — используется обычное движение курсора.";
        toggleRow(513, "Прямой ввод мыши", raw, -1, settings.rawInput);
        header("ОРУЖИЕ В РУКАХ");
        int vp = std::max(0, std::min(settings.vmPreset, 2));
        if (selectRow(514, "Положение оружия",
                      "Готовые положения оружия из CS2. «Рабочий стол» — стандартное; «Диван» — крупнее и ниже, для игры перед телевизором; "
                      "«Классика» — правее, дальше и ниже, как в CS:GO. Выбор пресета перезаписывает поле зрения и смещения ниже.",
                      -1, vp, {"Рабочий стол", "Диван", "Классика"}, -1))
            settings.applyViewmodelPreset(vp);
        if (sliderRow(515, "Поле зрения оружия",
                      "Насколько крупно выглядят руки и оружие. При меньшем значении модель крупнее и ближе, при большем — мельче и открывает больше "
                      "экрана. На поле зрения самой игры не влияет.",
                      -1, settings.vmFov, 54.0f, 68.0f, "%.0f"))
            settings.vmFov = std::round(settings.vmFov);
        if (sliderRow(516, "Смещение по X", "Сдвигает оружие вправо (+) или влево (-) от стандартного положения.", -1, settings.vmOffsetX, -2.0f, 2.5f,
                      "%.1f"))
            settings.vmOffsetX = snap(settings.vmOffsetX, 0.1f);
        if (sliderRow(517, "Смещение по Y", "Выдвигает оружие вперёд (+) или приближает его к камере (-).", -1, settings.vmOffsetY, -2.0f, 2.0f, "%.1f"))
            settings.vmOffsetY = snap(settings.vmOffsetY, 0.1f);
        if (sliderRow(518, "Смещение по Z", "Поднимает (+) или опускает (-) оружие. Опущенное оружие меньше закрывает обзор.", -1, settings.vmOffsetZ, -2.0f,
                      2.0f, "%.1f"))
            settings.vmOffsetZ = snap(settings.vmOffsetZ, 0.1f);
        percentRow(519, "Покачивание при ходьбе",
                   "Сила покачивания рук и оружия при движении. 0% — оружие неподвижно и не отвлекает от прицела, 100% — полное покачивание, как в CS2.",
                   -1, settings.vmBob, 0.0f, 100.0f, 5.0f);
    } else if (tab == 1) {
        settings.quality = settings.detectQualityPreset();
        bool custom = false;
        header("КАЧЕСТВО");
        int q = std::max(0, std::min(settings.quality, 4));
        const int qPerf[5] = {1, 2, 3, 4, 3};
        if (selectRow(520, "Качество графики",
                      "Быстрый выбор всех параметров группы «Качество»: сглаживания, теней, затенения, текстур и эффектов. «Своё» появляется, когда "
                      "параметры изменены вручную. На слабом компьютере начните с «Низкого».",
                      qPerf[q], q, {"Низкое", "Среднее", "Высокое", "Максимальное", "Своё"}, 4))
            settings.applyQualityPreset(q);
        int aa = settings.msaa >= 8 ? 3 : settings.msaa >= 4 ? 2 : settings.msaa >= 2 ? 1 : 0;
        if (selectRow(521, "Сглаживание (MSAA)",
                      "Убирает «лесенку» на краях стен, ящиков и моделей. 4x — хороший баланс качества и скорости, 8x — самые ровные края.", 3, aa,
                      {"Выкл.", "2x", "4x", "8x"}, -1)) {
            settings.msaa = aa ? 1 << aa : 0;
            custom = true;
        }
        custom |= selectRow(522, "Качество теней",
                            "Чёткость и дальность теней от солнца. «Высокое» — более чёткие тени, «Максимальное» — ещё и дальние тени и реалистично "
                            "мягкие края. Выключение заметно поднимает FPS, но картинка становится плоской.",
                            3, settings.shadows, {"Выкл.", "Среднее", "Высокое", "Максимальное"}, -1);
        custom |= selectRow(523, "Затенение (SSAO)",
                            "Мягкие контактные тени в углах, щелях и под предметами — сцена выглядит объёмнее. «Высокое» даёт более гладкий результат "
                            "без шума.",
                            3, settings.ssao, {"Выкл.", "Обычное", "Высокое"}, -1);
        custom |= toggleRow(524, "Солнечные лучи", "Объёмные лучи солнечного света в воздухе, пробивающиеся из-за стен и арок. Чисто визуальный эффект.", 2,
                            settings.sunShafts);
        custom |= toggleRow(525, "FXAA",
                            "Быстрое сглаживание на этапе постобработки: смягчает края текстур и мелких деталей, которые не убирает MSAA. Слегка "
                            "размывает картинку — это компенсирует резкость.",
                            1, settings.fxaa);
        custom |= selectRow(526, "Качество текстур",
                            "Разрешение текстур поверхностей: песка, камня, дерева и металла. Низкое экономит видеопамять на слабых видеокартах, но "
                            "вблизи поверхности менее чёткие. При смене текстуры создаются заново — возможна короткая пауза.",
                            1, settings.textureQuality, {"Низкое", "Среднее", "Высокое"}, -1);
        int an = settings.anisotropy >= 16 ? 4 : settings.anisotropy >= 8 ? 3 : settings.anisotropy >= 4 ? 2 : settings.anisotropy >= 2 ? 1 : 0;
        if (selectRow(527, "Анизотропная фильтрация",
                      "Сохраняет чёткость пола и стен, которые видны под острым углом вдали. На современных видеокартах 16x почти не влияет на FPS.", 1,
                      an, {"1x", "2x", "4x", "8x", "16x"}, -1)) {
            settings.anisotropy = 1 << an;
            custom = true;
        }
        custom |= toggleRow(528, "Динамическое освещение",
                            "Дульные вспышки и взрывы освещают стены, пол и модели вокруг. Помогает заметить стрельбу за углом.", 2,
                            settings.dynamicLights);
        custom |= toggleRow(529, "Блики объектива", "Блики и ореолы, когда в кадре яркое солнце. Чисто визуальный эффект.", 1, settings.lensFlare);
        custom |= toggleRow(530, "Свечение (bloom)", "Мягкое свечение вокруг ярких источников света: неба, солнца, вспышек и взрывов.", 1, settings.bloom);
        header("ИЗОБРАЖЕНИЕ");
        percentRow(531, "Масштаб рендеринга",
                   "Разрешение, в котором рисуется 3D-сцена, относительно экрана. Ниже 100% — заметно больше FPS на слабых видеокартах, но картинка "
                   "мягче. Интерфейс всегда остаётся чётким.",
                   4, settings.renderScale, 50.0f, 100.0f, 5.0f);
        if (sliderRow(532, "Яркость",
                      "Смещение экспозиции всей картинки. Повышайте, если тёмные углы и туннели плохо видны на вашем мониторе; понижайте, если "
                      "картинка пересвечена.",
                      0, settings.brightness, -1.0f, 1.0f, "%+.2f"))
            settings.brightness = snap(settings.brightness, 0.05f);
        percentRow(533, "Насыщенность", "Насыщенность цветов. Меньше 100% — более блёклая, «киношная» картинка, больше — яркие сочные цвета.", 0,
                   settings.saturation, 50.0f, 150.0f, 5.0f);
        percentRow(534, "Резкость",
                   "Подчёркивает мелкие детали после сглаживания и масштабирования. Особенно полезна с FXAA и масштабом рендеринга ниже 100%. "
                   "Слишком высокое значение даёт светлые ореолы на краях.",
                   1, settings.sharpen, 0.0f, 100.0f, 5.0f);
        toggleRow(535, "Зернистость плёнки",
                  "Лёгкий шум плёнки поверх картинки: убирает полосы на градиентах неба и делает изображение кинематографичнее.", 0, settings.filmGrain);
        toggleRow(536, "Хроматическая аберрация",
                  "Цветные каймы по краям экрана, как у настоящего объектива. Выглядит кинематографично, но немного размывает детали по краям.", 0,
                  settings.chromatic);
        toggleRow(537, "Виньетка", "Мягко затемняет углы экрана и фокусирует взгляд на центре.", 0, settings.vignette);
        header("ЭКРАН");
        toggleRow(538, "Вертикальная синхронизация",
                  "Синхронизирует кадры с частотой монитора и убирает разрывы изображения, но добавляет задержку ввода. В соревновательной игре её "
                  "обычно выключают.",
                  0, settings.vsync);
        const int fpsv[8] = {0, 60, 90, 120, 144, 165, 240, 300};
        int fi = 0;
        for (int i = 0; i < 8; i++)
            if (std::abs(fpsv[i] - settings.fpsMax) < std::abs(fpsv[fi] - settings.fpsMax)) fi = i;
        if (selectRow(539, "Ограничение FPS",
                      "Максимальная частота кадров, когда вертикальная синхронизация выключена. Ограничение снижает нагрев и шум видеокарты; ставьте "
                      "чуть выше частоты монитора.",
                      0, fi, {"Без ограничения", "60", "90", "120", "144", "165", "240", "300"}, -1))
            settings.fpsMax = fpsv[fi];
        toggleRow(540, "Полноэкранный режим", "Игра на весь экран без рамок окна. Обычно даёт чуть больше FPS и меньшую задержку, чем оконный режим.", 0,
                  settings.fullscreen);
        if (custom) settings.quality = settings.detectQualityPreset();
    } else if (tab == 2) {
        header("ГРОМКОСТЬ");
        percentRow(550, "Общая громкость", "Громкость всех звуков игры. Ползунки ниже задают долю от неё для отдельных групп звуков.", -1, settings.volume,
                   0.0f, 100.0f, 1.0f);
        percentRow(551, "Оружие", "Выстрелы, перезарядка, взрывы гранат и удары ножом.", -1, settings.volumeWeapons, 0.0f, 100.0f, 1.0f);
        percentRow(552, "Мир и игроки",
                   "Шаги, попадания, звуки бомбы и окружения. Не делайте слишком тихо: по шагам слышно приближение противника.", -1,
                   settings.volumeWorld, 0.0f, 100.0f, 1.0f);
        percentRow(553, "Интерфейс", "Звуки меню, закупки и подтверждения убийства.", -1, settings.volumeUi, 0.0f, 100.0f, 1.0f);
        percentRow(554, "Музыка", "Короткие музыкальные темы в начале раунда, при победе и поражении.", -1, settings.volumeMusic, 0.0f, 100.0f, 1.0f);
        header("ВЫВОД");
        toggleRow(555, "Объёмный звук (наушники)",
                  "3D-звук для наушников: задержка между ушами и «тень головы» помогают понять, откуда звук — слева, справа, спереди или сзади. На "
                  "колонках его можно выключить.",
                  -1, settings.spatialAudio);
        bool dev = audio.available();
        if (row(556, "Устройство вывода",
                dev ? "Звук выводится на устройство по умолчанию операционной системы."
                    : "Звуковое устройство не найдено, игра работает без звука. Подключите наушники или колонки и перезапустите игру.",
                -1, 0))
            valueRow(ui, theme, lx, ry, rw, "Устройство вывода", dev ? "Подключено" : "Не найдено", dev ? colors::greenHi : colors::red, rh);
    } else if (tab == 3) {
        header("HUD");
        toggleRow(560, "Показывать FPS", "Счётчик кадров в секунду в правом нижнем углу экрана. Помогает подобрать настройки графики.", -1, settings.showFps);
        percentRow(561, "Масштаб интерфейса",
                   "Размер HUD: здоровья, патронов, таймера, радара и списка убийств. Уменьшите, чтобы освободить экран; увеличьте на большом мониторе.",
                   -1, settings.hudScale, 80.0f, 120.0f, 5.0f);
        std::vector<std::string> hcols;
        for (auto& c : kHudColors) hcols.push_back(c.name);
        selectRow(562, "Цвет интерфейса", "Основной цвет цифр и значков HUD. Предупреждения — мало здоровья или патронов — всё равно остаются красными.",
                  -1, settings.hudColor, hcols, -1);
        header("РАДАР");
        percentRow(563, "Масштаб радара", "Насколько крупно показана карта на радаре. Больше — ближе и подробнее, меньше — видна большая часть карты.", -1,
                   settings.radarZoom, 60.0f, 160.0f, 5.0f);
        toggleRow(564, "Вращение радара",
                  "Вкл.: радар поворачивается вместе с игроком, и направление взгляда всегда вверху. Выкл.: карта неподвижна, поворачивается стрелка "
                  "игрока.",
                  -1, settings.radarRotate);
    } else if (tab == 4) {
        header("ПОВЕДЕНИЕ");
        selectRow(570, "Стиль",
                  "Статичный — прицел всегда одинаковый. Динамический — линии расходятся при движении, в прыжке и при стрельбе, показывая текущий "
                  "разброс оружия.",
                  -1, settings.chStyle, {"Статичный", "Динамический"}, -1);
        toggleRow(571, "T-образный прицел", "Убирает верхнюю линию прицела, чтобы она не закрывала голову противника.", -1, settings.chTStyle);
        toggleRow(572, "Следовать за отдачей",
                  "Прицел смещается туда, куда уводит пули отдача. Помогает учиться контролю спрея; многие игроки предпочитают неподвижный прицел.", -1,
                  settings.chFollowRecoil);
        header("ФОРМА И ЦВЕТ");
        if (sliderRow(573, "Длина", "Длина каждой из четырёх линий прицела.", -1, settings.chSize, 0.5f, 10.0f, "%.1f"))
            settings.chSize = snap(settings.chSize, 0.1f);
        if (sliderRow(574, "Промежуток", "Расстояние от центра до линий. Отрицательные значения сводят линии ближе к центру.", -1, settings.chGap, -5.0f,
                      5.0f, "%.1f"))
            settings.chGap = snap(settings.chGap, 0.1f);
        if (sliderRow(575, "Толщина", "Толщина линий прицела.", -1, settings.chThickness, 0.5f, 4.0f, "%.1f"))
            settings.chThickness = snap(settings.chThickness, 0.1f);
        toggleRow(576, "Обводка", "Тёмная рамка вокруг линий: прицел лучше виден на светлом фоне — небе, песке и стенах.", -1, settings.chOutline);
        toggleRow(577, "Точка в центре", "Добавляет точку в центр прицела.", -1, settings.chDot);
        std::vector<std::string> cols;
        for (auto& c : kCrosshairColors) cols.push_back(c.name);
        selectRow(578, "Цвет", "Цвет прицела. Яркие зелёный и голубой хорошо заметны и на песке, и в тени.", -1, settings.chColor, cols, -1);
        percentRow(579, "Непрозрачность", "Прозрачность прицела: 100% — полностью непрозрачный.", -1, settings.chAlpha, 20.0f, 100.0f, 5.0f);
    } else {
        int id = 600;
        for (const BindGroup& g : bindGroups()) {
            header(g.title);
            for (const Bind& b : g.binds) {
                if (row(id, b.action, b.desc, -1, 0)) {
                    float cy = ry + rh * 0.5f;
                    ui.text(fRegular, b.action, lx, cy - 10 * s, 20 * s, colors::text);
                    keyCaps(*this, b.keys, lx + rw, cy);
                    ui.rect(lx, ry + rh - 1, rw, 1, colors::line);
                }
                id++;
            }
        }
    }
    P.content[tab] = cur;
    ui.clearClip();
    if (scrollable) {
        float thumbY = ly + (lh - thumbH) * (maxScroll > 0 ? sc / maxScroll : 0.0f);
        ui.rect(sbX - 2 * s, ly, 4 * s, lh, rgba(255, 255, 255, 18), 2 * s);
        ui.rect(sbX - 2 * s, thumbY, 4 * s, thumbH, sbHot || ui.activeId == 590 ? colors::accent : rgba(255, 255, 255, 90), 2 * s);
    }

    // Description column.
    ui.rect(dx, dy, dw, dh, rgba(0, 0, 0, 70), 10 * s);
    ui.border(dx, dy, dw, dh, colors::line, 10 * s, 1);
    float ix = dx + 26 * s, iw = dw - 52 * s, iy = dy + 26 * s;
    if (tab == 4) {
        iy += crosshairPreview(*this, ix, iy, iw, P.time);
    } else if (tab == 3) {
        iy += hudPreview(*this, ix, iy, iw, P.time);
    } else if (tab == 2) {
        bool dev = audio.available();
        ui.circle(ix + 7 * s, iy + 13 * s, 6 * s, dev ? colors::greenHi : colors::red);
        ui.text(fBold, dev ? "Звук работает" : "Звуковое устройство не найдено", ix + 24 * s, iy + 2 * s, 20 * s, colors::text);
        ui.text(fRegular, dev ? "Вывод на устройство по умолчанию" : "Игра работает без звука", ix + 24 * s, iy + 30 * s, 17 * s, colors::dim);
        iy += 76 * s;
    }
    Help show = hovered.id ? hovered : reading && P.pinned.id ? P.pinned : Help();
    if (hovered.id) P.pinned = hovered;
    int showId = show.id ? show.id : -1 - tab;
    if (showId != P.shownId) {
        P.shownId = showId;
        P.fade = 0;
    }
    P.fade = approach(P.fade, 1.0f, dt * 7.0f);
    float fa = 0.25f + 0.75f * P.fade;
    const std::string title = show.id ? show.title : tabTitle(tab);
    const std::string body = show.id ? show.text : tabText(tab);
    iy += ui.textWrapped(fBold, title, ix, iy, 28 * s, iw, withAlpha(colors::text, fa), 34 * s) + 8 * s;
    ui.rect(ix, iy, 44 * s, 3 * s, withAlpha(colors::accent, fa), 1.5f * s);
    iy += 20 * s;
    iy += ui.textWrapped(fRegular, body, ix, iy, 19 * s, iw, withAlpha(rgba(206, 211, 220), fa), 27 * s) + 14 * s;
    if (show.id && show.perf >= 0) iy += perfMeter(*this, ix, iy, show.perf, fa);
    if (!show.id && tab == 1) {
        const char* qn[5] = {"Низкое", "Среднее", "Высокое", "Максимальное", "Своё"};
        const int qPerf[5] = {1, 2, 3, 4, 3};
        int q = std::max(0, std::min(settings.quality, 4));
        float w0 = ui.text(fRegular, "Текущий пресет:", ix, iy, 18 * s, colors::dim);
        ui.text(fBold, qn[q], ix + w0 + 7 * s, iy, 18 * s, colors::accent);
        iy += 34 * s;
        iy += perfMeter(*this, ix, iy, qPerf[q], 1.0f);
    }
    if (iy < dy + dh - 60 * s) {
        ui.rect(ix, dy + dh - 58 * s, iw, 1, colors::line);
        ui.text(fRegular, "Изменения применяются сразу и сохраняются при выходе из игры.", ix, dy + dh - 44 * s, 16 * s, colors::faint);
    }
}

void App::drawCrosshair(float cx, float cy, float sc) { drawCrosshairShape(ui, settings, cx, cy, sc, 0.0f); }

void App::drawIcon(int weapon, float x, float y, float h, uint32_t tint, bool alignRight, float maxW) {
    if (weapon < 0 || weapon >= W_COUNT) return;
    float w = h * iconAspect[weapon];
    if (maxW > 0 && w > maxW) {
        float nh = h * maxW / w;
        y += (h - nh) * 0.5f;
        h = nh;
        w = maxW;
    }
    if (alignRight) x -= w;
    vec4 uv = iconUV[weapon];
    ui.image(iconTex, x, y, w, h, tint, vec2(uv.x, uv.w), vec2(uv.z, uv.y));
}
