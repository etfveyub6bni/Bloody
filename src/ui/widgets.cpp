#include "ui/widgets.h"

#include <cstdio>

#include "core/settings.h"

float& widgetAnim(int id) {
    static std::unordered_map<int, float> anims;
    return anims[id];
}

uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (float)((a >> s) & 255), cb = (float)((b >> s) & 255);
        r |= (uint32_t)(lerpf(ca, cb, saturate(t)) + 0.5f) << s;
    }
    return r;
}

static void trackHover(UI& ui, int id, bool hot) {
    if (hot && ui.lastHover != id) {
        ui.lastHover = id;
        ui.hotSound++;
    } else if (!hot && ui.lastHover == id) {
        ui.lastHover = 0;
    }
}

bool button(UI& ui, Theme& th, int id, float x, float y, float w, float h, const std::string& label, int style, bool selected) {
    bool hot = ui.hover(x, y, w, h);
    trackHover(ui, id, hot);
    float& a = widgetAnim(id);
    a = approach(a, hot ? 1.0f : 0.0f, th.dt * 8.0f);
    float s = th.s;
    bool clicked = hot && ui.mouseReleased;
    if (clicked) ui.clickSound++;
    if (style == 1) {  // primary (green GO button)
        ui.shadow(x, y + 4 * s, w, h, 6 * s, 18 * s, rgba(40, 140, 40, (int)(80 + 90 * a)));
        ui.rectGrad(x, y, w, h, lerpColor(colors::greenHi, rgba(140, 230, 120), a), lerpColor(colors::green, rgba(96, 190, 80), a), 6 * s);
        ui.border(x, y, w, h, rgba(255, 255, 255, (int)(40 + 60 * a)), 6 * s, 1.5f * s);
        ui.text(*th.bold, label, x + w * 0.5f, y + h * 0.5f - h * 0.32f, h * 0.55f, rgba(255, 255, 255), ALIGN_CENTER, 3 * s);
    } else if (style == 2) {  // top navigation tab
        uint32_t col = selected ? colors::text : lerpColor(colors::dim, colors::text, a);
        ui.text(*th.bold, label, x + w * 0.5f, y + h * 0.5f - h * 0.3f, h * 0.48f, col, ALIGN_CENTER, 2.5f * s);
        if (selected || a > 0.01f) {
            float lw = w * (selected ? 0.7f : 0.4f * a);
            ui.rect(x + (w - lw) * 0.5f, y + h - 3 * s, lw, 3 * s, selected ? colors::accent : withAlpha(colors::text, a * 0.6f), 1.5f * s);
        }
    } else if (style == 3) {  // option chip
        uint32_t bg = selected ? rgba(222, 178, 84, 60) : lerpColor(rgba(255, 255, 255, 10), rgba(255, 255, 255, 28), a);
        ui.rect(x, y, w, h, bg, 4 * s);
        ui.border(x, y, w, h, selected ? colors::accent : rgba(255, 255, 255, (int)(30 + 40 * a)), 4 * s, 1.2f * s);
        ui.text(*th.bold, label, x + w * 0.5f, y + h * 0.5f - h * 0.3f, h * 0.46f, selected ? colors::text : lerpColor(colors::dim, colors::text, a), ALIGN_CENTER, 1.2f * s);
    } else {  // regular
        ui.rect(x, y, w, h, lerpColor(rgba(255, 255, 255, 14), rgba(255, 255, 255, 34), std::max(a, selected ? 1.0f : 0.0f)), 4 * s);
        ui.border(x, y, w, h, rgba(255, 255, 255, (int)(26 + 50 * a)), 4 * s, 1.0f * s);
        ui.text(*th.bold, label, x + w * 0.5f, y + h * 0.5f - h * 0.3f, h * 0.46f, colors::text, ALIGN_CENTER, 1.5f * s);
    }
    return clicked;
}

void sectionTitle(UI& ui, Theme& th, float x, float y, const std::string& label) {
    ui.text(*th.bold, label, x, y, 17 * th.s, colors::dim, ALIGN_LEFT, 2.5f * th.s);
}

bool slider(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, float& v, float mn, float mx, const char* fmt, float h) {
    float s = th.s;
    if (h <= 0) h = 44 * s;
    float cy = y + h * 0.5f;
    bool hot = ui.hover(x, y, w, h);
    trackHover(ui, id, hot);
    ui.text(*th.regular, label, x, cy - 10 * s, 20 * s, colors::text);
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, v);
    float bx = x + w * 0.5f, bw = w * 0.5f - 76 * s;
    ui.text(*th.bold, buf, x + w, cy - 10 * s, 20 * s, colors::accent, ALIGN_RIGHT);
    if (ui.mousePressed && ui.hover(bx - 8 * s, y, bw + 16 * s, h)) ui.activeId = id;
    bool changed = false;
    if (ui.activeId == id) {
        // Also apply on release: at low FPS the last move and the release can arrive in the same frame.
        if (ui.mouseDown || ui.mouseReleased) {
            float t = saturate((ui.mouse.x - bx) / bw);
            float nv = mn + (mx - mn) * t;
            if (nv != v) { v = nv; changed = true; }
        }
        if (!ui.mouseDown) ui.activeId = 0;
    }
    float t = saturate((v - mn) / (mx - mn));
    bool active = ui.activeId == id;
    ui.rect(bx, cy - 2 * s, bw, 4 * s, rgba(255, 255, 255, 40), 2 * s);
    ui.rect(bx, cy - 2 * s, bw * t, 4 * s, colors::accent, 2 * s);
    if (active) ui.circle(bx + bw * t, cy, 13 * s, withAlpha(colors::accent, 0.35f));
    ui.circle(bx + bw * t, cy, (hot || active ? 9.0f : 7.0f) * s, colors::text);
    ui.rect(x, y + h - 1, w, 1, colors::line);
    return changed;
}

bool toggle(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, bool& v, float h) {
    float s = th.s;
    if (h <= 0) h = 44 * s;
    float cy = y + h * 0.5f;
    bool hot = ui.hover(x, y, w, h);
    trackHover(ui, id, hot);
    ui.text(*th.regular, label, x, cy - 10 * s, 20 * s, colors::text);
    float& a = widgetAnim(id);
    a = approach(a, v ? 1.0f : 0.0f, th.dt * 8.0f);
    float tw = 46 * s, thh = 24 * s, tx = x + w - tw, ty = cy - thh * 0.5f;
    ui.text(*th.regular, v ? "Вкл." : "Выкл.", tx - 12 * s, cy - 9 * s, 18 * s, v ? colors::text : colors::dim, ALIGN_RIGHT);
    ui.rect(tx, ty, tw, thh, lerpColor(rgba(255, 255, 255, hot ? 60 : 40), colors::accent, a), thh * 0.5f);
    ui.circle(tx + thh * 0.5f + (tw - thh) * a, ty + thh * 0.5f, thh * 0.38f, colors::text);
    ui.rect(x, y + h - 1, w, 1, colors::line);
    if (hot && ui.mouseReleased) {
        v = !v;
        ui.clickSound++;
        return true;
    }
    return false;
}

static void chevron(UI& ui, float cx, float cy, float sz, bool left, float t, uint32_t col) {
    float d = left ? 1.0f : -1.0f;
    ui.line(cx + d * sz * 0.5f, cy - sz, cx - d * sz * 0.5f, cy, t, col);
    ui.line(cx - d * sz * 0.5f, cy, cx + d * sz * 0.5f, cy + sz, t, col);
}

bool selector(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, int& idx, const std::vector<std::string>& opts, float h,
              int skip) {
    float s = th.s;
    if (h <= 0) h = 44 * s;
    float cy = y + h * 0.5f;
    int n = (int)opts.size();
    bool hot = ui.hover(x, y, w, h);
    trackHover(ui, id, hot);
    ui.text(*th.regular, label, x, cy - 10 * s, 20 * s, colors::text);
    float bw = w * 0.5f, bx = x + w - bw, aw = 32 * s;
    int cur = std::max(0, std::min(idx, n - 1));
    bool dots = n > 1 && n <= 8;
    ui.text(*th.bold, opts[(size_t)cur], bx + bw * 0.5f, cy - (dots ? 13 : 10) * s, 20 * s, colors::accent, ALIGN_CENTER);
    if (dots) {
        float dx = bx + bw * 0.5f - (n - 1) * 5 * s;
        for (int i = 0; i < n; i++)
            ui.rect(dx + i * 10 * s - 3 * s, cy + 11 * s, 6 * s, 3 * s, i == cur ? colors::accent : rgba(255, 255, 255, 45), 1.5f * s);
    }
    int step = 0;
    for (int side = 0; side < 2; side++) {
        float ax = side == 0 ? bx : bx + bw - aw;
        bool ah = ui.hover(ax, cy - 16 * s, aw, 32 * s);
        if (ah) ui.rect(ax, cy - 16 * s, aw, 32 * s, rgba(255, 255, 255, 22), 4 * s);
        chevron(ui, ax + aw * 0.5f, cy, 6 * s, side == 0, 2.2f * s, ah || hot ? colors::text : colors::dim);
        if (ah && ui.mouseReleased) step = side == 0 ? -1 : 1;
    }
    // Clicking the value itself advances, like the right arrow.
    if (!step && ui.mouseReleased && ui.hover(bx + aw, y, bw - aw * 2, h)) step = 1;
    bool changed = false;
    if (step) {
        int ni = cur;
        for (int k = 0; k < n; k++) {
            ni = (ni + step + n) % n;
            if (ni != skip) break;
        }
        changed = ni != idx;
        idx = ni;
        ui.clickSound++;
    }
    ui.rect(x, y + h - 1, w, 1, colors::line);
    return changed;
}

void valueRow(UI& ui, Theme& th, float x, float y, float w, const std::string& label, const std::string& value, uint32_t col, float h) {
    float s = th.s;
    if (h <= 0) h = 44 * s;
    float cy = y + h * 0.5f;
    ui.text(*th.regular, label, x, cy - 10 * s, 20 * s, colors::text);
    ui.text(*th.bold, value, x + w, cy - 10 * s, 20 * s, col, ALIGN_RIGHT);
    ui.rect(x, y + h - 1, w, 1, colors::line);
}

void drawCrosshairShape(UI& ui, const Settings& st, float cx, float cy, float sc, float extraGap) {
    const CrosshairColor& cc = kCrosshairColors[std::max(0, std::min(st.chColor, 5))];
    float alpha = saturate(st.chAlpha);
    uint32_t col = rgba(cc.r, cc.g, cc.b, (int)(alpha * 255));
    uint32_t out = rgba(0, 0, 0, (int)(alpha * 200));
    float L = std::round(st.chSize * 2.2f * sc), T = std::max(1.0f, std::round(st.chThickness * 1.4f * sc));
    float G = std::round((st.chGap + 4.0f) * sc + std::max(0.0f, extraGap));
    cx = std::round(cx);
    cy = std::round(cy);
    float o = std::max(1.0f, std::round(sc));
    auto bar = [&](float x, float y, float w, float h) {
        if (st.chOutline) ui.rect(x - o, y - o, w + 2 * o, h + 2 * o, out);
        ui.rect(x, y, w, h, col);
    };
    float ht = std::floor(T * 0.5f);
    if (L > 0) {
        bar(cx - G - L, cy - ht, L, T);
        bar(cx + G, cy - ht, L, T);
        if (!st.chTStyle) bar(cx - ht, cy - G - L, T, L);
        bar(cx - ht, cy + G, T, L);
    }
    if (st.chDot) bar(cx - ht, cy - ht, T, T);
}
