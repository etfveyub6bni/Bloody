// GLSL sources. Everything targets OpenGL 3.3 core.
#pragma once

namespace glsl {

inline const char* kVersion = "#version 330 core\n";

inline const char* kNoise = R"GLSL(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float hash13(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
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
uniform float uSkyLum;
uniform float uTime;
uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowMat;
uniform vec2 uShadowParams;  // x: texel size, y: enabled
uniform vec4 uLightPos[4];   // xyz position, w radius
uniform vec4 uLightColor[4];
uniform int uLightCount;

vec3 skyGradient(vec3 d) {
    float h = d.z;
    vec3 col = mix(uSkyHorizon, uSkyZenith, pow(clamp(h, 0.0, 1.0), 0.55));
    col = mix(col, uGroundColor, smoothstep(0.0, -0.3, h));
    float sd = max(dot(d, uSunDir), 0.0);
    col += uSunColor * (0.018 * pow(sd, 6.0) + 0.05 * pow(sd, 48.0));
    return col;
}

vec3 envSpecular(vec3 r, float rough) {
    vec3 s = skyGradient(r);
    vec3 avg = mix(uGroundColor, uSkyZenith, 0.5 + 0.5 * r.z) * 0.8;
    return mix(s, avg, rough * rough);
}

const vec2 kPoisson[12] = vec2[](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
    vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
    vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

float shadowFactor(vec3 wpos, vec3 n) {
    if (uShadowParams.y < 0.5) return 1.0;
    float ndl = dot(n, uSunDir);
    vec3 p = wpos + n * (1.0 + 2.0 * (1.0 - abs(ndl)));
    vec4 sp = uShadowMat * vec4(p, 1.0);
    vec3 c = sp.xyz / sp.w;
    if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;
    float ang = hash12(gl_FragCoord.xy) * 6.2831853;
    mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
    float r = 1.5 * uShadowParams.x;
    float s = 0.0;
    for (int i = 0; i < 12; i++) s += texture(uShadowMap, vec3(c.xy + rot * kPoisson[i] * r, c.z - 0.00025));
    return s / 12.0;
}

vec2 envBRDF(float NoV, float rough) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 shadeSurface(vec3 albedo, float rough, float metal, vec3 N, vec3 V, vec3 P, vec3 indirect, float ao, float shadow) {
    rough = clamp(rough, 0.05, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 diffCol = albedo * (1.0 - metal);
    float NoV = max(dot(N, V), 1e-4);
    vec3 col = vec3(0.0);
    float NoL = dot(N, uSunDir);
    if (NoL > 0.0 && shadow > 0.001) {
        vec3 H = normalize(uSunDir + V);
        float NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
        float a = rough * rough, a2 = a * a;
        float d = NoH * NoH * (a2 - 1.0) + 1.0;
        float D = a2 / (PI * d * d);
        float k = (rough + 1.0) * (rough + 1.0) / 8.0;
        float G = (NoV / (NoV * (1.0 - k) + k)) * (NoL / (NoL * (1.0 - k) + k));
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
        vec3 spec = D * G * F / max(4.0 * NoV * NoL, 1e-4);
        col += (diffCol / PI * (1.0 - F) + spec) * uSunColor * NoL * shadow;
    }
    for (int i = 0; i < uLightCount; i++) {
        vec3 Lv = uLightPos[i].xyz - P;
        float dist = length(Lv);
        float att = clamp(1.0 - dist / uLightPos[i].w, 0.0, 1.0);
        att *= att;
        col += diffCol * uLightColor[i].rgb * max(dot(N, Lv / max(dist, 1e-3)), 0.0) * att;
    }
    vec2 ab = envBRDF(NoV, rough);
    float envScale = clamp(dot(indirect, vec3(0.3, 0.59, 0.11)) / max(uSkyLum, 1e-3), 0.04, 1.0);
    vec3 specAmb = envSpecular(reflect(-V, N), rough) * (F0 * ab.x + ab.y) * envScale;
    float specOcc = clamp(pow(NoV + ao, exp2(-16.0 * rough - 1.0)) - 1.0 + ao, 0.0, 1.0);
    col += diffCol * indirect * ao + specAmb * specOcc;
    return col;
}

vec3 applyFog(vec3 col, vec3 P) {
    vec3 d = P - uCamPos;
    float dist = length(d);
    float f = 1.0 - exp(-dist * uFogDensity);
    vec3 dir = d / max(dist, 1e-3);
    vec3 fc = uFogColor + uSunColor * 0.03 * pow(max(dot(dir, uSunDir), 0.0), 8.0);
    return mix(col, fc, clamp(f, 0.0, 1.0));
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
in vec3 vPos; in vec3 vNormal; in vec2 vUV; in vec4 vTangent; in vec4 vLight; in vec3 vTint;
flat in float vLayer;
out vec4 oColor;
void main() {
    vec4 alb = texture(uAlbedoTex, vec3(vUV, vLayer));
    vec4 nm = texture(uNormalTex, vec3(vUV, vLayer));
    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
    vec3 B = cross(N, T) * vTangent.w;
    vec2 nxy = nm.xy * 2.0 - 1.0;
    vec3 n = normalize(T * nxy.x + B * nxy.y + N * sqrt(max(1.0 - dot(nxy, nxy), 0.0)));
    vec3 albedo = alb.rgb * vTint;
    float ao = vLight.a * mix(1.0, nm.b, 0.8);
    vec3 V = normalize(uCamPos - vPos);
    float sh = shadowFactor(vPos, N);
    vec3 col = shadeSurface(albedo, alb.a, nm.a, n, V, vPos, vLight.rgb, ao, sh);
    oColor = vec4(applyFog(col, vPos), 1.0);
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
    gl_Position = uViewProj * (uModel * uBones[b] * vec4(aPos, 1.0));
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
    float sd = dot(dir, uSunDir);
    col += uSunColor * smoothstep(0.99950, 0.99972, sd) * 40.0;
    if (dir.z > 0.0) {
        vec2 uv = dir.xy / (dir.z + 0.12) * 1.3 + vec2(uTime * 0.004, uTime * 0.0016);
        float n = fbm2(uv * 1.4);
        float c = smoothstep(0.62 - uCloudiness * 0.25, 0.92, n) * smoothstep(0.0, 0.2, dir.z);
        vec3 lit = uSkyHorizon * 1.15 + uSunColor * (0.10 + 0.25 * pow(max(sd, 0.0), 3.0));
        col = mix(col, lit, c * 0.85);
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

inline const char* kCompositeFS = R"GLSL(
uniform sampler2D uHdr;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uSaturation;
uniform float uContrast;
uniform float uVignette;
uniform vec3 uGrade;
uniform vec4 uFlash;
uniform float uDamage;
uniform float uDesaturate;
uniform float uTime;
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
vec3 toSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main() {
    vec3 c = texture(uHdr, vUV).rgb + texture(uBloom, vUV).rgb * uBloomStrength;
    c = aces(c * uExposure * 1.1);
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l), c, uSaturation * (1.0 - uDesaturate));
    c = clamp((c - 0.18) * uContrast + 0.18, 0.0, 1.0) * uGrade;
    vec2 d = vUV - 0.5;
    c *= 1.0 - uVignette * dot(d, d) * 1.5;
    c = mix(c, vec3(0.55, 0.02, 0.02), uDamage * smoothstep(0.25, 0.75, length(d) * 1.5));
    c = mix(c, uFlash.rgb, clamp(uFlash.a, 0.0, 1.0));
    c = toSrgb(clamp(c, 0.0, 1.0));
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
