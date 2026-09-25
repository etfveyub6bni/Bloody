#pragma once
#include <vector>

#include "core/common.h"
#include "gfx/glutil.h"

struct Camera {
    vec3 pos;
    vec3 fwd{1, 0, 0}, left{0, 1, 0}, up{0, 0, 1};
    float fovY = 73.74f * kDeg;
    float aspect = 16.0f / 9.0f;
    float znear = 3.0f, zfar = 16000.0f;
    bool ortho = false;
    float orthoHalf = 1000.0f;  // half height of the orthographic view
    mat4 view, proj, viewProj, invViewProj;

    void setAngles(vec3 p, float pitchDeg, float yawDeg, float rollDeg = 0) {
        pos = p;
        fwd = angleForward(pitchDeg, yawDeg);
        vec3 l = yawLeft(yawDeg);
        vec3 u = cross(fwd, l);
        if (rollDeg != 0) {
            quat q = qaxis(fwd, rollDeg * kDeg);
            l = rotate(q, l);
            u = rotate(q, u);
        }
        left = l;
        up = u;
        update();
    }
    void lookAtPoint(vec3 p, vec3 target) {
        pos = p;
        fwd = normalize(target - p);
        left = normalize(cross(vec3(0, 0, 1), fwd));
        up = cross(fwd, left);
        update();
    }
    void update() {
        view = lookAt(pos, pos + fwd, up);
        proj = ortho ? ::ortho(-orthoHalf * aspect, orthoHalf * aspect, -orthoHalf, orthoHalf, znear, zfar)
                     : perspective(fovY, aspect, znear, zfar);
        viewProj = proj * view;
        invViewProj = inverse(viewProj);
    }
    // Converts a horizontal FOV defined at 4:3 (Source convention) into a vertical FOV.
    static float vfovFrom43(float hfovDeg) { return 2.0f * std::atan(std::tan(hfovDeg * 0.5f * kDeg) * 0.75f); }
};

struct Environment {
    vec3 sunDir = normalize(vec3(0.45f, -0.38f, 0.80f));
    vec3 sunColor = vec3(1.0f, 0.92f, 0.80f) * 3.4f;
    vec3 skyZenith{0.20f, 0.40f, 0.78f};
    vec3 skyHorizon{0.66f, 0.74f, 0.84f};
    vec3 groundColor{0.40f, 0.33f, 0.25f};
    vec3 fogColor{0.70f, 0.74f, 0.80f};
    float fogDensity = 0.00005f;
    float exposure = 1.0f;
    float bloomStrength = 0.05f;
    float bloomThreshold = 1.4f;
    float saturation = 1.05f;
    float contrast = 1.05f;
    float vignette = 0.30f;
    vec3 grade{1.02f, 1.0f, 0.97f};
    float cloudiness = 0.35f;
    float skyIntensity = 1.0f;   // multiplier for baked sky light
    float bounceScale = 0.75f;   // multiplier for baked sun bounce
    vec3 indoorAmbient{0.05f, 0.045f, 0.04f};

    vec3 skyRadiance(vec3 d) const;  // CPU mirror of the shader sky gradient (no sun disk)
};

struct WorldVertex {
    vec3 pos;
    vec3 normal;
    vec2 uv;
    vec4 tangent;  // xyz tangent, w bitangent sign
    vec4 light;    // baked indirect rgb, a = ambient occlusion
    uint32_t tint; // rgb tint, a = material layer index
};

struct ModelVertex {
    vec3 pos;
    vec3 normal;
    uint32_t color;  // sRGB albedo
    uint32_t mat;    // roughness, metalness, pattern, bone
};

struct WorldChunk {
    uint32_t first = 0, count = 0;
    AABB bounds;
};

struct ModelDraw {
    const GpuMesh* mesh = nullptr;
    mat4 model;
    const mat4* bones = nullptr;
    int boneCount = 0;
    vec4 tint{1, 1, 1, 1};
    vec3 ambUp{0.5f, 0.5f, 0.5f}, ambDown{0.3f, 0.3f, 0.3f};
    vec3 patternA{0.3f, 0.3f, 0.3f}, patternB{0.2f, 0.2f, 0.2f};
    bool castShadow = true;
};

struct SpriteVertex {
    vec3 pos;
    vec2 uv;
    uint32_t color;
    vec4 params;  // x shape, y seed, z additive, w lit
};

struct PointLight {
    vec3 pos;
    float radius;
    vec3 color;
};

struct ScreenFx {
    vec4 flash{1, 1, 1, 0};
    float damage = 0;
    float desaturate = 0;
};

struct FrameInput {
    Camera cam;
    Camera vmCam;
    bool drawWorld = true;
    bool drawSky = true;
    bool drawViewmodel = false;
    float time = 0;
    const std::vector<ModelDraw>* models = nullptr;
    const std::vector<ModelDraw>* vmModels = nullptr;
    const std::vector<SpriteVertex>* sprites = nullptr;
    const std::vector<SpriteVertex>* vmSprites = nullptr;
    const std::vector<SpriteVertex>* decals = nullptr;
    const std::vector<PointLight>* lights = nullptr;
    vec3 spriteLight{1, 1, 1};
    ScreenFx fx;
};

struct RenderSettings {
    float renderScale = 1.0f;
    int msaa = 4;
    int shadowSize = 4096;
    bool bloom = true;
    bool operator==(const RenderSettings& o) const {
        return renderScale == o.renderScale && msaa == o.msaa && shadowSize == o.shadowSize && bloom == o.bloom;
    }
};

class Renderer {
public:
    bool init();
    void shutdown();
    void configure(int outW, int outH, const RenderSettings& rs);

    void setWorldGeometry(const std::vector<WorldVertex>& verts, const std::vector<uint32_t>& idx,
                          const std::vector<WorldChunk>& chunks, const AABB& bounds);
    void clearWorld();
    void setWorldTextures(GLuint albedoArray, GLuint normalArray) { m_albedoArr = albedoArray; m_normalArr = normalArray; }
    void setEnvironment(const Environment& e) { m_env = e; m_shadowDirty = true; }
    const Environment& environment() const { return m_env; }

    GpuMesh createModelMesh(const std::vector<ModelVertex>& v, const std::vector<uint32_t>& idx);

    void renderFrame(const FrameInput& in);
    // Blurred copy of the final LDR image (for frosted UI panels). Call after renderFrame.
    void updateBlur();
    void presentToScreen(int winW, int winH);

    const RenderTarget& ldr() const { return m_ldr; }
    GLuint blurTexture() const { return m_blurA.color; }
    int outWidth() const { return m_outW; }
    int outHeight() const { return m_outH; }
    // Copies a region of the final LDR image into a new standalone texture (map thumbnails, icons).
    GLuint captureLdr(int x, int y, int w, int h, bool mips = false);

    int drawCalls = 0;

private:
    void applyCommon(Shader& s, const Camera& cam, const FrameInput& in, bool withShadow);
    void renderShadowMap(const FrameInput& in);
    void drawWorld(Shader& s, const Frustum* fr);
    void drawModels(Shader& s, const std::vector<ModelDraw>& list, bool shadowPass);
    void drawSprites(const std::vector<SpriteVertex>& verts, const Camera& cam, const FrameInput& in);
    void fullscreen();

    Shader m_world, m_model, m_shadowWorld, m_shadowModel, m_sky, m_sprite, m_bloomDown, m_bloomUp, m_composite, m_copy;
    GLuint m_emptyVao = 0;
    GLuint m_spriteVao = 0, m_spriteVbo = 0, m_spriteIbo = 0;
    static constexpr int kMaxSprites = 16384;

    GpuMesh m_worldMesh;
    std::vector<WorldChunk> m_chunks;
    AABB m_worldBounds;
    GLuint m_albedoArr = 0, m_normalArr = 0;
    Environment m_env;

    RenderSettings m_rs;
    int m_outW = 0, m_outH = 0, m_w = 0, m_h = 0;
    RenderTarget m_hdrMS, m_hdr, m_ldr, m_blurA, m_blurB, m_shadow;
    std::vector<RenderTarget> m_bloom;
    mat4 m_shadowViewProj, m_shadowMat;
    bool m_shadowDirty = true;
    bool m_configured = false;
};
