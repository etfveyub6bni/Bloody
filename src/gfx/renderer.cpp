#include "gfx/renderer.h"

#include <cstddef>

#include "gfx/model_shaders.h"
#include "gfx/shaders.h"

vec3 Environment::skyRadiance(vec3 d) const {
    float h = d.z;
    vec3 col = lerp(skyHorizon, skyZenith, std::pow(saturate(h), 0.55f));
    col = lerp(col, groundColor, smooth01(-h / 0.3f));
    float sd = std::max(dot(d, sunDir), 0.0f);
    col += sunColor * (0.018f * std::pow(sd, 6.0f) + 0.05f * std::pow(sd, 48.0f));
    return col;
}

static std::string src(std::initializer_list<const char*> parts) {
    std::string s = glsl::kVersion;
    for (const char* p : parts) s += p;
    return s;
}

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
    ok &= m_composite.build(src({kFullscreenVS}), src({kNoise, kCompositeFS}), "composite");
    ok &= m_copy.build(src({kFullscreenVS}), src({kCopyFS}), "copy");
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
    return ok;
}

void Renderer::shutdown() {
    m_worldMesh.destroy();
    for (auto& b : m_bloom) b.destroy();
    m_hdrMS.destroy(); m_hdr.destroy(); m_ldr.destroy(); m_blurA.destroy(); m_blurB.destroy(); m_shadow.destroy();
}

void Renderer::configure(int outW, int outH, const RenderSettings& rs) {
    outW = std::max(outW, 16);
    outH = std::max(outH, 16);
    if (m_configured && outW == m_outW && outH == m_outH && rs == m_rs) return;
    bool shadowChanged = !m_configured || rs.shadowSize != m_rs.shadowSize;
    m_rs = rs;
    m_outW = outW;
    m_outH = outH;
    m_w = std::max(16, (int)(outW * rs.renderScale));
    m_h = std::max(16, (int)(outH * rs.renderScale));
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    int samples = std::min(rs.msaa, (int)maxSamples);
    if (samples < 2) samples = 0;
    m_hdrMS.create(m_w, m_h, GL_RGBA16F, true, samples);
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
    m_ldr.create(outW, outH, GL_RGBA8, false);
    m_blurA.create(std::max(4, outW / 4), std::max(4, outH / 4), GL_RGBA8, false);
    m_blurB.create(std::max(4, outW / 8), std::max(4, outH / 8), GL_RGBA8, false);
    if (shadowChanged) {
        m_shadow.destroy();
        if (rs.shadowSize > 0) m_shadow.create(rs.shadowSize, rs.shadowSize, 0, true, 0, true);
        m_shadowDirty = true;
    }
    m_configured = true;
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

void Renderer::applyCommon(Shader& s, const Camera& cam, const FrameInput& in, bool withShadow) {
    s.set("uCamPos", cam.pos);
    s.set("uSunDir", m_env.sunDir);
    s.set("uSunColor", m_env.sunColor);
    s.set("uSkyZenith", m_env.skyZenith);
    s.set("uSkyHorizon", m_env.skyHorizon);
    s.set("uGroundColor", m_env.groundColor);
    s.set("uFogColor", m_env.fogColor);
    s.set("uFogDensity", m_env.fogDensity);
    vec3 skyAvg = (m_env.skyZenith + m_env.skyHorizon) * 0.5f * m_env.skyIntensity;
    s.set("uSkyLum", dot(skyAvg, vec3(0.3f, 0.59f, 0.11f)));
    s.set("uTime", in.time);
    bool sh = withShadow && m_shadow.fbo && m_rs.shadowSize > 0;
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, sh ? m_shadow.depth : 0);
    s.set("uShadowMap", 5);
    s.set("uShadowMat", m_shadowMat);
    s.set("uShadowParams", vec2(sh ? 1.0f / m_rs.shadowSize : 0.0f, sh ? 1.0f : 0.0f));
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

void Renderer::drawWorld(Shader& s, const Frustum* fr) {
    if (!m_worldMesh.vao) return;
    glBindVertexArray(m_worldMesh.vao);
    for (const auto& c : m_chunks) {
        if (fr && !fr->visible(c.bounds)) continue;
        glDrawElements(GL_TRIANGLES, (GLsizei)c.count, GL_UNSIGNED_INT, (const void*)(sizeof(uint32_t) * (size_t)c.first));
        drawCalls++;
    }
}

void Renderer::drawModels(Shader& s, const std::vector<ModelDraw>& list, bool shadowPass) {
    static const mat4 kIdentity[1] = {mat4()};
    for (const auto& d : list) {
        if (!d.mesh || !d.mesh->vao) continue;
        if (shadowPass && !d.castShadow) continue;
        s.set("uModel", d.model);
        if (d.bones && d.boneCount > 0) s.setMats("uBones", d.bones, std::min(d.boneCount, 48));
        else s.setMats("uBones", kIdentity, 1);
        if (!shadowPass) {
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

void Renderer::renderShadowMap(const FrameInput& in) {
    if (!m_shadow.fbo || !m_worldBounds.valid()) return;
    vec3 c = m_worldBounds.center();
    float radius = length(m_worldBounds.size()) * 0.5f + 64.0f;
    vec3 up = std::fabs(m_env.sunDir.z) > 0.95f ? vec3(1, 0, 0) : vec3(0, 0, 1);
    mat4 view = lookAt(c + m_env.sunDir * radius, c, up);
    vec3 mn(1e30f), mx(-1e30f);
    for (int i = 0; i < 8; i++) {
        vec3 p((i & 1) ? m_worldBounds.mx.x : m_worldBounds.mn.x, (i & 2) ? m_worldBounds.mx.y : m_worldBounds.mn.y,
               (i & 4) ? m_worldBounds.mx.z + 256.0f : m_worldBounds.mn.z);
        vec3 v = xformPoint(view, p);
        mn = vmin(mn, v);
        mx = vmax(mx, v);
    }
    mat4 proj = ortho(mn.x, mx.x, mn.y, mx.y, -mx.z - 64.0f, -mn.z + 64.0f);
    m_shadowViewProj = proj * view;
    mat4 bias = translate(vec3(0.5f)) * scale(vec3(0.5f));
    m_shadowMat = bias * m_shadowViewProj;

    m_shadow.bind();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    m_shadowWorld.use();
    m_shadowWorld.set("uViewProj", m_shadowViewProj);
    drawWorld(m_shadowWorld, nullptr);
    if (in.models) {
        m_shadowModel.use();
        m_shadowModel.set("uViewProj", m_shadowViewProj);
        drawModels(m_shadowModel, *in.models, true);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
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
    if (in.drawWorld && m_rs.shadowSize > 0) renderShadowMap(in);

    m_hdrMS.bind();
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    Frustum fr;
    fr.fromMatrix(in.cam.viewProj);
    if (in.drawWorld) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        m_world.use();
        m_world.set("uViewProj", in.cam.viewProj);
        applyCommon(m_world, in.cam, in, true);
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
        m_model.set("uViewProj", in.cam.viewProj);
        m_model.set("uIsViewmodel", 0.0f);
        applyCommon(m_model, in.cam, in, in.drawWorld);
        drawModels(m_model, *in.models, false);
    }
    // Sky last, only where nothing was drawn.
    glDepthMask(GL_FALSE);
    if (in.drawSky) {
        glDepthFunc(GL_LEQUAL);
        m_sky.use();
        applyCommon(m_sky, in.cam, in, false);
        m_sky.set("uInvViewProj", in.cam.invViewProj);
        m_sky.set("uCloudiness", m_env.cloudiness);
        fullscreen();
        glDepthFunc(GL_LESS);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (in.decals && !in.decals->empty()) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);
        drawSprites(*in.decals, in.cam, in);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (in.sprites) drawSprites(*in.sprites, in.cam, in);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    if (in.drawViewmodel && in.vmModels && !in.vmModels->empty()) {
        glClear(GL_DEPTH_BUFFER_BIT);
        m_model.use();
        m_model.set("uViewProj", in.vmCam.viewProj);
        m_model.set("uIsViewmodel", 1.0f);
        applyCommon(m_model, in.vmCam, in, in.drawWorld);
        drawModels(m_model, *in.vmModels, false);
        if (in.vmSprites && !in.vmSprites->empty()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            drawSprites(*in.vmSprites, in.vmCam, in);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    // Resolve MSAA.
    GLuint hdrTex = m_hdrMS.color;
    if (m_hdrMS.samples > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_hdrMS.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_hdr.fbo);
        glBlitFramebuffer(0, 0, m_w, m_h, 0, 0, m_w, m_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        hdrTex = m_hdr.color;
    }
    glDisable(GL_DEPTH_TEST);

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

    m_ldr.bind();
    m_composite.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex);
    glActiveTexture(GL_TEXTURE0);
    m_composite.set("uHdr", 0);
    m_composite.set("uBloom", 1);
    m_composite.set("uExposure", m_env.exposure);
    m_composite.set("uBloomStrength", bloomTex ? m_env.bloomStrength : 0.0f);
    m_composite.set("uSaturation", m_env.saturation);
    m_composite.set("uContrast", m_env.contrast);
    m_composite.set("uVignette", m_env.vignette);
    m_composite.set("uGrade", m_env.grade);
    m_composite.set("uFlash", in.fx.flash);
    m_composite.set("uDamage", in.fx.damage);
    m_composite.set("uDesaturate", in.fx.desaturate);
    m_composite.set("uTime", in.time);
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
