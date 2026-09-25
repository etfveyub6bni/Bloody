// Small immediate-mode widget set styled after the CS2 menus.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/ui.h"

struct Settings;

struct Theme {
    Font* regular = nullptr;
    Font* bold = nullptr;
    Font* title = nullptr;
    float s = 1;  // pixel scale
    float dt = 0.016f;
};

namespace colors {
const uint32_t text = rgba(236, 238, 242);
const uint32_t dim = rgba(150, 156, 168);
const uint32_t faint = rgba(96, 102, 114);
const uint32_t panel = rgba(14, 17, 23, 190);
const uint32_t panelLight = rgba(30, 35, 44, 200);
const uint32_t line = rgba(255, 255, 255, 28);
const uint32_t accent = rgba(222, 178, 84);
const uint32_t green = rgba(86, 178, 74);
const uint32_t greenHi = rgba(112, 206, 96);
const uint32_t ct = rgba(104, 146, 214);
const uint32_t t = rgba(222, 190, 110);
const uint32_t red = rgba(226, 70, 64);
}  // namespace colors

float& widgetAnim(int id);
uint32_t lerpColor(uint32_t a, uint32_t b, float t);
bool button(UI& ui, Theme& th, int id, float x, float y, float w, float h, const std::string& label, int style = 0, bool selected = false);
// Setting rows: label on the left, control on the right, content centred in a row of height h (0 = 44 * scale).
bool slider(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, float& v, float mn, float mx, const char* fmt, float h = 0);
bool toggle(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, bool& v, float h = 0);
// `skip` is an option that is shown when current but never reached with the arrows (e.g. "custom").
bool selector(UI& ui, Theme& th, int id, float x, float y, float w, const std::string& label, int& idx, const std::vector<std::string>& opts, float h = 0,
              int skip = -1);
void valueRow(UI& ui, Theme& th, float x, float y, float w, const std::string& label, const std::string& value, uint32_t col, float h = 0);
void sectionTitle(UI& ui, Theme& th, float x, float y, const std::string& label);
// CS2 classic crosshair; extraGap (pixels) widens the gap for the dynamic style.
void drawCrosshairShape(UI& ui, const Settings& st, float cx, float cy, float scale, float extraGap = 0);
