// GLSL sources. Everything targets OpenGL 3.3 core.
#pragma once

namespace glsl {

inline const char* kVersion = "#version 330 core\n";

inline const char* kNoise = R"GLSL(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float hash13(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }  // interleaved gradient noise
float vnoise2(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
float vnoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x), mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x), mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm2(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += vnoise2(p) * a; p = p * 2.03 + 17.1; a *= 0.5; } return s; }
float fbm3(vec3 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { s += vnoise3(p) * a; p = p * 2.03 + 11.7; a *= 0.5; } return s; }
)GLSL";

// Shared lighting: sky, cascaded sun shadows, SSAO, GGX shading and height fog. Model shaders call
// shadowFactor(), shadeSurface() and applyFog() from here.
inline const char* kLighting = R"GLSL(
const float PI = 3.14159265359;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uGroundColor;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec4 uFogParams;       // x: height falloff, y: base height, z: sun in-scattering, w: max opacity
uniform float uSkyLum;
uniform float uTime;
uniform sampler2DArrayShadow uShadowMap;
uniform sampler2DArray uShadowDepth;  // same texture without comparison (PCSS blocker search)
uniform mat4 uShadowMats[4];
uniform vec4 uCascadeInfo[4];  // x: world units per texel, y: depth range in world units
uniform vec4 uShadowParams;    // x: cascades, y: enabled, z: 1 / map size, w: quality 1..3
uniform sampler2D uSSAO;
uniform vec4 uSSAOParams;      // xy: 1 / render size, z: enabled
uniform vec4 uLightPos[4];     // xyz position, w radius
uniform vec4 uLightColor[4];
uniform int uLightCount;

float sat(float x) { return clamp(x, 0.0, 1.0); }

// Mirrored on the CPU by Environment::skyRadiance (light baking).
vec3 skyGradient(vec3 d) {
    float h = d.z;
    vec3 col = mix(uSkyHorizon, uSkyZenith, sqrt(sat(h)));
    col = mix(col, uSkyHorizon * 1.08, exp(-abs(h) * 16.0) * 0.45);
    float g = sat(-h / 0.3);
    col = mix(col, uGroundColor, g * g * (3.0 - 2.0 * g));
    float m = max(dot(d, uSunDir), 0.0);
    float m2 = m * m, m4 = m2 * m2, m8 = m4 * m4, m32 = m8 * m8 * m8 * m8;
    col += uSunColor * (0.018 * m4 * m + 0.05 * m32 * m8 + 0.16 * pow(m, 500.0));
    return col;
}

vec3 envSpecular(vec3 r, float rough) {
    r.z = max(r.z, -0.2);
    vec3 s = skyGradient(normalize(r));
    vec3 avg = mix(uGroundColor, uSkyZenith, 0.5 + 0.5 * r.z) * 0.8;
    return mix(s, avg, rough * rough);
}

const vec2 kPoisson[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(0.97484398, 0.75648379), vec2(-0.81409955, 0.91437590),
    vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023),
    vec2(0.79197514, 0.19090188), vec2(-0.24188840, 0.99706507), vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

float shadowFactor(vec3 wpos, vec3 n) {
    if (uShadowParams.y < 0.5) return 1.0;
    int count = int(uShadowParams.x + 0.5);
    float slope = 1.0 - abs(dot(n, uSunDir));
    float dither = ign(gl_FragCoord.xy);
    int ci = -1;
    vec3 c = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        if (i >= count) break;
        vec3 p = wpos + n * (uCascadeInfo[i].x * (1.5 + 2.5 * slope));
        vec3 sc = (uShadowMats[i] * vec4(p, 1.0)).xyz;
        vec2 e2 = min(sc.xy, 1.0 - sc.xy);
        float edge = min(e2.x, e2.y);
        if (edge <= 0.01 || sc.z >= 1.0) continue;
        c = sc;
        ci = i;
        // Dithered hand-over to the next cascade near the border hides the seam.
        if (i + 1 < count && edge < 0.08 && dither > edge / 0.08) continue;
        break;
    }
    if (ci < 0) return 1.0;
    float texel = uShadowParams.z;
    float wpt = uCascadeInfo[ci].x;
    float ref = c.z - wpt * 0.5 / uCascadeInfo[ci].y;
    float layer = float(ci);
    float ang = dither * 6.2831853;
    mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
    float radius = clamp(0.9 / wpt, 1.0, 3.0);
    if (uShadowParams.w > 2.5) {
        // PCSS: penumbra widens with the distance between blocker and receiver.
        float searchR = clamp(12.0 / wpt, 2.0, 16.0) * texel;
        float bsum = 0.0, bcnt = 0.0;
        for (int i = 0; i < 8; i++) {
            float d = texture(uShadowDepth, vec3(c.xy + rot * kPoisson[i * 2 + 1] * searchR, layer)).r;
            if (d < ref) { bsum += d; bcnt += 1.0; }
        }
        if (bcnt < 0.5) return 1.0;
        float dist = (ref - bsum / bcnt) * uCascadeInfo[ci].y;
        radius = clamp((0.35 + dist * 0.018) / wpt, 1.0, 12.0);
    }
    radius *= texel;
    float s = 0.0;
    for (int i = 0; i < 4; i++) s += texture(uShadowMap, vec4(c.xy + rot * kPoisson[i] * radius, layer, ref));
    if (s < 0.001 || s > 3.999) return s * 0.25;
    int taps = uShadowParams.w > 1.5 ? 16 : 8;
    for (int i = 4; i < 16; i++) {
        if (i >= taps) break;
        s += texture(uShadowMap, vec4(c.xy + rot * kPoisson[i] * radius, layer, ref));
    }
    return s / float(taps);
}

float ssaoFactor() { return uSSAOParams.z > 0.5 ? texture(uSSAO, gl_FragCoord.xy * uSSAOParams.xy).r : 1.0; }

vec2 envBRDF(float NoV, float rough) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 shadeSurface(vec3 albedo, float rough, float metal, vec3 N, vec3 V, vec3 P, vec3 indirect, float ao, float shadow) {
    rough = clamp(rough, 0.05, 1.0);
    ao *= ssaoFactor();
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 diffCol = albedo * (1.0 - metal);
    float NoV = max(dot(N, V), 1e-4);
    float a = rough * rough, a2 = a * a;
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    vec3 col = vec3(0.0);
    float NoL = dot(N, uSunDir);
    if (NoL > 0.0 && shadow > 0.001) {
        vec3 H = normalize(uSunDir + V);
        float NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
        float d = NoH * NoH * (a2 - 1.0) + 1.0;
        float D = a2 / (PI * d * d);
        float G = (NoV / (NoV * (1.0 - k) + k)) * (NoL / (NoL * (1.0 - k) + k));
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
        vec3 spec = D * G * F / max(4.0 * NoV * NoL, 1e-4);
        col += (diffCol / PI * (1.0 - F) + spec) * uSunColor * NoL * shadow;
    }
    for (int i = 0; i < uLightCount; i++) {
        vec3 Lv = uLightPos[i].xyz - P;
        float dist = length(Lv);
        vec3 L = Lv / max(dist, 1e-3);
        float att = sat(1.0 - dist / uLightPos[i].w);
        att *= att;
        float nl = max(dot(N, L), 0.0);
        float nh = max(dot(N, normalize(L + V)), 0.0);
        float dd = nh * nh * (a2 - 1.0) + 1.0;
        vec3 spec = F0 * (a2 / (PI * dd * dd)) * 0.25;
        col += (diffCol + spec) * uLightColor[i].rgb * nl * att;
    }
    vec2 ab = envBRDF(NoV, rough);
    float envScale = clamp(dot(indirect, vec3(0.3, 0.59, 0.11)) / max(uSkyLum, 1e-3), 0.04, 1.0);
    vec3 specAmb = envSpecular(reflect(-V, N), rough) * (F0 * ab.x + ab.y) * envScale;
    float specOcc = sat(pow(NoV + ao, exp2(-16.0 * rough - 1.0)) - 1.0 + ao);
    col += diffCol * indirect * ao + specAmb * specOcc;
    return col;
}

// Exponential height fog with Henyey-Greenstein sun in-scattering (aerial perspective).
vec3 applyFog(vec3 col, vec3 P) {
    vec3 d = P - uCamPos;
    float dist = length(d);
    vec3 dir = d / max(dist, 1e-3);
    float fall = uFogParams.x;
    float kz = fall * d.z;
    float integral = exp(-fall * (uCamPos.z - uFogParams.y)) * (abs(kz) > 1e-4 ? (1.0 - exp(-kz)) / kz : 1.0);
    float f = min(1.0 - exp(-uFogDensity * dist * integral), uFogParams.w);
    const float g = 0.7;
    float hg = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * dot(dir, uSunDir), 1.5));
    vec3 fc = uFogColor + uSunColor * hg * uFogParams.z;
    return mix(col, fc, f);
}
)GLSL";

inline const char* kWorldVS = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in vec4 aLight;
layout(location = 5) in vec4 aTint;
uniform mat4 uViewProj;
out vec3 vPos; out vec3 vNormal; out vec2 vUV; out vec4 vTangent; out vec4 vLight; out vec3 vTint;
flat out float vLayer;
void main() {
    vPos = aPos; vNormal = aNormal; vUV = aUV; vTangent = aTangent; vLight = aLight; vTint = aTint.rgb;
    vLayer = floor(aTint.a * 255.0 + 0.5);
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)GLSL";

inline const char* kWorldFS = R"GLSL(
uniform sampler2DArray uAlbedoTex;
uniform sampler2DArray uNormalTex;
uniform vec4 uLayerParams[16];  // x: uv scale, y: anti-tiling, z: macro variation, w: grime
uniform vec4 uReveal;           // x: plaster layer, y: brick layer, z: enabled
uniform vec4 uDust;             // x: sand layer, y: bitmask of layers whose ledges collect sand
uniform float uDebugLayers;     // CS2P_GFX_DEBUG=layers: flat colour per material layer
in vec3 vPos; in vec3 vNormal; in vec2 vUV; in vec4 vTangent; in vec4 vLight; in vec3 vTint;
flat in float vLayer;
out vec4 oColor;

void sampleLayer(vec2 uv, float layer, vec2 dx, vec2 dy, bool antiTile, out vec4 alb, out vec4 nm) {
    if (antiTile) {
        // Two randomly offset lookups blended by a low-frequency index (after Inigo Quilez) hide repetition.
        float l = vnoise2(uv * 0.55) * 8.0;
        float f = fract(l), ia = floor(l);
        vec2 oa = sin(vec2(3.0, 7.0) * ia), ob = sin(vec2(3.0, 7.0) * (ia + 1.0));
        vec4 a1 = textureGrad(uAlbedoTex, vec3(uv + oa, layer), dx, dy);
        vec4 a2 = textureGrad(uAlbedoTex, vec3(uv + ob, layer), dx, dy);
        float b = smoothstep(0.2, 0.8, f - 0.1 * dot(a1.rgb - a2.rgb, vec3(1.0)));
        alb = mix(a1, a2, b);
        nm = mix(textureGrad(uNormalTex, vec3(uv + oa, layer), dx, dy), textureGrad(uNormalTex, vec3(uv + ob, layer), dx, dy), b);
    } else {
        alb = textureGrad(uAlbedoTex, vec3(uv, layer), dx, dy);
        nm = textureGrad(uNormalTex, vec3(uv, layer), dx, dy);
    }
}

void main() {
    int li = int(vLayer + 0.5);
    vec4 LP = uLayerParams[li];
    vec2 uv = vUV * LP.x;
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    vec4 alb, nm;
    sampleLayer(uv, vLayer, dx, dy, LP.y > 0.5, alb, nm);
    vec3 N = normalize(vNormal);
    bool wall = abs(N.z) < 0.5;

    // Sand settles on upward-facing ledges, curbs and roofs of wall materials.
    if (N.z > 0.7 && ((int(uDust.y) >> li) & 1) == 1) {
        vec2 suv = vPos.xy / 256.0;
        vec2 sdx = dFdx(suv), sdy = dFdy(suv);
        vec4 sa = textureGrad(uAlbedoTex, vec3(suv, uDust.x), sdx, sdy);
        vec4 sn = textureGrad(uNormalTex, vec3(suv, uDust.x), sdx, sdy);
        float k = smoothstep(0.7, 0.9, N.z) * (0.75 + 0.25 * smoothstep(0.3, 0.7, vnoise2(vPos.xy * 0.05)));
        alb = mix(alb, sa, k);
        nm = mix(nm, sn, k * 0.7);
    }

    // Exposed brickwork on plaster walls, placed in world space so patches never repeat.
    if (li == int(uReveal.x + 0.5) && wall && uReveal.z > 0.5) {
        vec2 wc = vec2(dot(vPos.xy, vec2(-N.y, N.x)), vPos.z);
        float m = fbm2(wc * 0.0065 + vec2(13.7, 5.1)) + 0.07 * (1.0 - smoothstep(10.0, 120.0, vPos.z));
        m += (vnoise2(wc * 0.09) - 0.5) * 0.025;
        const float thr = 0.72;
        if (m > thr - 0.04) {
            float s = 2.0 / LP.x;
            vec4 ba = textureGrad(uAlbedoTex, vec3(vUV * 2.0, uReveal.y), dx * s, dy * s);
            vec4 bn = textureGrad(uNormalTex, vec3(vUV * 2.0, uReveal.y), dx * s, dy * s);
            float t = smoothstep(thr, thr + 0.008, m);
            float lip = smoothstep(thr - 0.035, thr, m) * (1.0 - t);
            float inner = t * (1.0 - smoothstep(thr + 0.008, thr + 0.05, m));
            ba.rgb = mix(ba.rgb, vec3(dot(ba.rgb, vec3(0.33))), 0.25) * vec3(0.80, 0.74, 0.68);
            alb = mix(alb, ba, t);
            nm = mix(nm, bn, t);
            alb.rgb *= 1.0 + 0.06 * lip;
            nm.b *= 1.0 - 0.6 * inner - 0.15 * lip;
        }
    }

    vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
    vec3 B = cross(N, T) * vTangent.w;
    vec2 nxy = nm.xy * 2.0 - 1.0;
    vec3 n = normalize(T * nxy.x + B * nxy.y + N * sqrt(max(1.0 - dot(nxy, nxy), 0.0)));
    vec3 albedo = alb.rgb * vTint;
    float rough = alb.a;

    // World-space macro variation breaks up any remaining tiling.
    if (LP.z > 0.0) {
        float mv = vnoise3(vPos * 0.0024) * 0.65 + vnoise3(vPos * 0.011 + 5.3) * 0.35 - 0.5;
        albedo *= 1.0 + mv * 0.32 * LP.z;
        albedo *= mix(vec3(1.0), vec3(1.06, 1.0, 0.91), clamp(mv * 2.0, -1.0, 1.0) * 0.5 * LP.z);
        rough = clamp(rough - mv * 0.12 * LP.z, 0.05, 1.0);
    }
    // Dirt collects where the baked occlusion is strong (corners, wall bases).
    float gn = vnoise3(vPos * 0.035) * 0.6 + vnoise3(vPos * 0.11 + 3.7) * 0.4;
    float grime = sat((1.0 - vLight.a) * 1.5) * LP.w * smoothstep(0.25, 0.75, gn);
    albedo = mix(albedo, albedo * vec3(0.66, 0.56, 0.46), grime * 0.5);

    float cavity = nm.b;
    float ao = vLight.a * mix(1.0, cavity, 0.8);
    vec3 V = normalize(uCamPos - vPos);
    float sh = shadowFactor(vPos, N) * mix(1.0, cavity, 0.35);
    vec3 col = shadeSurface(albedo, rough, nm.a, n, V, vPos, vLight.rgb, ao, sh);
    oColor = vec4(applyFog(col, vPos), 1.0);
    if (uDebugLayers > 0.5) {
        vec3 lc = fract(vec3(0.37, 0.61, 0.83) * float(li + 1) * 1.7);
        oColor = vec4(lc * (0.55 + 0.45 * N.z), 1.0);
    }
}
)GLSL";

inline const char* kShadowWorldVS = R"GLSL(
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)GLSL";

inline const char* kShadowModelVS = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aMat;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uBones[48];
void main() {
    int b = int(aMat.w * 255.0 + 0.5);
    gl_Position = uViewProj * (uModel * (uBones[b] * vec4(aPos, 1.0)));
}
)GLSL";

inline const char* kEmptyFS = R"GLSL(
out vec4 oColor;
void main() { oColor = vec4(1.0); }
)GLSL";

inline const char* kFullscreenVS = R"GLSL(
out vec2 vUV;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
)GLSL";

inline const char* kSkyFS = R"GLSL(
uniform mat4 uInvViewProj;
uniform float uCloudiness;
in vec2 vUV;
out vec4 oColor;
void main() {
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(wp.xyz / wp.w - uCamPos);
    vec3 col = skyGradient(dir);
    float mu = dot(dir, uSunDir);
    // Sun disk with limb darkening.
    float disk = smoothstep(0.99950, 0.99972, mu);
    float limb = sat((mu - 0.99950) / (1.0 - 0.99950));
    col += uSunColor * disk * 40.0 * (0.6 + 0.4 * sqrt(limb));
    if (dir.z > 0.0) {
        vec2 uv = dir.xy / (dir.z + 0.1);
        vec2 wind = vec2(uTime * 0.004, uTime * 0.0016);
        float fade = smoothstep(0.0, 0.22, dir.z);
        // High cirrus streaks.
        vec2 cu = vec2(uv.x * 0.8 + uv.y * 0.6, uv.y * 0.8 - uv.x * 0.6) * vec2(0.35, 2.2) + wind * 0.5;
        float cirrus = smoothstep(0.52, 0.85, fbm2(cu + 31.0)) * 0.35;
        col = mix(col, uSkyHorizon * 1.2 + uSunColor * 0.12, cirrus * fade);
        // Domain-warped cumulus lit from the sun side, with a silver lining near the sun.
        vec2 q = uv * 1.1 + wind;
        float warp = fbm2(q * 0.6 + 5.2);
        float dens = fbm2(q * 1.35 + warp * 0.9);
        float cov = 0.60 - uCloudiness * 0.24;
        float c = smoothstep(cov, cov + 0.22, dens);
        if (c > 0.001) {
            vec2 toSun = normalize(uSunDir.xy + vec2(1e-4)) * 0.09;
            float d2 = fbm2((q + toSun) * 1.35 + warp * 0.9);
            float lit = sat(0.55 + (dens - d2) * 5.0);
            vec3 shade = mix(uSkyZenith * 0.55 + uSkyHorizon * 0.55, uSkyHorizon * 1.25 + uSunColor * 0.28, lit);
            float edge = 1.0 - smoothstep(cov, cov + 0.12, dens);
            shade += uSunColor * pow(max(mu, 0.0), 10.0) * edge * 0.6;
            col = mix(col, shade, c * fade * 0.93);
        }
    }
    oColor = vec4(col, 1.0);
}
)GLSL";

inline const char* kSpriteVS = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aParams;
uniform mat4 uViewProj;
out vec2 vUV; out vec4 vColor; out vec4 vParams;
void main() { vUV = aUV; vColor = aColor; vParams = aParams; gl_Position = uViewProj * vec4(aPos, 1.0); }
)GLSL";

inline const char* kSpriteFS = R"GLSL(
uniform vec3 uLitColor;
in vec2 vUV; in vec4 vColor; in vec4 vParams;
out vec4 oColor;
void main() {
    int shape = int(vParams.x + 0.5);
    vec2 p = vUV * 2.0 - 1.0;
    float r = length(p);
    float a = 0.0;
    vec3 c = vColor.rgb;
    float seed = vParams.y;
    if (shape == 0) {            // soft smoke/dust puff
        float n = fbm2(vUV * 3.0 + seed * 17.0);
        a = smoothstep(1.0, 0.1, r + (n - 0.5) * 0.7);
    } else if (shape == 1) {     // glow / spark
        a = pow(max(1.0 - r, 0.0), 2.2);
    } else if (shape == 2) {     // muzzle flash star
        float ang = atan(p.y, p.x);
        float spikes = 0.45 + 0.55 * pow(abs(cos(ang * 2.5 + seed * 6.283)), 8.0);
        float n = vnoise2(p * 5.0 + seed * 31.0);
        a = smoothstep(spikes, spikes * 0.15, r) * (0.75 + 0.5 * n);
        c *= 1.0 + 2.5 * (1.0 - smoothstep(0.0, 0.5, r));
    } else if (shape == 3) {     // tracer streak, uv.x along its length
        a = smoothstep(1.0, 0.0, abs(p.y)) * smoothstep(0.0, 0.35, vUV.x) * smoothstep(1.0, 0.8, vUV.x);
    } else if (shape == 4) {     // bullet hole decal
        float n = vnoise2(p * 5.0 + seed * 13.0);
        float hole = smoothstep(0.30, 0.18, r + (n - 0.5) * 0.12);
        float ring = smoothstep(1.0, 0.3, r + (n - 0.5) * 0.4) * 0.6;
        a = max(hole, ring);
        c = mix(c, vec3(0.015), hole);
    } else if (shape == 5) {     // blood splat decal
        float n = fbm2(p * 2.0 + seed * 7.0);
        a = smoothstep(0.75, 0.45, r + (n - 0.5) * 0.9);
    } else if (shape == 6) {     // dense smoke-grenade puff
        float n = fbm2(vUV * 2.2 + seed * 9.0 + uTime * 0.02);
        a = smoothstep(1.0, 0.25, r + (n - 0.5) * 0.5);
    } else if (shape == 7) {     // solid ring (HE shockwave)
        a = smoothstep(0.1, 0.0, abs(r - 0.8)) * 0.8;
    } else {                     // plain quad
        a = 1.0;
    }
    if (vParams.w > 0.5) c *= uLitColor;
    a *= vColor.a;
    // Premultiplied output; additive sprites write zero alpha so they don't occlude.
    oColor = vec4(c * a, vParams.z > 0.5 ? 0.0 : a);
}
)GLSL";

inline const char* kBloomDownFS = R"GLSL(
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform int uPrefilter;
uniform float uThreshold;
in vec2 vUV;
out vec4 oColor;
vec3 s(vec2 o) { return texture(uSrc, vUV + o * uTexel).rgb; }
void main() {
    vec3 a = s(vec2(-2, 2)), b = s(vec2(0, 2)), c = s(vec2(2, 2));
    vec3 d = s(vec2(-2, 0)), e = s(vec2(0, 0)), f = s(vec2(2, 0));
    vec3 g = s(vec2(-2, -2)), h = s(vec2(0, -2)), i = s(vec2(2, -2));
    vec3 j = s(vec2(-1, 1)), k = s(vec2(1, 1)), l = s(vec2(-1, -1)), m = s(vec2(1, -1));
    vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (uPrefilter == 1) {
        col = min(col, vec3(60.0));
        float br = max(col.r, max(col.g, col.b));
        float knee = uThreshold * 0.5;
        float rq = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);
        rq = rq * rq / (4.0 * knee + 1e-4);
        col *= max(rq, br - uThreshold) / max(br, 1e-4);
    }
    oColor = vec4(col, 1.0);
}
)GLSL";

inline const char* kBloomUpFS = R"GLSL(
uniform sampler2D uSrc;
uniform vec2 uTexel;
in vec2 vUV;
out vec4 oColor;
void main() {
    vec2 t = uTexel;
    vec3 c = texture(uSrc, vUV + vec2(-t.x, -t.y)).rgb + texture(uSrc, vUV + vec2(0, -t.y)).rgb * 2.0 + texture(uSrc, vUV + vec2(t.x, -t.y)).rgb
           + texture(uSrc, vUV + vec2(-t.x, 0)).rgb * 2.0 + texture(uSrc, vUV).rgb * 4.0 + texture(uSrc, vUV + vec2(t.x, 0)).rgb * 2.0
           + texture(uSrc, vUV + vec2(-t.x, t.y)).rgb + texture(uSrc, vUV + vec2(0, t.y)).rgb * 2.0 + texture(uSrc, vUV + vec2(t.x, t.y)).rgb;
    oColor = vec4(c / 16.0, 1.0);
}
)GLSL";

// Scalable ambient obscurance (McGuire et al.) from the depth pre-pass; output r = AO, g = view depth.
inline const char* kSSAOFS = R"GLSL(
uniform sampler2D uDepth;
uniform vec4 uProj;   // x: tan(fovX/2), y: tan(fovY/2), z: near, w: far
uniform vec4 uAO;     // x: radius, y: intensity, z: samples, w: spiral turns
uniform vec2 uTexel;  // depth texel size
in vec2 vUV;
out vec4 oColor;
float linZ(float d) { float z = d * 2.0 - 1.0; return 2.0 * uProj.z * uProj.w / (uProj.w + uProj.z - z * (uProj.w - uProj.z)); }
// Snap to the depth texel centre so the ray matches the fetched depth (avoids banding on grazing floors).
vec3 viewPos(vec2 uv) {
    uv = (floor(uv / uTexel) + 0.5) * uTexel;
    float z = linZ(textureLod(uDepth, uv, 0.0).r);
    return vec3((uv * 2.0 - 1.0) * uProj.xy * z, -z);
}
void main() {
    float d0 = textureLod(uDepth, vUV, 0.0).r;
    if (d0 >= 1.0) { oColor = vec4(1.0, 60000.0, 0.0, 1.0); return; }
    vec3 P = viewPos(vUV);
    vec3 Pr = viewPos(vUV + vec2(uTexel.x, 0.0)), Pl = viewPos(vUV - vec2(uTexel.x, 0.0));
    vec3 Pt = viewPos(vUV + vec2(0.0, uTexel.y)), Pb = viewPos(vUV - vec2(0.0, uTexel.y));
    vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
    vec3 dy = abs(Pt.z - P.z) < abs(P.z - Pb.z) ? Pt - P : P - Pb;
    vec3 N = normalize(cross(dx, dy));
    float z = -P.z;
    float R = uAO.x;
    vec2 ssR = vec2(R / (2.0 * uProj.x * z), R / (2.0 * uProj.y * z));
    ssR = min(ssR, vec2(0.25));
    int n = int(uAO.z);
    float phi = ign(gl_FragCoord.xy) * 6.2831853;
    float sum = 0.0;
    for (int i = 0; i < 24; i++) {
        if (i >= n) break;
        float a = (float(i) + 0.5) / float(n);
        float ang = a * uAO.w * 6.2831853 + phi;
        vec3 S = viewPos(vUV + vec2(cos(ang), sin(ang)) * a * ssR);
        vec3 v = (S - P) / R;
        float vv = dot(v, v), vn = dot(v, N);
        float f = max(1.0 - vv, 0.0);
        sum += f * f * f * max((vn - 0.02) / (0.03 + vv), 0.0);
    }
    float ao = max(0.0, 1.0 - sum * uAO.y * 5.0 / float(n));
    ao = mix(ao, 1.0, smoothstep(2500.0, 4500.0, z));
    oColor = vec4(ao, z, 0.0, 1.0);
}
)GLSL";

// Depth-aware separable blur for the AO buffer.
inline const char* kSSAOBlurFS = R"GLSL(
uniform sampler2D uSrc;
uniform vec2 uDir;
in vec2 vUV;
out vec4 oColor;
void main() {
    vec2 c = texture(uSrc, vUV).rg;
    float sum = c.r, wsum = 1.0;
    const float w[4] = float[](0.9, 0.7, 0.45, 0.2);
    float tol = 1.0 / (c.g * 0.03 + 1.0);
    for (int i = 1; i <= 4; i++) {
        vec2 a = texture(uSrc, vUV + uDir * float(i)).rg;
        vec2 b = texture(uSrc, vUV - uDir * float(i)).rg;
        float wa = w[i - 1] * max(0.0, 1.0 - abs(a.g - c.g) * tol);
        float wb = w[i - 1] * max(0.0, 1.0 - abs(b.g - c.g) * tol);
        sum += a.r * wa + b.r * wb;
        wsum += wa + wb;
    }
    oColor = vec4(sum / wsum, c.g, 0.0, 1.0);
}
)GLSL";

// Sun shafts, step 1: bright sky around the sun where the depth buffer is empty.
inline const char* kShaftMaskFS = R"GLSL(
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec2 uTexel;
in vec2 vUV;
out vec4 oColor;
void main() {
    float sky = 0.0;
    sky += step(1.0, textureLod(uDepth, vUV + vec2(-1.0, -1.0) * uTexel, 0.0).r);
    sky += step(1.0, textureLod(uDepth, vUV + vec2(1.0, -1.0) * uTexel, 0.0).r);
    sky += step(1.0, textureLod(uDepth, vUV + vec2(-1.0, 1.0) * uTexel, 0.0).r);
    sky += step(1.0, textureLod(uDepth, vUV + vec2(1.0, 1.0) * uTexel, 0.0).r);
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(wp.xyz / wp.w - uCamPos);
    float mu = max(dot(dir, uSunDir), 0.0);
    float glow = pow(mu, 12.0) * 0.12 + pow(mu, 160.0) * 0.8 + smoothstep(0.9993, 0.9997, mu) * 4.0;
    oColor = vec4(vec3(glow * sky * 0.25), 1.0);
}
)GLSL";

// Sun shafts, step 2: radial blur towards the sun (volumetric light scattering as a post-process).
inline const char* kShaftBlurFS = R"GLSL(
uniform sampler2D uSrc;
uniform vec2 uSunUV;
uniform vec3 uParams;  // x: fraction of the way to the sun, y: decay, z: samples
in vec2 vUV;
out vec4 oColor;
void main() {
    int n = int(uParams.z);
    vec2 delta = (uSunUV - vUV) * uParams.x / float(n);
    vec2 uv = vUV + delta * ign(gl_FragCoord.xy);
    float decay = 1.0;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 64; i++) {
        if (i >= n) break;
        sum += textureLod(uSrc, uv, 0.0).rgb * decay;
        decay *= uParams.y;
        uv += delta;
    }
    oColor = vec4(sum / float(n), 1.0);
}
)GLSL";

// 1x1 lens-flare visibility: fraction of the sun disk that is visible in the HDR image.
inline const char* kFlareVisFS = R"GLSL(
uniform sampler2D uHdr;
uniform vec2 uSunUV;
uniform vec2 uRadius;
out vec4 oColor;
void main() {
    float vis = 0.0;
    for (int i = 0; i < 32; i++) {
        float a = float(i) * 2.39996323;
        vec2 uv = uSunUV + vec2(cos(a), sin(a)) * sqrt((float(i) + 0.5) / 32.0) * uRadius;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) continue;
        vec3 c = textureLod(uHdr, uv, 0.0).rgb;
        vis += smoothstep(6.0, 25.0, dot(c, vec3(0.2126, 0.7152, 0.0722)));
    }
    oColor = vec4(vis / 32.0);
}
)GLSL";

// HDR -> LDR: bloom, shafts, lens flare, exposure, filmic tone curve and grading. Alpha = luma for FXAA.
inline const char* kCompositeFS = R"GLSL(
uniform sampler2D uHdr;
uniform sampler2D uBloom;
uniform sampler2D uShafts;
uniform sampler2D uFlareVis;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uSaturation;
uniform float uContrast;
uniform vec3 uGrade;
uniform vec3 uShadowTint;
uniform vec3 uHighlightTint;
uniform vec3 uShaftColor;
uniform vec4 uSun;      // xy: sun uv, z: flare strength, w: aspect
uniform vec3 uSunTint;
uniform float uDesaturate;
uniform int uDebugView;  // 1: AO buffer, 2: sun shafts (CS2P_GFX_DEBUG=ssao|shafts)
uniform sampler2D uDebugTex;
in vec2 vUV;
out vec4 oColor;
vec3 aces(vec3 color) {
    const mat3 inM = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 outM = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    color = inM * color;
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return clamp(outM * (a / b), 0.0, 1.0);
}
vec3 toSrgb(vec3 c) { return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }
vec3 lensFlare(vec2 uv) {
    vec2 asp = vec2(uSun.w, 1.0);
    vec2 ts = (uv - uSun.xy) * asp;
    float d = length(ts);
    vec3 col = uSunTint * (exp(-d * 8.0) * 0.14 + exp(-d * 30.0) * 0.8);
    float ang = atan(ts.y, ts.x);
    float star = pow(abs(cos(ang * 3.0)), 80.0) + 0.6 * pow(abs(cos(ang * 3.0 + 1.0472)), 80.0);
    col += uSunTint * star * exp(-d * 5.0) * 0.18;
    col += uSunTint * vec3(1.0, 0.75, 0.55) * smoothstep(0.035, 0.0, abs(d - 0.34)) * 0.035;
    vec2 axis = vec2(0.5) - uSun.xy;
    const float gp[6] = float[](0.35, 0.72, 1.15, 1.45, 1.8, 2.3);
    const float gs[6] = float[](0.035, 0.022, 0.07, 0.03, 0.11, 0.18);
    const vec3 gc[6] = vec3[](vec3(1.0, 0.55, 0.25), vec3(0.45, 1.0, 0.55), vec3(0.55, 0.5, 1.0),
                              vec3(1.0, 0.85, 0.45), vec3(0.3, 0.65, 1.0), vec3(0.8, 0.45, 1.0));
    for (int i = 0; i < 6; i++) {
        float gd = length((uv - (uSun.xy + axis * gp[i])) * asp) / gs[i];
        float disc = smoothstep(1.0, 0.8, gd) * (0.35 + 0.65 * smoothstep(0.4, 1.0, gd));
        col += uSunTint * gc[i] * disc * 0.035;
    }
    return col * uSun.z;
}
void main() {
    if (uDebugView == 1) { oColor = vec4(vec3(texture(uDebugTex, vUV).r), 1.0); return; }
    if (uDebugView == 2) { oColor = vec4(sqrt(texture(uShafts, vUV).rgb), 1.0); return; }
    vec3 c = texture(uHdr, vUV).rgb;
    c += texture(uBloom, vUV).rgb * uBloomStrength;
    c += texture(uShafts, vUV).rgb * uShaftColor;
    if (uSun.z > 0.0) c += lensFlare(vUV) * texture(uFlareVis, vec2(0.5)).r;
    c = aces(c * uExposure * uGrade);
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c *= mix(uShadowTint, uHighlightTint, smoothstep(0.04, 0.55, l));
    c = clamp((c - 0.18) * uContrast + 0.18, 0.0, 1.0);
    l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = max(mix(vec3(l), c, uSaturation * (1.0 - uDesaturate)), 0.0);
    c = toSrgb(clamp(c, 0.0, 1.0));
    oColor = vec4(c, dot(c, vec3(0.299, 0.587, 0.114)));
}
)GLSL";

// Final pass: FXAA 3.11 (quality), contrast-adaptive sharpening, chromatic aberration, vignette,
// screen overlays, film grain and dithering.
inline const char* kFinalFS = R"GLSL(
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform float uFxaa;
uniform float uSharpen;
uniform float uChromatic;
uniform float uVignette;
uniform float uGrain;
uniform vec4 uFlash;
uniform float uDamage;
uniform float uTime;
in vec2 vUV;
out vec4 oColor;
float lumaAt(vec2 uv) { return textureLod(uSrc, uv, 0.0).a; }
float lumaOff(vec2 uv, vec2 o) { return textureLod(uSrc, uv + o * uTexel, 0.0).a; }
vec3 fxaa(vec2 uv, out bool edge) {
    vec4 rgbyM = textureLod(uSrc, uv, 0.0);
    float lumaM = rgbyM.a;
    float lumaS = lumaOff(uv, vec2(0.0, 1.0)), lumaE = lumaOff(uv, vec2(1.0, 0.0));
    float lumaN = lumaOff(uv, vec2(0.0, -1.0)), lumaW = lumaOff(uv, vec2(-1.0, 0.0));
    float rangeMax = max(max(lumaN, lumaW), max(lumaE, max(lumaS, lumaM)));
    float rangeMin = min(min(lumaN, lumaW), min(lumaE, min(lumaS, lumaM)));
    float range = rangeMax - rangeMin;
    edge = range >= max(0.0312, rangeMax * 0.125);
    if (!edge) return rgbyM.rgb;
    float lumaNW = lumaOff(uv, vec2(-1.0, -1.0)), lumaSE = lumaOff(uv, vec2(1.0, 1.0));
    float lumaNE = lumaOff(uv, vec2(1.0, -1.0)), lumaSW = lumaOff(uv, vec2(-1.0, 1.0));
    float lumaNS = lumaN + lumaS, lumaWE = lumaW + lumaE;
    float subpixRcpRange = 1.0 / range;
    float subpixNSWE = lumaNS + lumaWE;
    float edgeHorz1 = -2.0 * lumaM + lumaNS, edgeVert1 = -2.0 * lumaM + lumaWE;
    float lumaNESE = lumaNE + lumaSE, lumaNWNE = lumaNW + lumaNE;
    float edgeHorz2 = -2.0 * lumaE + lumaNESE, edgeVert2 = -2.0 * lumaN + lumaNWNE;
    float lumaNWSW = lumaNW + lumaSW, lumaSWSE = lumaSW + lumaSE;
    float edgeHorz4 = abs(edgeHorz1) * 2.0 + abs(edgeHorz2), edgeVert4 = abs(edgeVert1) * 2.0 + abs(edgeVert2);
    float edgeHorz3 = -2.0 * lumaW + lumaNWSW, edgeVert3 = -2.0 * lumaS + lumaSWSE;
    float edgeHorz = abs(edgeHorz3) + edgeHorz4, edgeVert = abs(edgeVert3) + edgeVert4;
    float subpixNWSWNESE = lumaNWSW + lumaNESE;
    float lengthSign = uTexel.x;
    bool horzSpan = edgeHorz >= edgeVert;
    float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;
    if (!horzSpan) { lumaN = lumaW; lumaS = lumaE; }
    else lengthSign = uTexel.y;
    float subpixB = subpixA * (1.0 / 12.0) - lumaM;
    float gradientN = lumaN - lumaM, gradientS = lumaS - lumaM;
    float lumaNN = lumaN + lumaM, lumaSS = lumaS + lumaM;
    bool pairN = abs(gradientN) >= abs(gradientS);
    float gradient = max(abs(gradientN), abs(gradientS));
    if (pairN) lengthSign = -lengthSign;
    float subpixC = clamp(abs(subpixB) * subpixRcpRange, 0.0, 1.0);
    vec2 posB = uv;
    vec2 offNP = horzSpan ? vec2(uTexel.x, 0.0) : vec2(0.0, uTexel.y);
    if (!horzSpan) posB.x += lengthSign * 0.5;
    else posB.y += lengthSign * 0.5;
    vec2 posN = posB - offNP, posP = posB + offNP;
    float subpixD = -2.0 * subpixC + 3.0;
    float lumaEndN = lumaAt(posN), lumaEndP = lumaAt(posP);
    float subpixE = subpixC * subpixC;
    if (!pairN) lumaNN = lumaSS;
    float gradientScaled = gradient * 0.25;
    float lumaMM = lumaM - lumaNN * 0.5;
    float subpixF = subpixD * subpixE;
    bool lumaMLTZero = lumaMM < 0.0;
    lumaEndN -= lumaNN * 0.5;
    lumaEndP -= lumaNN * 0.5;
    bool doneN = abs(lumaEndN) >= gradientScaled, doneP = abs(lumaEndP) >= gradientScaled;
    const float steps[8] = float[](1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0);
    for (int i = 0; i < 8; i++) {
        if (doneN && doneP) break;
        if (!doneN) { posN -= offNP * steps[i]; lumaEndN = lumaAt(posN) - lumaNN * 0.5; doneN = abs(lumaEndN) >= gradientScaled; }
        if (!doneP) { posP += offNP * steps[i]; lumaEndP = lumaAt(posP) - lumaNN * 0.5; doneP = abs(lumaEndP) >= gradientScaled; }
    }
    float dstN = horzSpan ? uv.x - posN.x : uv.y - posN.y;
    float dstP = horzSpan ? posP.x - uv.x : posP.y - uv.y;
    bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
    bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
    float spanLength = dstP + dstN;
    bool directionN = dstN < dstP;
    float dst = min(dstN, dstP);
    bool goodSpan = directionN ? goodSpanN : goodSpanP;
    float subpixG = subpixF * subpixF;
    float pixelOffset = dst * (-1.0 / spanLength) + 0.5;
    float subpixH = subpixG * 0.75;
    float pixelOffsetSubpix = max(goodSpan ? pixelOffset : 0.0, subpixH);
    vec2 fuv = uv;
    if (!horzSpan) fuv.x += pixelOffsetSubpix * lengthSign;
    else fuv.y += pixelOffsetSubpix * lengthSign;
    return textureLod(uSrc, fuv, 0.0).rgb;
}
// AMD FidelityFX CAS style: sharpen less where local contrast is already high.
vec3 sharpen(vec2 uv, vec3 c, float amount) {
    vec3 a = textureLod(uSrc, uv + vec2(0.0, -uTexel.y), 0.0).rgb, b = textureLod(uSrc, uv + vec2(-uTexel.x, 0.0), 0.0).rgb;
    vec3 d = textureLod(uSrc, uv + vec2(uTexel.x, 0.0), 0.0).rgb, e = textureLod(uSrc, uv + vec2(0.0, uTexel.y), 0.0).rgb;
    vec3 mn = min(min(min(a, b), min(c, d)), e), mx = max(max(max(a, b), max(c, d)), e);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-4), 0.0, 1.0));
    vec3 w = amp * (-1.0 / mix(8.0, 4.5, amount));
    return clamp((c + (a + b + d + e) * w) / (1.0 + 4.0 * w), 0.0, 1.0);
}
void main() {
    bool edge = false;
    vec3 c = uFxaa > 0.5 ? fxaa(vUV, edge) : textureLod(uSrc, vUV, 0.0).rgb;
    if (uSharpen > 0.001 && !edge) c = sharpen(vUV, c, uSharpen);
    vec2 d = vUV - 0.5;
    if (uChromatic > 0.0) {
        vec2 off = d * dot(d, d) * uChromatic * 0.045;
        c.r = mix(c.r, textureLod(uSrc, vUV - off, 0.0).r, 0.85);
        c.b = mix(c.b, textureLod(uSrc, vUV + off, 0.0).b, 0.85);
    }
    c *= 1.0 - uVignette * smoothstep(0.1, 0.75, dot(d, d) * 1.9);
    c = mix(c, vec3(0.74, 0.1, 0.08), uDamage * smoothstep(0.25, 0.75, length(d) * 1.5));
    c = mix(c, uFlash.rgb, clamp(uFlash.a, 0.0, 1.0));
    if (uGrain > 0.0) {
        float g = hash12(gl_FragCoord.xy + fract(uTime * 7.31) * vec2(193.0, 71.0)) + hash12(gl_FragCoord.yx * 1.31 + fract(uTime * 3.7) * 37.0) - 1.0;
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        c += g * uGrain * 0.045 * (1.0 - 0.7 * l);
    }
    c += (hash12(gl_FragCoord.xy + fract(uTime) * 91.0) - 0.5) / 255.0;
    oColor = vec4(c, 1.0);
}
)GLSL";

inline const char* kCopyFS = R"GLSL(
uniform sampler2D uSrc;
in vec2 vUV;
out vec4 oColor;
void main() { oColor = vec4(texture(uSrc, vUV).rgb, 1.0); }
)GLSL";

}  // namespace glsl
