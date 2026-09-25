// GLSL for skinned/rigid models (weapons, arms, agents). Linked after kNoise and kLighting from shaders.h.
#pragma once

namespace glsl {

inline const char* kModelVS = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aMat;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uBones[48];
out vec3 vPos; out vec3 vNormal; out vec3 vObj; out vec4 vColor; out vec3 vMat;
void main() {
    int b = int(aMat.w * 255.0 + 0.5);
    mat4 M = uModel * uBones[b];
    vec4 wp = M * vec4(aPos, 1.0);
    vPos = wp.xyz;
    vNormal = mat3(M) * aNormal;
    vObj = aPos; vColor = aColor; vMat = aMat.xyz;
    gl_Position = uViewProj * wp;
}
)GLSL";

inline const char* kModelFS = R"GLSL(
uniform vec4 uTint;
uniform vec3 uAmbUp;
uniform vec3 uAmbDown;
uniform float uIsViewmodel;
uniform vec3 uPatternA;
uniform vec3 uPatternB;
in vec3 vPos; in vec3 vNormal; in vec3 vObj; in vec4 vColor; in vec3 vMat;
out vec4 oColor;
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }

// Bump mapping without tangents: perturbs N by the screen-space gradient of a height in world units.
// Derivatives must be taken in uniform control flow (outside the per-material branches), otherwise
// they are undefined at material borders inside a pixel quad and can produce NaNs.
vec3 bumpNormal(vec3 N, float h, vec3 dpdx, vec3 dpdy) {
    vec3 r1 = cross(dpdy, N), r2 = cross(N, dpdx);
    float det = dot(dpdx, r1);
    vec3 grad = sign(det) * (dFdx(h) * r1 + dFdy(h) * r2);
    float gl = length(grad), lim = abs(det) * 0.8;  // caps the tilt (height jumps at material borders)
    if (gl > lim) grad *= lim / max(gl, 1e-30);
    vec3 n = abs(det) * N - grad;
    float len = length(n);
    // Tiny or degenerate footprints (and material borders) fall back to the smooth normal.
    if (!(len > 1e-20) || abs(det) < 1e-16) return N;
    n /= len;
    return dot(n, N) < 0.2 ? N : n;
}
float g_pixelFootprint;
// Fades detail whose period is close to the pixel footprint (avoids sparkle).
float detailFade(float period) { return 1.0 - smoothstep(0.35, 1.0, g_pixelFootprint / period); }
float weave(vec3 q, float f) {
    return sin(q.x * f + sin(q.z * f * 0.5) * 0.5) * sin((q.y + q.z) * f);
}

// Like shadeSurface, but reflections come mostly from the local probe (walls, ground) instead of
// the open sky, so metal and varnish do not turn blue or purple in enclosed places.
vec3 shadeModel(vec3 albedo, float rough, float metal, vec3 N, vec3 V, vec3 P, vec3 indirect, float ao, float shadow) {
    vec3 direct = shadeSurface(albedo, rough, metal, N, V, P, vec3(0.0), 1.0, shadow);
    ao *= ssaoFactor();
    rough = clamp(rough, 0.05, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 diffCol = albedo * (1.0 - metal);
    float NoV = max(dot(N, V), 1e-4);
    vec3 R = reflect(-V, N);
    vec3 local = mix(uAmbDown, uAmbUp, clamp(R.z * 0.5 + 0.5, 0.0, 1.0)) * 1.15;
    float openSky = clamp(R.z * 1.6, 0.0, 1.0) * clamp(dot(uAmbUp, vec3(0.3, 0.59, 0.11)) / max(uSkyLum, 1e-3), 0.0, 1.0);
    vec3 env = mix(local, envSpecular(R, rough), openSky * 0.35 * (1.0 - rough));
    env = mix(env, vec3(dot(env, vec3(0.3, 0.59, 0.11))), 0.4);
    vec2 ab = envBRDF(NoV, rough);
    float specOcc = clamp(pow(NoV + ao, exp2(-16.0 * rough - 1.0)) - 1.0 + ao, 0.0, 1.0);
    return direct + diffCol * indirect * ao + env * (F0 * ab.x + ab.y) * specOcc;
}

void main() {
    vec3 albedo = toLinear(vColor.rgb) * uTint.rgb;
    float rough = vMat.x, metal = vMat.y;
    int pattern = int(vMat.z * 255.0 + 0.5);
    float ao = vColor.a;
    vec3 N = normalize(vNormal);
    vec3 q = vObj;
    // All screen-space derivatives are evaluated here, before any per-material branching.
    vec3 dpdx = dFdx(vPos), dpdy = dFdy(vPos);
    g_pixelFootprint = length(fwidth(vObj));
    float curvature = length(fwidth(N)) / max(length(fwidth(vPos)), 1e-5);
    float h = 0.0;
    float emissive = 0.0, sss = 0.0, sheen = 0.0;
    if (pattern == 1) {            // varnished wood, grain along model X
        float g = fbm3(q * vec3(0.16, 2.6, 2.6));
        float rings = sin(q.y * 6.0 + q.z * 4.0 + g * 11.0) * 0.5 + 0.5;
        float fine = vnoise3(q * vec3(0.5, 36.0, 36.0));
        float pores = smoothstep(0.62, 0.8, vnoise3(q * vec3(1.2, 55.0, 55.0)));
        albedo *= mix(0.55, 1.12, smoothstep(0.15, 0.85, rings)) * (0.9 + 0.16 * fine) * (1.0 - 0.25 * pores);
        rough = clamp(rough + (0.5 - rings) * 0.1 + pores * 0.15, 0.15, 0.95);
        h = (rings * 0.006 - pores * 0.004) * detailFade(0.08);
    } else if (pattern == 2) {     // woven fabric
        float w = weave(q, 42.0);
        float m = fbm3(q * 1.2);
        albedo *= 0.9 + 0.08 * w + (m - 0.5) * 0.3;
        rough = clamp(rough + 0.05 * w, 0.4, 1.0);
        h = w * 0.004 * detailFade(0.15) + (m - 0.5) * 0.02;
        sheen = 0.25;
    } else if (pattern == 3) {     // camo fabric
        float c1 = fbm3(q * 0.28 + 3.1), c2 = fbm3(q * 0.42 + 9.7);
        albedo = mix(albedo, toLinear(uPatternA), smoothstep(0.47, 0.51, c1));
        albedo = mix(albedo, toLinear(uPatternB), smoothstep(0.54, 0.58, c2));
        float w = weave(q, 42.0);
        float m = fbm3(q * 1.2);
        albedo *= 0.92 + 0.07 * w + (m - 0.5) * 0.22;
        h = w * 0.004 * detailFade(0.15) + (m - 0.5) * 0.02;
        sheen = 0.25;
    } else if (pattern == 4) {     // stippled polymer
        float s = vnoise3(q * 16.0), s2 = vnoise3(q * 41.0);
        albedo *= 0.92 + 0.12 * s;
        rough = clamp(rough + (s - 0.5) * 0.2, 0.2, 0.95);
        h = (s * 0.006 + s2 * 0.003) * detailFade(0.07);
    } else if (pattern == 5) {     // parkerized / blued steel with worn edges
        float n1 = fbm3(q * 1.1), n2 = vnoise3(q * 7.0), n3 = vnoise3(q * 38.0);
        albedo *= 0.82 + 0.3 * n1;
        rough = clamp(rough + (n1 - 0.5) * 0.22 + (n3 - 0.5) * 0.08, 0.12, 0.95);
        float wear = smoothstep(0.5, 0.9, smoothstep(6.0, 22.0, curvature) * (0.5 + n2));
        albedo = mix(albedo, vec3(0.42, 0.42, 0.44), wear * 0.8);
        metal = mix(metal, 1.0, wear);
        rough = mix(rough, 0.28, wear);
        h = n3 * 0.0025 * detailFade(0.05);
    } else if (pattern == 6) {     // leather / glove material
        float n = vnoise3(q * 22.0), m = fbm3(q * 1.6), c = vnoise3(q * 60.0);
        albedo *= 0.88 + 0.16 * n + (m - 0.5) * 0.25;
        rough = clamp(rough + (n - 0.5) * 0.2, 0.25, 0.95);
        h = (n * 0.005 + c * 0.002) * detailFade(0.08) + (m - 0.5) * 0.015;
        sheen = 0.1;
    } else if (pattern == 7) {     // skin
        float m = fbm3(q * 2.2), pores = vnoise3(q * 70.0);
        albedo *= 0.93 + 0.14 * m;
        albedo = mix(albedo, albedo * vec3(1.08, 0.9, 0.86), smoothstep(0.55, 0.75, fbm3(q * 0.9 + 4.0)) * 0.5);
        rough = clamp(0.5 + (pores - 0.5) * 0.15, 0.3, 0.8);
        h = pores * 0.0015 * detailFade(0.03) + (m - 0.5) * 0.004;
        sss = 1.0;
    } else if (pattern == 8) {     // emissive
        emissive = 1.0;
    } else if (pattern == 9) {     // lens glass
        rough = 0.04; metal = 0.0;
    } else if (pattern == 10) {    // brushed / machined bare metal
        float b = vnoise3(q * vec3(2.0, 90.0, 90.0));
        rough = clamp(rough + (b - 0.5) * 0.12, 0.08, 0.9);
        albedo *= 0.94 + 0.08 * b;
        h = b * 0.0015 * detailFade(0.02);
    }
    N = bumpNormal(N, h, dpdx, dpdy);

    vec3 V = normalize(uCamPos - vPos);
    float sh = shadowFactor(vPos, N);
    vec3 indirect = mix(uAmbDown, uAmbUp, N.z * 0.5 + 0.5);
    if (uIsViewmodel > 0.5) indirect *= 1.45;
    vec3 col = shadeModel(albedo, rough, metal, N, V, vPos, indirect, ao, sh * mix(1.0, ao, 0.35));
    float NoL = dot(N, uSunDir);
    if (sss > 0.0) {
        // Light bleeding past the terminator, reddened by scattering under the skin.
        float wrap = clamp((NoL + 0.45) / 1.45, 0.0, 1.0) - clamp(NoL, 0.0, 1.0);
        col += albedo * vec3(1.0, 0.5, 0.36) * uSunColor * wrap * 0.15 * sh;
        col += albedo * vec3(0.9, 0.4, 0.3) * indirect * 0.06 * ao;
    }
    if (sheen > 0.0) {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 4.0);
        col += albedo * (indirect + uSunColor * max(NoL, 0.0) * sh) * rim * sheen;
    }
    // Soft fill from the camera keeps viewmodels readable in dark corners, as in CS2.
    if (uIsViewmodel > 0.5) col += albedo * (1.0 - metal * 0.6) * (uAmbUp * 0.5 + vec3(0.07)) * pow(max(dot(N, V), 0.0), 2.0) * ao;
    col += albedo * emissive * 6.0;
    if (uIsViewmodel < 0.5) col = applyFog(col, vPos);
    // A single NaN or extreme highlight would be smeared into a large blob by bloom.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    oColor = vec4(min(col, vec3(48.0)), 1.0);
}
)GLSL";

}  // namespace glsl
