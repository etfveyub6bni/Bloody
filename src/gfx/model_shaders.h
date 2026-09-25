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
void main() {
    vec3 albedo = toLinear(vColor.rgb) * uTint.rgb;
    float rough = vMat.x, metal = vMat.y;
    int pattern = int(vMat.z * 255.0 + 0.5);
    vec3 N = normalize(vNormal);
    float emissive = 0.0;
    if (pattern == 1) {            // wood grain (grain runs along model X)
        float g = fbm3(vObj * vec3(0.35, 5.0, 5.0));
        float rings = sin(vObj.y * 7.0 + vObj.z * 5.0 + g * 9.0) * 0.5 + 0.5;
        albedo *= mix(0.70, 1.12, rings) * mix(0.88, 1.06, vnoise3(vObj * vec3(0.8, 12.0, 12.0)));
        rough = clamp(rough + (rings - 0.5) * 0.2, 0.05, 1.0);
    } else if (pattern == 2) {     // woven fabric
        float w = sin(vObj.x * 38.0) * sin((vObj.y + vObj.z) * 38.0);
        albedo *= 0.92 + 0.06 * w + (fbm3(vObj * 1.5) - 0.5) * 0.3;
    } else if (pattern == 3) {     // camo fabric
        float c1 = fbm3(vObj * 0.28 + 3.1), c2 = fbm3(vObj * 0.42 + 9.7);
        albedo = mix(albedo, toLinear(uPatternA), smoothstep(0.47, 0.51, c1));
        albedo = mix(albedo, toLinear(uPatternB), smoothstep(0.54, 0.58, c2));
        float w = sin(vObj.x * 38.0) * sin((vObj.y + vObj.z) * 38.0);
        albedo *= 0.93 + 0.05 * w;
    } else if (pattern == 4) {     // stippled polymer grip
        float s = vnoise3(vObj * 14.0);
        albedo *= 0.88 + 0.22 * s;
        rough = clamp(rough + (s - 0.5) * 0.35, 0.05, 1.0);
    } else if (pattern == 5) {     // worn blued steel
        float n = fbm3(vObj * 2.2);
        float scratch = smoothstep(0.62, 0.72, vnoise3(vObj * vec3(1.5, 30.0, 30.0)));
        albedo *= 0.8 + 0.35 * n + scratch * 0.6;
        rough = clamp(rough + (n - 0.5) * 0.3 - scratch * 0.2, 0.05, 1.0);
    } else if (pattern == 6) {     // leather / glove
        float n = vnoise3(vObj * 9.0), m = fbm3(vObj * 1.3);
        albedo *= 0.85 + 0.2 * n + (m - 0.5) * 0.25;
        rough = clamp(rough + (n - 0.5) * 0.25, 0.05, 1.0);
    } else if (pattern == 7) {     // skin
        albedo *= 0.94 + 0.12 * fbm3(vObj * 3.0);
    } else if (pattern == 8) {     // emissive
        emissive = 1.0;
    } else if (pattern == 9) {     // lens glass
        rough = 0.05; metal = 0.0;
    }
    vec3 V = normalize(uCamPos - vPos);
    float sh = shadowFactor(vPos, N);
    vec3 indirect = mix(uAmbDown, uAmbUp, N.z * 0.5 + 0.5);
    float ao = 1.0;
    if (uIsViewmodel > 0.5) indirect *= 1.35;
    vec3 col = shadeSurface(albedo, rough, metal, N, V, vPos, indirect, ao, sh);
    // Viewmodel fill light from the camera keeps hands and weapons readable, as in CS2.
    if (uIsViewmodel > 0.5) col += albedo * (1.0 - metal * 0.6) * (uAmbUp * 0.9 + vec3(0.22)) * pow(max(dot(N, V), 0.0), 1.5);
    col += albedo * emissive * 6.0;
    if (uIsViewmodel < 0.5) col = applyFog(col, vPos);
    oColor = vec4(col, 1.0);
}
)GLSL";

}  // namespace glsl
