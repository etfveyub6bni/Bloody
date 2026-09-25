// Immediate-mode 2D UI renderer: SDF text, rounded panels, frosted glass, images.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "core/common.h"
#include "gfx/glutil.h"

struct Glyph {
    float u0, v0, u1, v1;
    float w, h, xoff, yoff, advance;
};

class Font {
public:
    bool load(const std::string& path, float basePx = 48.0f, int atlas = 1024);
    const Glyph* get(uint32_t cp) const;
    GLuint tex = 0;
    float basePx = 48, ascent = 0, descent = 0, lineGap = 0;
    float spread = 8;  // SDF range in pixels at basePx

private:
    std::unordered_map<uint32_t, Glyph> m_glyphs;
    std::vector<unsigned char> m_ttf;
};

inline uint32_t rgba(int r, int g, int b, int a = 255) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}
inline uint32_t withAlpha(uint32_t c, float a) {
    uint32_t na = (uint32_t)(saturate(a * ((c >> 24) / 255.0f)) * 255.0f + 0.5f);
    return (c & 0x00FFFFFFu) | (na << 24);
}
std::u32string utf8to32(const std::string& s);

enum TextAlign { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

class UI {
public:
    bool init();
    void begin(int w, int h, GLuint blurTex, float scale);
    void end();

    // Geometry (pixels, origin top-left).
    void rect(float x, float y, float w, float h, uint32_t col, float radius = 0);
    void rectGrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom, float radius = 0);
    void rectGradH(float x, float y, float w, float h, uint32_t left, uint32_t right);
    void border(float x, float y, float w, float h, uint32_t col, float radius, float thickness);
    void glass(float x, float y, float w, float h, uint32_t tint, float radius);
    void shadow(float x, float y, float w, float h, float radius, float softness, uint32_t col);
    void image(GLuint tex, float x, float y, float w, float h, uint32_t tint = 0xFFFFFFFFu, vec2 uv0 = {0, 0}, vec2 uv1 = {1, 1}, float radius = 0);
    // Axis-aligned quad with explicit UVs per corner (TL, TR, BR, BL), e.g. for a rotating radar.
    void imageUV(GLuint tex, float x, float y, float w, float h, const vec2 uv[4], uint32_t tint, float radius);
    void line(float x0, float y0, float x1, float y1, float thickness, uint32_t col);
    void circle(float cx, float cy, float r, uint32_t col);
    void ring(float cx, float cy, float r, float thickness, uint32_t col);
    // Text: y is the top of the line box.
    float text(Font& f, const std::string& s, float x, float y, float size, uint32_t col, int align = ALIGN_LEFT, float spacing = 0, float bold = 0);
    float textShadowed(Font& f, const std::string& s, float x, float y, float size, uint32_t col, int align = ALIGN_LEFT, float spacing = 0);
    float textWidth(Font& f, const std::string& s, float size, float spacing = 0);
    // Greedy word wrap; '\n' starts a new line. Returns the lines (drawn by textWrapped).
    std::vector<std::string> wrap(Font& f, const std::string& s, float size, float maxW);
    // Returns the height used (lines * lineH).
    float textWrapped(Font& f, const std::string& s, float x, float y, float size, float maxW, uint32_t col, float lineH);

    // Scissor clip in pixels (not nestable). hover() also ignores the mouse outside the clip.
    void setClip(float x, float y, float w, float h);
    void clearClip();

    // Input for widgets.
    vec2 mouse;
    bool mouseDown = false, mousePressed = false, mouseReleased = false;
    float scroll = 0;
    bool hover(float x, float y, float w, float h) const {
        if (m_clipOn && !(mouse.x >= m_clip.x && mouse.x < m_clip.x + m_clip.z && mouse.y >= m_clip.y && mouse.y < m_clip.y + m_clip.w)) return false;
        return mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h;
    }
    int width = 0, height = 0;
    float scale = 1;  // UI scale relative to a 1080p reference
    int activeId = 0;
    int hotSound = 0;    // incremented when a new widget becomes hovered (for UI sounds)
    int clickSound = 0;
    int lastHover = 0;

private:
    struct V {
        vec2 pos, uv;
        uint32_t color;
        vec4 rect, params;
    };
    void quad(float x, float y, float w, float h, vec2 uv0, vec2 uv1, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3, vec4 params, GLuint tex, float pad = 0);
    void setTex(GLuint tex);
    void flush();
    Shader m_shader;
    GLuint m_vao = 0, m_vbo = 0, m_white = 0, m_blur = 0, m_curTex = 0;
    std::vector<V> m_verts;
    bool m_clipOn = false;
    vec4 m_clip;
};
