#include "ui/ui.h"

#include <cstddef>

#include "stb_truetype.h"

std::u32string utf8to32(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

bool Font::load(const std::string& path, float px, int atlas) {
    if (!readFileBytes(path, m_ttf)) {
        logError("Font not found: %s", path.c_str());
        return false;
    }
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, m_ttf.data(), stbtt_GetFontOffsetForIndex(m_ttf.data(), 0))) return false;
    basePx = px;
    float sc = stbtt_ScaleForPixelHeight(&info, px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    ascent = asc * sc;
    descent = desc * sc;
    lineGap = gap * sc;
    std::vector<uint32_t> cps;
    for (uint32_t c = 32; c < 127; c++) cps.push_back(c);
    for (uint32_t c = 0x410; c <= 0x44F; c++) cps.push_back(c);
    for (uint32_t c : {0x401u, 0x451u, 0xB7u, 0xA0u, 0x2012u, 0x2039u, 0x203Au, 0x2014u, 0x2013u, 0x2022u, 0xABu, 0xBBu, 0xB0u, 0xD7u, 0x2026u, 0x2116u, 0x25B2u, 0x25BCu, 0x2191u, 0x2193u, 0x2192u, 0x2190u})
        cps.push_back(c);
    std::vector<unsigned char> img((size_t)atlas * atlas, 0);
    int x = 1, y = 1, rowH = 0;
    const int pad = (int)spread;
    for (uint32_t cp : cps) {
        int gi = stbtt_FindGlyphIndex(&info, (int)cp);
        if (gi == 0 && cp != 32) continue;
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&info, gi, &adv, &lsb);
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetGlyphSDF(&info, sc, gi, pad, 128, 128.0f / spread, &w, &h, &xo, &yo);
        Glyph g{};
        g.advance = adv * sc;
        if (sdf) {
            if (x + w + 1 >= atlas) { x = 1; y += rowH + 1; rowH = 0; }
            if (y + h + 1 >= atlas) { stbtt_FreeSDF(sdf, nullptr); break; }
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w; i++) img[(size_t)(y + j) * atlas + x + i] = sdf[j * w + i];
            g.u0 = (float)x / atlas;
            g.v0 = (float)y / atlas;
            g.u1 = (float)(x + w) / atlas;
            g.v1 = (float)(y + h) / atlas;
            g.w = (float)w;
            g.h = (float)h;
            g.xoff = (float)xo;
            g.yoff = (float)yo;
            x += w + 1;
            rowH = std::max(rowH, h);
            stbtt_FreeSDF(sdf, nullptr);
        }
        m_glyphs[cp] = g;
    }
    tex = makeTexture2D(atlas, atlas, GL_R8, GL_RED, GL_UNSIGNED_BYTE, img.data(), false, GL_LINEAR, GL_CLAMP_TO_EDGE);
    return true;
}

const Glyph* Font::get(uint32_t cp) const {
    auto it = m_glyphs.find(cp);
    if (it != m_glyphs.end()) return &it->second;
    it = m_glyphs.find('?');
    return it != m_glyphs.end() ? &it->second : nullptr;
}

static const char* kUIVS = R"GLSL(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aRect;
layout(location = 4) in vec4 aParams;
uniform vec2 uScreen;
out vec2 vUV; out vec4 vColor; out vec4 vRect; out vec4 vParams;
void main() {
    vUV = aUV; vColor = aColor; vRect = aRect; vParams = aParams;
    vec2 p = aPos / uScreen * 2.0 - 1.0;
    gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
}
)GLSL";

static const char* kUIFS = R"GLSL(#version 330 core
uniform sampler2D uTex;
uniform sampler2D uBlur;
uniform vec2 uScreen;
in vec2 vUV; in vec4 vColor; in vec4 vRect; in vec4 vParams;
out vec4 oColor;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    int mode = int(vParams.y + 0.5);
    vec4 c = vColor;
    float d = sdRoundBox(vRect.xy, vRect.zw, vParams.x);
    float aa = max(fwidth(d), 0.5);
    if (mode == 0) {
        c *= texture(uTex, vUV);
        if (vParams.x > 0.0) c.a *= 1.0 - smoothstep(-aa, aa, d);
    } else if (mode == 1) {
        float s = texture(uTex, vUV).r;
        float w = max(fwidth(s), 0.01) * 0.75;
        float edge = 0.5 - vParams.z;
        c.a *= smoothstep(edge - w - vParams.w, edge + w, s);
    } else if (mode == 2) {
        c.a *= 1.0 - smoothstep(-aa, aa, d);
    } else if (mode == 3) {
        c.a *= (1.0 - smoothstep(-aa, aa, d)) * smoothstep(-aa, aa, d + vParams.z);
    } else if (mode == 4) {
        vec3 b = texture(uBlur, gl_FragCoord.xy / uScreen).rgb;
        c = vec4(mix(b, vColor.rgb, vColor.a), 1.0 - smoothstep(-aa, aa, d));
    } else if (mode == 5) {
        c.a *= 1.0 - smoothstep(-vParams.w, vParams.w, d);
    }
    oColor = c;
}
)GLSL";

bool UI::init() {
    if (!m_shader.build(kUIVS, kUIFS, "ui")) return false;
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), (void*)offsetof(V, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, rect));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, params));
    glBindVertexArray(0);
    unsigned char white[4] = {255, 255, 255, 255};
    m_white = makeTexture2D(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, white, false, GL_NEAREST, GL_CLAMP_TO_EDGE);
    return true;
}

void UI::begin(int w, int h, GLuint blurTex, float sc) {
    width = w;
    height = h;
    m_blur = blurTex;
    scale = sc;
    m_verts.clear();
    m_curTex = m_white;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_shader.use();
    m_shader.set("uScreen", vec2((float)w, (float)h));
    m_shader.set("uTex", 0);
    m_shader.set("uBlur", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_blur);
    glActiveTexture(GL_TEXTURE0);
}

void UI::end() {
    flush();
    glDisable(GL_BLEND);
}

void UI::setTex(GLuint tex) {
    if (tex != m_curTex) {
        flush();
        m_curTex = tex;
    }
}

void UI::flush() {
    if (m_verts.empty()) return;
    m_shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_curTex);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m_verts.size() * sizeof(V)), m_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_verts.size());
    glBindVertexArray(0);
    m_verts.clear();
}

void UI::quad(float x, float y, float w, float h, vec2 uv0, vec2 uv1, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3, vec4 params, GLuint tex,
              float pad) {
    setTex(tex);
    float hw = w * 0.5f, hh = h * 0.5f;
    float x0 = x - pad, y0 = y - pad, x1 = x + w + pad, y1 = y + h + pad;
    auto mk = [&](float px, float py, vec2 uv, uint32_t c) {
        V v;
        v.pos = {px, py};
        v.uv = uv;
        v.color = c;
        v.rect = vec4(px - (x + hw), py - (y + hh), hw, hh);
        v.params = params;
        return v;
    };
    V a = mk(x0, y0, uv0, c0), b = mk(x1, y0, {uv1.x, uv0.y}, c1), cc = mk(x1, y1, uv1, c2), d = mk(x0, y1, {uv0.x, uv1.y}, c3);
    m_verts.insert(m_verts.end(), {a, b, cc, a, cc, d});
}

void UI::rect(float x, float y, float w, float h, uint32_t col, float radius) {
    quad(x, y, w, h, {0, 0}, {1, 1}, col, col, col, col, vec4(radius, radius > 0 ? 2.0f : 0.0f, 0, 0), m_white, radius > 0 ? 1.0f : 0.0f);
}

void UI::rectGrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom, float radius) {
    quad(x, y, w, h, {0, 0}, {1, 1}, top, top, bottom, bottom, vec4(radius, radius > 0 ? 2.0f : 0.0f, 0, 0), m_white, 0);
}

void UI::rectGradH(float x, float y, float w, float h, uint32_t left, uint32_t right) {
    quad(x, y, w, h, {0, 0}, {1, 1}, left, right, right, left, vec4(0, 0, 0, 0), m_white, 0);
}

void UI::border(float x, float y, float w, float h, uint32_t col, float radius, float t) {
    quad(x, y, w, h, {0, 0}, {1, 1}, col, col, col, col, vec4(radius, 3, t, 0), m_white, 1.0f);
}

void UI::glass(float x, float y, float w, float h, uint32_t tint, float radius) {
    quad(x, y, w, h, {0, 0}, {1, 1}, tint, tint, tint, tint, vec4(radius, 4, 0, 0), m_white, 1.0f);
}

void UI::shadow(float x, float y, float w, float h, float radius, float soft, uint32_t col) {
    quad(x, y, w, h, {0, 0}, {1, 1}, col, col, col, col, vec4(radius, 5, 0, soft), m_white, soft * 1.5f);
}

void UI::image(GLuint tex, float x, float y, float w, float h, uint32_t tint, vec2 uv0, vec2 uv1, float radius) {
    quad(x, y, w, h, uv0, uv1, tint, tint, tint, tint, vec4(radius, 0, 0, 0), tex, 0);
}

void UI::imageUV(GLuint tex, float x, float y, float w, float h, const vec2 uv[4], uint32_t tint, float radius) {
    size_t base = m_verts.size();
    quad(x, y, w, h, {0, 0}, {1, 1}, tint, tint, tint, tint, vec4(radius, 0, 0, 0), tex, 0);
    // quad() emits TL, TR, BR, TL, BR, BL.
    const int map[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) m_verts[base + i].uv = uv[map[i]];
}

void UI::line(float x0, float y0, float x1, float y1, float t, uint32_t col) {
    vec2 d = normalize(vec2(x1 - x0, y1 - y0)) * (t * 0.5f);
    vec2 n(-d.y, d.x);
    setTex(m_white);
    V v[4];
    vec2 p[4] = {vec2(x0, y0) + n, vec2(x1, y1) + n, vec2(x1, y1) - n, vec2(x0, y0) - n};
    for (int i = 0; i < 4; i++) {
        v[i].pos = p[i];
        v[i].uv = {0.5f, 0.5f};
        v[i].color = col;
        v[i].rect = vec4(0, 0, 0, 0);
        v[i].params = vec4(0, 0, 0, 0);
    }
    m_verts.insert(m_verts.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
}

void UI::circle(float cx, float cy, float r, uint32_t col) { rect(cx - r, cy - r, r * 2, r * 2, col, r); }
void UI::ring(float cx, float cy, float r, float t, uint32_t col) { border(cx - r, cy - r, r * 2, r * 2, col, r, t); }

float UI::textWidth(Font& f, const std::string& s, float size, float spacing) {
    float k = size / f.basePx, w = 0;
    for (char32_t cp : utf8to32(s)) {
        const Glyph* g = f.get(cp);
        if (g) w += g->advance * k + spacing;
    }
    return std::max(0.0f, w - spacing);
}

float UI::text(Font& f, const std::string& s, float x, float y, float size, uint32_t col, int align, float spacing, float bold) {
    float w = textWidth(f, s, size, spacing);
    if (align == ALIGN_CENTER) x -= w * 0.5f;
    else if (align == ALIGN_RIGHT) x -= w;
    float k = size / f.basePx;
    float baseline = y + f.ascent * k;
    float pen = x;
    for (char32_t cp : utf8to32(s)) {
        const Glyph* g = f.get(cp);
        if (!g) continue;
        if (g->w > 0) {
            float gx = pen + g->xoff * k, gy = baseline + g->yoff * k;
            quad(gx, gy, g->w * k, g->h * k, {g->u0, g->v0}, {g->u1, g->v1}, col, col, col, col, vec4(0, 1, bold, 0), f.tex, 0);
        }
        pen += g->advance * k + spacing;
    }
    return w;
}

float UI::textShadowed(Font& f, const std::string& s, float x, float y, float size, uint32_t col, int align, float spacing) {
    float w = textWidth(f, s, size, spacing);
    float ax = align == ALIGN_CENTER ? x - w * 0.5f : align == ALIGN_RIGHT ? x - w : x;
    float k = size / f.basePx;
    float baseline = y + f.ascent * k + size * 0.05f;
    float pen = ax + size * 0.04f;
    uint32_t sh = rgba(0, 0, 0, (int)(170 * ((col >> 24) / 255.0f)));
    for (char32_t cp : utf8to32(s)) {
        const Glyph* g = f.get(cp);
        if (!g) continue;
        if (g->w > 0)
            quad(pen + g->xoff * k, baseline + g->yoff * k, g->w * k, g->h * k, {g->u0, g->v0}, {g->u1, g->v1}, sh, sh, sh, sh,
                 vec4(0, 1, 0.08f, 0.25f), f.tex, 0);
        pen += g->advance * k + spacing;
    }
    text(f, s, x, y, size, col, align, spacing, 0.0f);
    return w;
}
