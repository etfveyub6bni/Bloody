#include "gfx/renderer.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "assets/textures.h"
#include "gfx/model_shaders.h"
#include "gfx/shaders.h"

vec3 Environment::skyRadiance(vec3 d) const {
    float h = d.z;
    vec3 col = lerp(skyHorizon, skyZenith, std::sqrt(saturate(h)));
    col = lerp(col, skyHorizon * 1.08f, std::exp(-std::fabs(h) * 16.0f) * 0.45f);
    col = lerp(col, groundColor, smooth01(-h / 0.3f));
    float m = std::max(dot(d, sunDir), 0.0f);
    col += sunColor * (0.018f * std::pow(m, 5.0f) + 0.05f * std::pow(m, 40.0f) + 0.16f * std::pow(m, 500.0f));
    return col;
}

static std::string src(std::initializer_list<const char*> parts) {
    std::string s = glsl::kVersion;
    for (const char* p : parts) s += p;
    return s;
}

namespace {
enum { DRAW_SHADED = 0, DRAW_SHADOW, DRAW_DEPTH };
constexpr int kUnitShadow = 5, kUnitShadowRaw = 6, kUnitSSAO = 7;

int debugView() {
    static int v = -1;
    if (v < 0) {
        const char* e = std::getenv("CS2P_GFX_DEBUG");
        v = !e ? 0 : std::strcmp(e, "ssao") == 0 ? 1 : std::strcmp(e, "shafts") == 0 ? 2 : std::strcmp(e, "layers") == 0 ? 3 : 0;
    }
    return v;
}
}  // namespace

bool Renderer::init() {
    using namespace glsl;
    bool ok = true;
    ok &= m_world.build(src({kWorldVS}), src({kNoise, kLighting, kWorldFS}), "world");
    ok &= m_model.build(src({kModelVS}), src({kNoise, kLighting, kModelFS}), "model");
    ok &= m_shadowWorld.build(src({kShadowWorldVS}), src({kEmptyFS}), "shadow_world");
    ok &= m_shadowModel.build(src({kShadowModelVS}), src({kEmptyFS}), "shadow_model");
    ok &= m_sky.build(src({kFullscreenVS}), src({kNoise, kLighting, kSkyFS}), "sky");
    ok &= m_sprite.build(src({kSpriteVS}), src({kNoise, "uniform float uTime;\n", kSpriteFS}), "sprite");
    ok &= m_bloomDown.build(src({kFullscreenVS}), src({kBloomDownFS}), "bloom_down");
    ok &= m_bloomUp.build(src({kFullscreenVS}), src({kBloomUpFS}), "bloom_up");
    ok &= m_composite.build(src({kFullscreenVS}), src({kCompositeFS}), "composite");
    ok &= m_final.build(src({kFullscreenVS}), src({kNoise, kFinalFS}), "final");
    ok &= m_copy.build(src({kFullscreenVS}), src({kCopyFS}), "copy");
    ok &= m_ssao.build(src({kFullscreenVS}), src({kNoise, kSSAOFS}), "ssao");
    ok &= m_ssaoBlur.build(src({kFullscreenVS}), src({kSSAOBlurFS}), "ssao_blur");
    ok &= m_shaftMask.build(src({kFullscreenVS}), src({kShaftMaskFS}), "shaft_mask");
    ok &= m_shaftBlur.build(src({kFullscreenVS}), src({kNoise, kShaftBlurFS}), "shaft_blur");
    ok &= m_flareVis.build(src({kFullscreenVS}), src({kFlareVisFS}), "flare_vis");
    glGenVertexArrays(1, &m_emptyVao);

    glGenVertexArrays(1, &m_spriteVao);
    glBindVertexArray(m_spriteVao);
    glGenBuffers(1, &m_spriteVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_spriteVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(SpriteVertex) * 4 * kMaxSprites, nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, params));
    std::vector<uint32_t> qi(kMaxSprites * 6);
    for (uint32_t i = 0; i < (uint32_t)kMaxSprites; i++) {
        uint32_t b = i * 4;
        uint32_t* q = &qi[i * 6];
        q[0] = b; q[1] = b + 1; q[2] = b + 2; q[3] = b; q[4] = b + 2; q[5] = b + 3;
    }
    glGenBuffers(1, &m_spriteIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_spriteIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, qi.size() * sizeof(uint32_t), qi.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    // Two views of the shadow array: hardware comparison for PCF, raw depth for the PCSS blocker search.
    glGenSamplers(1, &m_sampCmp);
    glGenSamplers(1, &m_sampRaw);
    for (GLuint s : {m_sampCmp, m_sampRaw}) {
        bool cmp = s == m_sampCmp;
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, cmp ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, cmp ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, cmp ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
    const float zero[4] = {0, 0, 0, 0};
    m_black = makeTexture2D(1, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, zero, false, GL_NEAREST, GL_CLAMP_TO_EDGE);
    return ok;
}

void Renderer::shutdown() {
    m_worldMesh.destroy();
    for (auto& b : m_bloom) b.destroy();
    for (RenderTarget* t : {&m_hdrMS, &m_hdr, &m_post, &m_ldr, &m_blurA, &m_blurB, &m_depth, &m_ssaoA, &m_ssaoB, &m_shaftA, &m_shaftB, &m_flare})
        t->destroy();
    if (m_shadowTex) glDeleteTextures(1, &m_shadowTex);
    if (m_shadowFbo) glDeleteFramebuffers(1, &m_shadowFbo);
    if (m_sampCmp) glDeleteSamplers(1, &m_sampCmp);
    if (m_sampRaw) glDeleteSamplers(1, &m_sampRaw);
    if (m_black) glDeleteTextures(1, &m_black);
    if (m_albedoArr) glDeleteTextures(1, &m_albedoArr);
    if (m_normalArr) glDeleteTextures(1, &m_normalArr);
    m_shadowTex = m_shadowFbo = m_sampCmp = m_sampRaw = m_black = m_albedoArr = m_normalArr = 0;
}

void Renderer::configure(int outW, int outH, const RenderSettings& in) {
    RenderSettings rs = in;
    rs.renderScale = clampf(rs.renderScale, 0.25f, 1.0f);
    rs.shadows = std::max(0, std::min(rs.shadows, 3));
    rs.ssao = std::max(0, std::min(rs.ssao, 2));
    rs.textureQuality = std::max(0, std::min(rs.textureQuality, 2));
    rs.anisotropy = std::max(1, std::min(rs.anisotropy, 16));
    outW = std::max(outW, 16);
    outH = std::max(outH, 16);
    bool targets = !m_configured || outW != m_outW || outH != m_outH || rs.renderScale != m_rs.renderScale ||
                   rs.msaa != m_rs.msaa || (rs.ssao > 0) != (m_rs.ssao > 0) || rs.sunShafts != m_rs.sunShafts ||
                   rs.lensFlare != m_rs.lensFlare;
    bool shadows = !m_configured || rs.shadows != m_rs.shadows;
    m_rs = rs;
    if (targets) {
        m_outW = outW;
        m_outH = outH;
        m_w = std::max(16, (int)(outW * rs.renderScale));
        m_h = std::max(16, (int)(outH * rs.renderScale));
        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        int samples = std::min(rs.msaa, (int)maxSamples);
        if (samples < 2) samples = 0;
        // Fall back to fewer samples if the driver rejects the combination.
        while (!m_hdrMS.create(m_w, m_h, GL_RGBA16F, true, samples) && samples > 0) samples = samples > 2 ? samples / 2 : 0;
        if (samples > 0) m_hdr.create(m_w, m_h, GL_RGBA16F, false);
        else m_hdr.destroy();
        for (auto& b : m_bloom) b.destroy();
        m_bloom.clear();
        int bw = m_w / 2, bh = m_h / 2;
        for (int i = 0; i < 6 && bw >= 4 && bh >= 4; i++) {
            RenderTarget t;
            t.create(bw, bh, GL_RGBA16F, false);
            m_bloom.push_back(t);
            bw /= 2;
            bh /= 2;
        }
        m_post.create(m_w, m_h, GL_RGBA8, false);
        m_ldr.create(outW, outH, GL_RGBA8, false);
        m_blurA.create(std::max(4, outW / 4), std::max(4, outH / 4), GL_RGBA8, false);
        m_blurB.create(std::max(4, outW / 8), std::max(4, outH / 8), GL_RGBA8, false);
        if (rs.ssao > 0 || rs.sunShafts) {
            m_depth.create(m_w, m_h, 0, true, 0, true);
            glBindTexture(GL_TEXTURE_2D, m_depth.depth);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else {
            m_depth.destroy();
        }
        if (rs.ssao > 0) {
            m_ssaoA.create(std::max(8, m_w / 2), std::max(8, m_h / 2), GL_RG16F, false);
            m_ssaoB.create(std::max(8, m_w / 2), std::max(8, m_h / 2), GL_RG16F, false);
        } else {
            m_ssaoA.destroy();
            m_ssaoB.destroy();
        }
        if (rs.sunShafts) {
            m_shaftA.create(std::max(8, m_w / 4), std::max(8, m_h / 4), GL_RGBA16F, false);
            m_shaftB.create(std::max(8, m_w / 4), std::max(8, m_h / 4), GL_RGBA16F, false);
        } else {
            m_shaftA.destroy();
            m_shaftB.destroy();
        }
        if (rs.lensFlare) m_flare.create(1, 1, GL_RGBA16F, false);
        else m_flare.destroy();
    }
    if (shadows) createShadowMaps();
    if (rs.textureQuality != m_texQuality || rs.anisotropy != m_texAniso || !m_ownTextures) updateWorldTextures();
    m_configured = true;
}

void Renderer::createShadowMaps() {
    if (m_shadowTex) glDeleteTextures(1, &m_shadowTex);
    if (m_shadowFbo) glDeleteFramebuffers(1, &m_shadowFbo);
    m_shadowTex = m_shadowFbo = 0;
    m_cascades = m_shadowSize = 0;
    m_shadowQuality = m_rs.shadows;
    m_shadowsReady = false;
    if (m_rs.shadows <= 0) return;
    int cascades = m_rs.shadows >= 3 ? 4 : 3;
    int size = m_rs.shadows == 1 ? 1024 : 2048;
    glGenTextures(1, &m_shadowTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, size, size, cascades, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &m_shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowTex, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        logError("Shadow map framebuffer incomplete (0x%x), shadows disabled", st);
        glDeleteTextures(1, &m_shadowTex);
        glDeleteFramebuffers(1, &m_shadowFbo);
        m_shadowTex = m_shadowFbo = 0;
        return;
    }
    m_cascades = cascades;
    m_shadowSize = size;
    m_shadowDirty = true;
}

void Renderer::updateWorldTextures() {
    bool hasAniso = GLAD_GL_EXT_texture_filter_anisotropic || GLAD_GL_ARB_texture_filter_anisotropic;
    float aniso = std::max(1.0f, std::min((float)m_rs.anisotropy, maxAnisotropy()));
    if (m_rs.textureQuality != m_texQuality || !m_ownTextures) {
        auto t0 = std::chrono::steady_clock::now();
        int size = worldTextureSize(m_rs.textureQuality);
        WorldTextureSet ts;
        generateWorldTextures(ts, size);
        std::vector<const void*> al, nl;
        for (int i = 0; i < MAT_COUNT; i++) {
            al.push_back(ts.albedo[i].data());
            nl.push_back(ts.normal[i].data());
        }
        GLuint a = makeTextureArray(size, size, MAT_COUNT, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, al, aniso);
        GLuint n = makeTextureArray(size, size, MAT_COUNT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nl, aniso);
        if (m_albedoArr) glDeleteTextures(1, &m_albedoArr);
        if (m_normalArr) glDeleteTextures(1, &m_normalArr);
        m_albedoArr = a;
        m_normalArr = n;
        m_ownTextures = true;
        m_texQuality = m_rs.textureQuality;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        logInfo("World textures: %d layers at %dx%d, anisotropy %.0f, %.0f ms", (int)MAT_COUNT, size, size, aniso, ms);
    } else if (hasAniso) {
        for (GLuint t : {m_albedoArr, m_normalArr}) {
            glBindTexture(GL_TEXTURE_2D_ARRAY, t);
            glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, aniso);
        }
    }
    m_texAniso = m_rs.anisotropy;
}

void Renderer::setWorldTextures(GLuint albedoArray, GLuint normalArray) {
    if (m_ownTextures) {
        if (albedoArray && albedoArray != m_albedoArr) glDeleteTextures(1, &albedoArray);
        if (normalArray && normalArray != m_normalArr) glDeleteTextures(1, &normalArray);
        return;
    }
    m_albedoArr = albedoArray;
    m_normalArr = normalArray;
}

void Renderer::setWorldGeometry(const std::vector<WorldVertex>& v, const std::vector<uint32_t>& idx,
                                const std::vector<WorldChunk>& chunks, const AABB& bounds) {
    m_worldMesh.destroy();
    std::vector<VertexAttrib> at = {
        {3, GL_FLOAT, false, offsetof(WorldVertex, pos)},     {3, GL_FLOAT, false, offsetof(WorldVertex, normal)},
        {2, GL_FLOAT, false, offsetof(WorldVertex, uv)},      {4, GL_FLOAT, false, offsetof(WorldVertex, tangent)},
        {4, GL_FLOAT, false, offsetof(WorldVertex, light)},   {4, GL_UNSIGNED_BYTE, true, offsetof(WorldVertex, tint)},
    };
    m_worldMesh = buildMesh(v.data(), v.size() * sizeof(WorldVertex), sizeof(WorldVertex), at, idx.data(), idx.size());
    m_chunks = chunks;
    m_worldBounds = bounds;
    // Desert maps (with sand floors) get sand on their ledges.
    m_worldHasSand = false;
    for (const auto& wv : v)
        if ((wv.tint >> 24) == (uint32_t)MAT_SAND) {
            m_worldHasSand = true;
            break;
        }
    m_shadowDirty = true;
}

void Renderer::clearWorld() {
    m_worldMesh.destroy();
    m_chunks.clear();
    m_worldBounds = AABB();
}

GpuMesh Renderer::createModelMesh(const std::vector<ModelVertex>& v, const std::vector<uint32_t>& idx) {
    std::vector<VertexAttrib> at = {
        {3, GL_FLOAT, false, offsetof(ModelVertex, pos)},
        {3, GL_FLOAT, false, offsetof(ModelVertex, normal)},
        {4, GL_UNSIGNED_BYTE, true, offsetof(ModelVertex, color)},
        {4, GL_UNSIGNED_BYTE, true, offsetof(ModelVertex, mat)},
    };
    return buildMesh(v.data(), v.size() * sizeof(ModelVertex), sizeof(ModelVertex), at, idx.data(), idx.size());
}

void Renderer::fullscreen() {
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    drawCalls++;
}

void Renderer::applyCommon(Shader& s, const Camera& cam, const FrameInput& in, bool withShadow, bool withSSAO) {
    s.set("uCamPos", cam.pos);
    s.set("uSunDir", m_env.sunDir);
    s.set("uSunColor", m_env.sunColor);
    s.set("uSkyZenith", m_env.skyZenith);
    s.set("uSkyHorizon", m_env.skyHorizon);
    s.set("uGroundColor", m_env.groundColor);
    s.set("uFogColor", m_env.fogColor);
    s.set("uFogDensity", m_env.fogDensity);
    s.set("uFogParams", vec4(m_env.fogFalloff, m_env.fogBaseHeight, m_env.fogSunScatter, m_env.fogMaxOpacity));
    vec3 skyAvg = (m_env.skyZenith + m_env.skyHorizon) * 0.5f * m_env.skyIntensity;
    s.set("uSkyLum", dot(skyAvg, vec3(0.3f, 0.59f, 0.11f)));
    s.set("uTime", in.time);
    bool sh = withShadow && m_shadowsReady;
    glActiveTexture(GL_TEXTURE0 + kUnitShadow);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sh ? m_shadowTex : 0);
    glBindSampler(kUnitShadow, m_sampCmp);
    glActiveTexture(GL_TEXTURE0 + kUnitShadowRaw);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sh ? m_shadowTex : 0);
    glBindSampler(kUnitShadowRaw, m_sampRaw);
    s.set("uShadowMap", kUnitShadow);
    s.set("uShadowDepth", kUnitShadowRaw);
    s.setMats("uShadowMats", m_cascadeMat, kMaxCascades);
    s.setVec4s("uCascadeInfo", m_cascadeInfo, kMaxCascades);
    s.set("uShadowParams", vec4((float)m_cascades, sh ? 1.0f : 0.0f, m_shadowSize > 0 ? 1.0f / m_shadowSize : 0.0f, (float)m_shadowQuality));
    bool ao = withSSAO && m_ssaoReady;
    glActiveTexture(GL_TEXTURE0 + kUnitSSAO);
    glBindTexture(GL_TEXTURE_2D, ao ? m_ssaoA.color : m_black);
    s.set("uSSAO", kUnitSSAO);
    s.set("uSSAOParams", vec4(1.0f / m_w, 1.0f / m_h, ao ? 1.0f : 0.0f, 0.0f));
    int n = 0;
    vec4 lp[4], lc[4];
    if (in.lights)
        for (const auto& l : *in.lights) {
            if (n >= 4) break;
            lp[n] = vec4(l.pos, l.radius);
            lc[n] = vec4(l.color, 0);
            n++;
        }
    s.set("uLightCount", n);
    if (n) {
        s.setVec4s("uLightPos", lp, n);
        s.setVec4s("uLightColor", lc, n);
    }
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::setupWorldMaterials(Shader& s) {
    vec4 params[16];
    for (int i = 0; i < 16; i++) {
        if (i < MAT_COUNT) {
            const MaterialInfo& mi = materialInfo(i);
            params[i] = vec4(mi.uvScale, mi.antiTile, mi.macro, mi.grime);
        } else {
            params[i] = vec4(1, 0, 0, 0);
        }
    }
    s.setVec4s("uLayerParams", params, 16);
    s.set("uReveal", vec4((float)MAT_PLASTER, (float)MAT_SANDSTONE, 1.0f, 0.0f));
    int dustMask = (1 << MAT_SANDSTONE) | (1 << MAT_PLASTER) | (1 << MAT_TRIM) | (1 << MAT_BRICK) | (1 << MAT_METAL);
    s.set("uDust", vec4((float)MAT_SAND, m_worldHasSand ? (float)dustMask : 0.0f, 0.0f, 0.0f));
    s.set("uDebugLayers", debugView() == 3 ? 1.0f : 0.0f);
}

void Renderer::drawWorld(Shader& s, const Frustum* fr) {
    (void)s;
    if (!m_worldMesh.vao) return;
    glBindVertexArray(m_worldMesh.vao);
    for (const auto& c : m_chunks) {
        if (fr && !fr->visible(c.bounds)) continue;
        glDrawElements(GL_TRIANGLES, (GLsizei)c.count, GL_UNSIGNED_INT, (const void*)(sizeof(uint32_t) * (size_t)c.first));
        drawCalls++;
    }
}

void Renderer::drawModels(Shader& s, const std::vector<ModelDraw>& list, int mode) {
    static const mat4 kIdentity[1] = {mat4()};
    for (const auto& d : list) {
        if (!d.mesh || !d.mesh->vao) continue;
        if (mode == DRAW_SHADOW && !d.castShadow) continue;
        s.set("uModel", d.model);
        if (d.bones && d.boneCount > 0) s.setMats("uBones", d.bones, std::min(d.boneCount, 48));
        else s.setMats("uBones", kIdentity, 1);
        if (mode == DRAW_SHADED) {
            s.set("uTint", d.tint);
            s.set("uAmbUp", d.ambUp);
            s.set("uAmbDown", d.ambDown);
            s.set("uPatternA", d.patternA);
            s.set("uPatternB", d.patternB);
        }
        d.mesh->draw();
        drawCalls++;
    }
}

void Renderer::computeCascades(const Camera& cam) {
    vec3 L = m_env.sunDir;
    vec3 up = std::fabs(L.z) > 0.95f ? vec3(1, 0, 0) : vec3(0, 0, 1);
    mat4 lightView = lookAt(vec3(0.0f), -L, up);
    // Depth range spans the whole map so every caster lands in every cascade.
    vec3 mn(1e30f), mx(-1e30f);
    for (int i = 0; i < 8; i++) {
        vec3 p((i & 1) ? m_worldBounds.mx.x : m_worldBounds.mn.x, (i & 2) ? m_worldBounds.mx.y : m_worldBounds.mn.y,
               (i & 4) ? m_worldBounds.mx.z + 256.0f : m_worldBounds.mn.z);
        vec3 v = xformPoint(lightView, p);
        mn = vmin(mn, v);
        mx = vmax(mx, v);
    }
    float zn = -mx.z - 64.0f, zf = -mn.z + 64.0f;
    const float split4[4] = {0, 220.0f, 700.0f, 1900.0f};
    const float split3[3] = {0, 420.0f, 1500.0f};
    const float* splits = m_cascades >= 4 ? split4 : split3;
    const mat4 bias = translate(vec3(0.5f)) * scale(vec3(0.5f));
    for (int i = 0; i < m_cascades; i++) {
        float cx, cy, radius;
        if (i == m_cascades - 1 || cam.ortho) {
            // Last cascade: the whole map.
            cx = (mn.x + mx.x) * 0.5f;
            cy = (mn.y + mx.y) * 0.5f;
            radius = std::max(mx.x - mn.x, mx.y - mn.y) * 0.5f + 32.0f;
        } else {
            // Bounding sphere of the view-frustum slice: rotation-invariant size, so no shimmering.
            float d0 = i == 0 ? cam.znear : splits[i], d1 = splits[i + 1];
            float ty = std::tan(cam.fovY * 0.5f), tx = ty * cam.aspect;
            vec3 corners[8], centre(0.0f);
            for (int k = 0; k < 8; k++) {
                float d = (k & 4) ? d1 : d0;
                corners[k] = cam.pos + cam.fwd * d + cam.left * (tx * d * ((k & 1) ? 1.0f : -1.0f)) +
                             cam.up * (ty * d * ((k & 2) ? 1.0f : -1.0f));
                centre += corners[k];
            }
            centre = centre / 8.0f;
            float r = 0;
            for (auto& c : corners) r = std::max(r, length(c - centre));
            radius = std::ceil(r / 16.0f) * 16.0f;
            vec3 lc = xformPoint(lightView, centre);
            cx = lc.x;
            cy = lc.y;
        }
        // Snap to whole shadow texels so edges stay put while the camera moves.
        float texel = 2.0f * radius / (float)m_shadowSize;
        cx = std::floor(cx / texel) * texel;
        cy = std::floor(cy / texel) * texel;
        m_cascadeVP[i] = ortho(cx - radius, cx + radius, cy - radius, cy + radius, zn, zf) * lightView;
        m_cascadeMat[i] = bias * m_cascadeVP[i];
        m_cascadeInfo[i] = vec4(texel, zf - zn, 0.0f, 0.0f);
    }
}

void Renderer::renderShadowMaps(const FrameInput& in) {
    if (!m_shadowTex || !m_worldBounds.valid()) return;
    computeCascades(in.cam);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glViewport(0, 0, m_shadowSize, m_shadowSize);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.6f, 2.0f);
    for (int i = 0; i < m_cascades; i++) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowTex, 0, i);
        glClear(GL_DEPTH_BUFFER_BIT);
        Frustum fr;
        fr.fromMatrix(m_cascadeVP[i]);
        m_shadowWorld.use();
        m_shadowWorld.set("uViewProj", m_cascadeVP[i]);
        drawWorld(m_shadowWorld, &fr);
        if (in.models && !in.models->empty()) {
            m_shadowModel.use();
            m_shadowModel.set("uViewProj", m_cascadeVP[i]);
            drawModels(m_shadowModel, *in.models, DRAW_SHADOW);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    m_shadowsReady = true;
}

void Renderer::renderDepthPrepass(const FrameInput& in, const Frustum& fr) {
    m_depth.bind();
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    if (in.drawWorld) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        m_shadowWorld.use();
        m_shadowWorld.set("uViewProj", in.cam.viewProj);
        drawWorld(m_shadowWorld, &fr);
        glDisable(GL_CULL_FACE);
    }
    if (in.models && !in.models->empty()) {
        m_shadowModel.use();
        m_shadowModel.set("uViewProj", in.cam.viewProj);
        drawModels(m_shadowModel, *in.models, DRAW_DEPTH);
    }
    // Viewmodel depth (own projection) only blocks sun shafts; SSAO ignores samples that far off in depth.
    if (in.drawViewmodel && in.vmModels && !in.vmModels->empty()) {
        glDepthFunc(GL_ALWAYS);
        m_shadowModel.use();
        m_shadowModel.set("uViewProj", in.vmCam.viewProj);
        drawModels(m_shadowModel, *in.vmModels, DRAW_DEPTH);
        glDepthFunc(GL_LESS);
    }
}

void Renderer::renderSSAO(const Camera& cam) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    m_ssaoA.bind();
    m_ssao.use();
    glBindTexture(GL_TEXTURE_2D, m_depth.depth);
    m_ssao.set("uDepth", 0);
    float ty = std::tan(cam.fovY * 0.5f);
    m_ssao.set("uProj", vec4(ty * cam.aspect, ty, cam.znear, cam.zfar));
    bool high = m_rs.ssao >= 2;
    // radius (units), intensity, samples, spiral turns (coprime with the sample count)
    m_ssao.set("uAO", high ? vec4(40.0f, 1.7f, 16.0f, 7.0f) : vec4(32.0f, 1.5f, 10.0f, 3.0f));
    m_ssao.set("uTexel", vec2(1.0f / m_w, 1.0f / m_h));
    fullscreen();
    m_ssaoBlur.use();
    m_ssaoBlur.set("uSrc", 0);
    m_ssaoB.bind();
    glBindTexture(GL_TEXTURE_2D, m_ssaoA.color);
    m_ssaoBlur.set("uDir", vec2(1.0f / m_ssaoA.w, 0.0f));
    fullscreen();
    m_ssaoA.bind();
    glBindTexture(GL_TEXTURE_2D, m_ssaoB.color);
    m_ssaoBlur.set("uDir", vec2(0.0f, 1.0f / m_ssaoA.h));
    fullscreen();
    m_ssaoReady = true;
}

void Renderer::renderSunShafts(const Camera& cam, vec2 sunUV) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    m_shaftA.bind();
    m_shaftMask.use();
    glBindTexture(GL_TEXTURE_2D, m_depth.depth);
    m_shaftMask.set("uDepth", 0);
    m_shaftMask.set("uInvViewProj", cam.invViewProj);
    m_shaftMask.set("uCamPos", cam.pos);
    m_shaftMask.set("uSunDir", m_env.sunDir);
    m_shaftMask.set("uTexel", vec2(1.0f / m_w, 1.0f / m_h));
    fullscreen();
    m_shaftBlur.use();
    m_shaftBlur.set("uSrc", 0);
    m_shaftBlur.set("uSunUV", sunUV);
    m_shaftB.bind();
    glBindTexture(GL_TEXTURE_2D, m_shaftA.color);
    m_shaftBlur.set("uParams", vec3(0.9f, 0.975f, 48.0f));
    fullscreen();
    // A second, shorter pass smooths the streaks.
    m_shaftA.bind();
    glBindTexture(GL_TEXTURE_2D, m_shaftB.color);
    m_shaftBlur.set("uParams", vec3(0.25f, 0.99f, 16.0f));
    fullscreen();
}

void Renderer::drawSprites(const std::vector<SpriteVertex>& verts, const Camera& cam, const FrameInput& in) {
    if (verts.empty()) return;
    m_sprite.use();
    m_sprite.set("uViewProj", cam.viewProj);
    m_sprite.set("uTime", in.time);
    m_sprite.set("uLitColor", in.spriteLight);
    glBindVertexArray(m_spriteVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_spriteVbo);
    size_t total = std::min(verts.size() / 4, (size_t)kMaxSprites);
    glBufferData(GL_ARRAY_BUFFER, sizeof(SpriteVertex) * 4 * kMaxSprites, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(total * 4 * sizeof(SpriteVertex)), verts.data());
    glDrawElements(GL_TRIANGLES, (GLsizei)(total * 6), GL_UNSIGNED_INT, nullptr);
    drawCalls++;
}

void Renderer::renderFrame(const FrameInput& in) {
    drawCalls = 0;
    const Camera& cam = in.cam;
    const bool persp = !cam.ortho;
    m_shadowsReady = false;
    m_ssaoReady = false;
    if (in.drawWorld && m_cascades > 0) renderShadowMaps(in);

    Frustum fr;
    fr.fromMatrix(cam.viewProj);
    // Sun on screen: drives shafts and lens flare.
    vec4 sc = cam.viewProj * vec4(cam.pos + m_env.sunDir * 10000.0f, 1.0f);
    bool sunAhead = persp && in.drawSky && sc.w > 0.0f;
    vec2 sunUV(0.5f);
    float shaftFade = 0.0f, flareFade = 0.0f;
    if (sunAhead) {
        sunUV = vec2(sc.x / sc.w * 0.5f + 0.5f, sc.y / sc.w * 0.5f + 0.5f);
        float edge = std::max(std::fabs(sunUV.x - 0.5f), std::fabs(sunUV.y - 0.5f));
        float facing = saturate((dot(cam.fwd, m_env.sunDir) - 0.1f) / 0.4f);
        shaftFade = facing * saturate(1.6f - 2.0f * edge);
        flareFade = saturate((0.56f - edge) / 0.08f);
    }
    bool doShafts = m_rs.sunShafts && m_depth.fbo && m_shaftA.fbo && shaftFade > 0.001f;
    bool doSSAO = m_rs.ssao > 0 && m_depth.fbo && m_ssaoA.fbo && persp && in.drawWorld;
    bool doFlare = m_rs.lensFlare && m_flare.fbo && flareFade > 0.001f;
    if (doShafts || doSSAO) renderDepthPrepass(in, fr);
    if (doSSAO) renderSSAO(cam);

    m_hdrMS.bind();
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    if (in.drawWorld) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        m_world.use();
        m_world.set("uViewProj", cam.viewProj);
        applyCommon(m_world, cam, in, true, true);
        setupWorldMaterials(m_world);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_albedoArr);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_normalArr);
        glActiveTexture(GL_TEXTURE0);
        m_world.set("uAlbedoTex", 0);
        m_world.set("uNormalTex", 1);
        drawWorld(m_world, &fr);
    }
    glDisable(GL_CULL_FACE);
    if (in.models && !in.models->empty()) {
        m_model.use();
        m_model.set("uViewProj", cam.viewProj);
        m_model.set("uIsViewmodel", 0.0f);
        applyCommon(m_model, cam, in, in.drawWorld, true);
        drawModels(m_model, *in.models, DRAW_SHADED);
    }
    // Sky last, only where nothing was drawn.
    glDepthMask(GL_FALSE);
    if (in.drawSky) {
        glDepthFunc(GL_LEQUAL);
        m_sky.use();
        applyCommon(m_sky, cam, in, false, false);
        m_sky.set("uInvViewProj", cam.invViewProj);
        m_sky.set("uCloudiness", m_env.cloudiness);
        fullscreen();
        glDepthFunc(GL_LESS);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (in.decals && !in.decals->empty()) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);
        drawSprites(*in.decals, cam, in);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (in.sprites) drawSprites(*in.sprites, cam, in);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    if (in.drawViewmodel && in.vmModels && !in.vmModels->empty()) {
        glClear(GL_DEPTH_BUFFER_BIT);
        m_model.use();
        m_model.set("uViewProj", in.vmCam.viewProj);
        m_model.set("uIsViewmodel", 1.0f);
        applyCommon(m_model, in.vmCam, in, in.drawWorld, false);
        drawModels(m_model, *in.vmModels, DRAW_SHADED);
        if (in.vmSprites && !in.vmSprites->empty()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            drawSprites(*in.vmSprites, in.vmCam, in);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }
    glBindSampler(kUnitShadow, 0);
    glBindSampler(kUnitShadowRaw, 0);

    // Resolve MSAA.
    GLuint hdrTex = m_hdrMS.color;
    if (m_hdrMS.samples > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_hdrMS.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_hdr.fbo);
        glBlitFramebuffer(0, 0, m_w, m_h, 0, 0, m_w, m_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        hdrTex = m_hdr.color;
    }
    glDisable(GL_DEPTH_TEST);

    if (doShafts) renderSunShafts(cam, sunUV);
    if (doFlare) {
        m_flare.bind();
        m_flareVis.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrTex);
        m_flareVis.set("uHdr", 0);
        m_flareVis.set("uSunUV", sunUV);
        float r = std::tan(1.2f * kDeg) / (2.0f * std::tan(cam.fovY * 0.5f));
        m_flareVis.set("uRadius", vec2(r / cam.aspect, r));
        fullscreen();
    }

    // Bloom: 13-tap downsample chain then tent upsample with additive blend.
    GLuint bloomTex = 0;
    if (m_rs.bloom && !m_bloom.empty()) {
        m_bloomDown.use();
        m_bloomDown.set("uSrc", 0);
        m_bloomDown.set("uThreshold", m_env.bloomThreshold);
        glActiveTexture(GL_TEXTURE0);
        GLuint srcTex = hdrTex;
        int sw = m_w, sh = m_h;
        for (size_t i = 0; i < m_bloom.size(); i++) {
            m_bloom[i].bind();
            glBindTexture(GL_TEXTURE_2D, srcTex);
            m_bloomDown.set("uTexel", vec2(1.0f / sw, 1.0f / sh));
            m_bloomDown.set("uPrefilter", i == 0 ? 1 : 0);
            fullscreen();
            srcTex = m_bloom[i].color;
            sw = m_bloom[i].w;
            sh = m_bloom[i].h;
        }
        m_bloomUp.use();
        m_bloomUp.set("uSrc", 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int i = (int)m_bloom.size() - 1; i > 0; i--) {
            m_bloom[i - 1].bind();
            glBindTexture(GL_TEXTURE_2D, m_bloom[i].color);
            m_bloomUp.set("uTexel", vec2(1.0f / m_bloom[i].w, 1.0f / m_bloom[i].h));
            fullscreen();
        }
        glDisable(GL_BLEND);
        bloomTex = m_bloom[0].color;
    }

    // HDR -> graded LDR at render resolution.
    m_post.bind();
    m_composite.use();
    const GLuint inputs[4] = {hdrTex, bloomTex ? bloomTex : m_black, doShafts ? m_shaftA.color : m_black, doFlare ? m_flare.color : m_black};
    for (int i = 3; i >= 0; i--) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, inputs[i]);
    }
    m_composite.set("uHdr", 0);
    m_composite.set("uBloom", 1);
    m_composite.set("uShafts", 2);
    m_composite.set("uFlareVis", 3);
    m_composite.set("uExposure", m_env.exposure * std::exp2(clampf(m_rs.brightness, -1.0f, 1.0f)));
    m_composite.set("uBloomStrength", bloomTex ? m_env.bloomStrength : 0.0f);
    m_composite.set("uSaturation", m_env.saturation * clampf(m_rs.saturation, 0.5f, 1.5f));
    m_composite.set("uContrast", m_env.contrast);
    m_composite.set("uGrade", m_env.grade);
    m_composite.set("uShadowTint", m_env.shadowTint);
    m_composite.set("uHighlightTint", m_env.highlightTint);
    m_composite.set("uShaftColor", doShafts ? m_env.sunColor * (m_env.shaftStrength * shaftFade) : vec3(0.0f));
    float sunLum = std::max(1e-3f, dot(m_env.sunColor, vec3(0.2126f, 0.7152f, 0.0722f)));
    m_composite.set("uSun", vec4(sunUV.x, sunUV.y, doFlare ? m_env.flareStrength * flareFade : 0.0f, (float)m_w / (float)m_h));
    m_composite.set("uSunTint", m_env.sunColor / sunLum);
    m_composite.set("uDesaturate", in.fx.desaturate);
    int dbg = debugView();
    m_composite.set("uDebugView", dbg);
    m_composite.set("uDebugTex", 4);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, dbg == 1 && m_ssaoReady ? m_ssaoA.color : m_black);
    glActiveTexture(GL_TEXTURE0);
    fullscreen();

    // Anti-aliasing, sharpening and lens/film effects at output resolution.
    m_ldr.bind();
    m_final.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_post.color);
    m_final.set("uSrc", 0);
    m_final.set("uTexel", vec2(1.0f / m_post.w, 1.0f / m_post.h));
    m_final.set("uFxaa", m_rs.fxaa ? 1.0f : 0.0f);
    m_final.set("uSharpen", persp ? clampf(m_rs.sharpen, 0.0f, 1.0f) : 0.0f);
    m_final.set("uChromatic", persp && m_rs.chromatic ? 1.0f : 0.0f);
    m_final.set("uVignette", persp && m_rs.vignette ? m_env.vignette : 0.0f);
    m_final.set("uGrain", persp && m_rs.filmGrain ? 1.0f : 0.0f);
    m_final.set("uFlash", in.fx.flash);
    m_final.set("uDamage", in.fx.damage);
    m_final.set("uTime", in.time);
    fullscreen();
    glBindVertexArray(0);
}

void Renderer::updateBlur() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    m_bloomDown.use();
    m_bloomDown.set("uSrc", 0);
    m_bloomDown.set("uPrefilter", 0);
    glActiveTexture(GL_TEXTURE0);
    m_blurA.bind();
    glBindTexture(GL_TEXTURE_2D, m_ldr.color);
    m_bloomDown.set("uTexel", vec2(2.0f / m_ldr.w, 2.0f / m_ldr.h));
    fullscreen();
    m_blurB.bind();
    glBindTexture(GL_TEXTURE_2D, m_blurA.color);
    m_bloomDown.set("uTexel", vec2(1.0f / m_blurA.w, 1.0f / m_blurA.h));
    fullscreen();
    m_bloomUp.use();
    m_bloomUp.set("uSrc", 0);
    m_blurA.bind();
    glBindTexture(GL_TEXTURE_2D, m_blurB.color);
    m_bloomUp.set("uTexel", vec2(1.0f / m_blurB.w, 1.0f / m_blurB.h));
    fullscreen();
}

void Renderer::presentToScreen(int winW, int winH) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_ldr.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, m_ldr.w, m_ldr.h, 0, 0, winW, winH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint Renderer::captureLdr(int x, int y, int w, int h, bool mips) {
    GLuint tex = makeTexture2D(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, false, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_ldr.fbo);
    glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, w, h);
    if (mips) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return tex;
}
