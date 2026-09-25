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
    vec3 sunDir = normalize(vec3(0.45f, -0.40f, 0.74f));
    vec3 sunColor = vec3(1.0f, 0.86f, 0.68f) * 4.0f;
    vec3 skyZenith{0.13f, 0.31f, 0.80f};
    vec3 skyHorizon{0.56f, 0.68f, 0.88f};
    vec3 groundColor{0.42f, 0.34f, 0.25f};
    vec3 fogColor{0.66f, 0.73f, 0.84f};
    float fogDensity = 0.00007f;
    float fogFalloff = 1.0f / 700.0f;  // height fog: density halves every ~485 units
    float fogBaseHeight = 0.0f;
    float fogSunScatter = 0.06f;       // warm forward scattering towards the sun
    float fogMaxOpacity = 0.85f;
    float exposure = 1.1f;
    float bloomStrength = 0.06f;
    float bloomThreshold = 1.2f;
    float saturation = 1.12f;
    float contrast = 1.10f;
    float vignette = 0.30f;
    vec3 grade{1.03f, 1.0f, 0.96f};
    vec3 shadowTint{0.93f, 0.98f, 1.10f};    // split toning: cool shade
    vec3 highlightTint{1.05f, 1.0f, 0.93f};  // warm highlights
    float shaftStrength = 0.7f;
    float flareStrength = 1.0f;
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
    int msaa = 8;               // 0, 2, 4, 8
    int shadows = 3;            // 0 off, 1 medium, 2 high, 3 max (cascades, PCSS)
    int ssao = 2;               // 0 off, 1 normal, 2 high
    bool sunShafts = true;
    bool fxaa = true;
    bool bloom = true;
    bool lensFlare = true;
    int textureQuality = 2;     // 0 low (256), 1 medium (512), 2 high (1024); regenerates on change
    int anisotropy = 16;        // 1..16, clamped to the driver limit
    // Post-processing taste.
    float brightness = 0.0f;    // -1..1 exposure offset in stops
    float saturation = 1.0f;    // 0.5..1.5
    float sharpen = 0.35f;      // 0..1
    bool filmGrain = true;
    bool chromatic = false;
    bool vignette = true;
};

class Renderer {
public:
    bool init();
    void shutdown();
    // Applies every video setting; render targets, shadow maps and world textures are rebuilt only on change.
    void configure(int outW, int outH, const RenderSettings& rs);

    void setWorldGeometry(const std::vector<WorldVertex>& verts, const std::vector<uint32_t>& idx,
                          const std::vector<WorldChunk>& chunks, const AABB& bounds);
    void clearWorld();
    // Takes ownership. The renderer generates its own world textures at the configured quality, so arrays
    // passed here are only used until then (and deleted if the renderer's own set already exists).
    void setWorldTextures(GLuint albedoArray, GLuint normalArray);
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
    void applyCommon(Shader& s, const Camera& cam, const FrameInput& in, bool withShadow, bool withSSAO);
    void createShadowMaps();
    void computeCascades(const Camera& cam);
    void renderShadowMaps(const FrameInput& in);
    void renderDepthPrepass(const FrameInput& in, const Frustum& fr);
    void renderSSAO(const Camera& cam);
    void renderSunShafts(const Camera& cam, vec2 sunUV);
    void updateWorldTextures();
    void setupWorldMaterials(Shader& s);
    void drawWorld(Shader& s, const Frustum* fr);
    void drawModels(Shader& s, const std::vector<ModelDraw>& list, int mode);
    void drawSprites(const std::vector<SpriteVertex>& verts, const Camera& cam, const FrameInput& in);
    void fullscreen();

    Shader m_world, m_model, m_shadowWorld, m_shadowModel, m_sky, m_sprite, m_bloomDown, m_bloomUp, m_composite, m_final,
        m_copy, m_ssao, m_ssaoBlur, m_shaftMask, m_shaftBlur, m_flareVis;
    GLuint m_emptyVao = 0;
    GLuint m_spriteVao = 0, m_spriteVbo = 0, m_spriteIbo = 0;
    static constexpr int kMaxSprites = 16384;
    static constexpr int kMaxCascades = 4;

    GpuMesh m_worldMesh;
    std::vector<WorldChunk> m_chunks;
    AABB m_worldBounds;
    GLuint m_albedoArr = 0, m_normalArr = 0;
    bool m_ownTextures = false;
    bool m_worldHasSand = false;
    int m_texQuality = -1, m_texAniso = -1;
    Environment m_env;

    RenderSettings m_rs;
    int m_outW = 0, m_outH = 0, m_w = 0, m_h = 0;
    RenderTarget m_hdrMS, m_hdr, m_post, m_ldr, m_blurA, m_blurB;
    RenderTarget m_depth, m_ssaoA, m_ssaoB, m_shaftA, m_shaftB, m_flare;
    std::vector<RenderTarget> m_bloom;
    // Cascaded sun shadows: one depth texture array, one layer per cascade.
    GLuint m_shadowTex = 0, m_shadowFbo = 0, m_sampCmp = 0, m_sampRaw = 0, m_black = 0;
    int m_shadowSize = 0, m_cascades = 0, m_shadowQuality = 0;
    mat4 m_cascadeVP[kMaxCascades], m_cascadeMat[kMaxCascades];
    vec4 m_cascadeInfo[kMaxCascades];
    bool m_shadowsReady = false, m_ssaoReady = false;
    bool m_shadowDirty = true;
    bool m_configured = false;
};
