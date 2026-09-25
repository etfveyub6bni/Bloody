// Main menu: play setup, arsenal viewer and settings.
#include <cstdio>

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
        drawSettingsPanel(56 * s, 104 * s, 860 * s, 900 * s);
    }
    char foot[96];
    std::snprintf(foot, sizeof(foot), "C++ / OpenGL 3.3  ·  %.0f FPS", fps);
    ui.text(fRegular, foot, W - 28 * s, H - 40 * s, 16 * s, colors::faint, ALIGN_RIGHT);
}

void App::drawSettingsPanel(float x, float y, float w, float h) {
    float s = theme.s;
    ui.shadow(x, y, w, h, 12 * s, 30 * s, rgba(0, 0, 0, 120));
    ui.glass(x, y, w, h, rgba(12, 14, 19, 175), 12 * s);
    ui.border(x, y, w, h, colors::line, 12 * s, 1);
    ui.text(fTitle, "НАСТРОЙКИ", x + 28 * s, y + 22 * s, 34 * s, colors::text, ALIGN_LEFT, 2 * s);
    const char* sub[4] = {"ИГРА", "ВИДЕО", "ЗВУК", "ПРИЦЕЛ"};
    for (int i = 0; i < 4; i++)
        if (button(ui, theme, 500 + i, x + 28 * s + i * 142 * s, y + 80 * s, 130 * s, 40 * s, sub[i], 3, settingsTab == i)) settingsTab = i;
    float cx = x + 28 * s, cy = y + 148 * s, cw = w - 56 * s, row = 52 * s;
    if (settingsTab == 3) cw = w - 400 * s;
    if (settingsTab == 0) {
        slider(ui, theme, 510, cx, cy, cw, "Чувствительность мыши", settings.sensitivity, 0.2f, 6.0f, "%.2f");
        slider(ui, theme, 511, cx, cy + row, cw, "Чувствительность в прицеле", settings.zoomSensitivity, 0.3f, 2.0f, "%.2f");
        toggle(ui, theme, 512, cx, cy + row * 2, cw, "Инвертировать мышь", settings.invertY);
        selector(ui, theme, 513, cx, cy + row * 3, cw, "Положение оружия", settings.vmPreset, {"Рабочий стол", "Диван", "Классика"});
        slider(ui, theme, 514, cx, cy + row * 4, cw, "Поле зрения оружия", settings.vmFov, 54.0f, 68.0f, "%.0f");
        toggle(ui, theme, 515, cx, cy + row * 5, cw, "Показывать FPS", settings.showFps);
        ui.text(fRegular, "WASD — движение, Пробел — прыжок, Ctrl — присесть, Shift — шаг", cx, cy + row * 6.4f, 17 * s, colors::dim);
        ui.text(fRegular, "ЛКМ/ПКМ — огонь/прицел, R — перезарядка, F — осмотр, E — бомба", cx, cy + row * 6.4f + 26 * s, 17 * s, colors::dim);
        ui.text(fRegular, "1-5 / колесо — оружие, Q — прошлое, G — выбросить, B — закупка, Tab — счёт", cx, cy + row * 6.4f + 52 * s, 17 * s, colors::dim);
    } else if (settingsTab == 1) {
        int aa = settings.msaa >= 8 ? 3 : settings.msaa >= 4 ? 2 : settings.msaa >= 2 ? 1 : 0;
        if (selector(ui, theme, 520, cx, cy, cw, "Сглаживание (MSAA)", aa, {"Выкл.", "2x", "4x", "8x"})) settings.msaa = aa == 0 ? 0 : 1 << aa;
        selector(ui, theme, 521, cx, cy + row, cw, "Качество теней", settings.shadows, {"Выкл.", "Среднее", "Высокое"});
        float pct = settings.renderScale * 100.0f;
        if (slider(ui, theme, 522, cx, cy + row * 2, cw, "Масштаб рендеринга", pct, 50.0f, 100.0f, "%.0f%%")) settings.renderScale = std::round(pct / 5.0f) * 0.05f;
        toggle(ui, theme, 523, cx, cy + row * 3, cw, "Свечение (bloom)", settings.bloom);
        toggle(ui, theme, 524, cx, cy + row * 4, cw, "Вертикальная синхронизация", settings.vsync);
        int fi = settings.fpsMax == 0 ? 0 : settings.fpsMax <= 60 ? 1 : settings.fpsMax <= 144 ? 2 : 3;
        const int fpsv[4] = {0, 60, 144, 240};
        if (selector(ui, theme, 525, cx, cy + row * 5, cw, "Ограничение FPS", fi, {"Без ограничения", "60", "144", "240"})) settings.fpsMax = fpsv[fi];
        toggle(ui, theme, 526, cx, cy + row * 6, cw, "Полноэкранный режим", settings.fullscreen);
    } else if (settingsTab == 2) {
        float vol = settings.volume * 100.0f;
        if (slider(ui, theme, 530, cx, cy, cw, "Общая громкость", vol, 0.0f, 100.0f, "%.0f%%")) settings.volume = vol / 100.0f;
        ui.text(fRegular, audio.available() ? "Устройство вывода звука активно" : "Устройство вывода звука не найдено", cx, cy + row * 1.4f, 17 * s, colors::dim);
    } else {
        slider(ui, theme, 540, cx, cy, cw, "Длина", settings.chSize, 0.5f, 10.0f, "%.1f");
        slider(ui, theme, 541, cx, cy + row, cw, "Промежуток", settings.chGap, -5.0f, 5.0f, "%.1f");
        slider(ui, theme, 542, cx, cy + row * 2, cw, "Толщина", settings.chThickness, 0.5f, 4.0f, "%.1f");
        toggle(ui, theme, 543, cx, cy + row * 3, cw, "Обводка", settings.chOutline);
        toggle(ui, theme, 544, cx, cy + row * 4, cw, "Точка в центре", settings.chDot);
        std::vector<std::string> cols;
        for (auto& c : kCrosshairColors) cols.push_back(c.name);
        selector(ui, theme, 545, cx, cy + row * 5, cw, "Цвет", settings.chColor, cols);
        slider(ui, theme, 546, cx, cy + row * 6, cw, "Непрозрачность", settings.chAlpha, 0.2f, 1.0f, "%.2f");
        float bx = x + w - 340 * s, by = cy, bs = 300 * s;
        ui.rectGrad(bx, by, bs, bs * 0.55f, rgba(120, 160, 205), rgba(200, 210, 220), 8 * s);
        ui.rectGrad(bx, by + bs * 0.55f, bs, bs * 0.45f, rgba(196, 162, 112), rgba(150, 120, 80), 0);
        ui.border(bx, by, bs, bs, colors::line, 8 * s, 1);
        drawCrosshair(bx + bs * 0.5f, by + bs * 0.5f, s * 1.5f);
        ui.text(fRegular, "Предпросмотр", bx + bs * 0.5f, by + bs + 10 * s, 16 * s, colors::dim, ALIGN_CENTER);
    }
}

void App::drawCrosshair(float cx, float cy, float sc) {
    const CrosshairColor& cc = kCrosshairColors[std::max(0, std::min(settings.chColor, 5))];
    uint32_t col = rgba(cc.r, cc.g, cc.b, (int)(settings.chAlpha * 255));
    uint32_t out = rgba(0, 0, 0, (int)(settings.chAlpha * 200));
    float L = std::round(settings.chSize * 2.2f * sc), T = std::max(1.0f, std::round(settings.chThickness * 1.4f * sc));
    float G = std::round((settings.chGap + 4.0f) * sc);
    cx = std::round(cx);
    cy = std::round(cy);
    float o = std::max(1.0f, std::round(sc));
    auto bar = [&](float x, float y, float w, float h) {
        if (settings.chOutline) ui.rect(x - o, y - o, w + 2 * o, h + 2 * o, out);
        ui.rect(x, y, w, h, col);
    };
    float ht = std::floor(T * 0.5f);
    if (L > 0) {
        bar(cx - G - L, cy - ht, L, T);
        bar(cx + G, cy - ht, L, T);
        bar(cx - ht, cy - G - L, T, L);
        bar(cx - ht, cy + G, T, L);
    }
    if (settings.chDot) bar(cx - ht, cy - ht, T, T);
}

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
