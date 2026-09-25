#include "ui/widgets.h"

#include <cstdio>

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

bool slider(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, float& v, float mn, float mx, const char* fmt) {
    float s = th.s, h = 44 * s;
    bool hot = ui.hover(x, y, w, h);
    trackHover(ui, id, hot);
    ui.text(*th.regular, label, x, y + 4 * s, 20 * s, colors::text);
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, v);
    float bx = x + w * 0.5f, bw = w * 0.5f - 70 * s, by = y + 18 * s;
    ui.text(*th.bold, buf, x + w, y + 4 * s, 20 * s, colors::accent, ALIGN_RIGHT);
    if (ui.mousePressed && ui.hover(bx - 6 * s, y, bw + 12 * s, h)) ui.activeId = id;
    bool changed = false;
    if (ui.activeId == id) {
        if (ui.mouseDown) {
            float t = saturate((ui.mouse.x - bx) / bw);
            float nv = mn + (mx - mn) * t;
            if (nv != v) { v = nv; changed = true; }
        } else {
            ui.activeId = 0;
        }
    }
    float t = saturate((v - mn) / (mx - mn));
    ui.rect(bx, by, bw, 4 * s, rgba(255, 255, 255, 40), 2 * s);
    ui.rect(bx, by, bw * t, 4 * s, colors::accent, 2 * s);
    ui.circle(bx + bw * t, by + 2 * s, (hot || ui.activeId == id ? 9.0f : 7.0f) * s, colors::text);
    ui.rect(x, y + h - 1, w, 1, colors::line);
    return changed;
}

bool toggle(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, bool& v) {
    float s = th.s, h = 44 * s;
    bool hot = ui.hover(x, y, w, h);
    trackHover(ui, id, hot);
    ui.text(*th.regular, label, x, y + 4 * s, 20 * s, colors::text);
    float& a = widgetAnim(id);
    a = approach(a, v ? 1.0f : 0.0f, th.dt * 8.0f);
    float tw = 46 * s, thh = 24 * s, tx = x + w - tw, ty = y + 4 * s;
    ui.rect(tx, ty, tw, thh, lerpColor(rgba(255, 255, 255, 40), colors::accent, a), thh * 0.5f);
    ui.circle(tx + thh * 0.5f + (tw - thh) * a, ty + thh * 0.5f, thh * 0.38f, colors::text);
    ui.rect(x, y + h - 1, w, 1, colors::line);
    if (hot && ui.mouseReleased) {
        v = !v;
        ui.clickSound++;
        return true;
    }
    return false;
}

bool selector(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, int& idx, const std::vector<std::string>& opts) {
    float s = th.s, h = 44 * s;
    bool hot = ui.hover(x, y, w, h);
    trackHover(ui, id, hot);
    ui.text(*th.regular, label, x, y + 4 * s, 20 * s, colors::text);
    float bw = w * 0.45f, bx = x + w - bw;
    ui.text(*th.bold, opts[(size_t)std::max(0, std::min(idx, (int)opts.size() - 1))], bx + bw * 0.5f, y + 4 * s, 20 * s, colors::accent, ALIGN_CENTER);
    bool changed = false;
    for (int side = 0; side < 2; side++) {
        float ax = side == 0 ? bx : bx + bw - 24 * s;
        bool ah = ui.hover(ax, y, 24 * s, 32 * s);
        ui.text(*th.bold, side == 0 ? "<" : ">", ax + 12 * s, y + 3 * s, 22 * s, ah ? colors::text : colors::dim, ALIGN_CENTER);
        if (ah && ui.mouseReleased) {
            idx = (idx + (side == 0 ? -1 : 1) + (int)opts.size()) % (int)opts.size();
            changed = true;
            ui.clickSound++;
        }
    }
    ui.rect(x, y + h - 1, w, 1, colors::line);
    return changed;
}
