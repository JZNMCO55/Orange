#version 450
#extension GL_GOOGLE_include_directive : require

// 内置 rim-light 片元 shader（Task 07 重构版）：fresnel 风格 rim glow，
// 主光参数从 light UBO 取，shadow factor 走 shadow_pcf 与 toon 同模式。
//
// rim 强度 / 颜色 / 幂指数 hardcode 为 OrangeEngine 的卡通主调（per-instance
// 调参留给 Phase 6 Material UBO）；view position 当前用 vModelPos 的退
// 化解，等 Pipeline 把 camera worldPos 喂进 frame UBO 后再升级。

#include "include/shadow_pcf.glsl.inc"

layout(set = 0, binding = 0) uniform sampler2D uShadowMap;

layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;
    vec4 uLightDirIntensity;
    vec4 uLightColor;
    vec4 uShadowParams;
    vec4 uCameraWorldPos;     // xyz = camera worldPos, w 未用
    vec4 uFrameInfo;          // x = time seconds, y/z/w 预留 —— rim_light 不用
} light;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vModelPos;

layout(location = 0) out vec4 outColor;

void main()
{
    // 同 toon.frag：Vulkan 屏幕 +Y 朝下，cross 顺序 dy×dx 取 outward。
    vec3 dx     = dFdx(vWorldPos);
    vec3 dy     = dFdy(vWorldPos);
    vec3 normal = normalize(cross(dy, dx));

    // 真 viewDir：world-space 由 frame UBO 的 cameraWorldPos 取。指向
    // surface → camera。
    vec3 viewDir = normalize(light.uCameraWorldPos.xyz - vWorldPos);

    // Rim：silhouette（normal ⊥ view）处取最大值。kRimIntensity 抬到
    // 1.6 让 rim 在亮内表面也压得住——"暗内 + 亮 orange 边"是 rim_light
    // 这套 builtin 的核心特征。
    const float kRimPower     = 2.5;
    const float kRimIntensity = 1.6;
    const vec3  kRimColor     = vec3(1.0, 0.65, 0.30);
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), kRimPower);

    // 主光 NdotL + 阴影因子；rim 不受阴影抑制（边沿光本身就是逆光时最亮）。
    vec3  lightDir = normalize(-light.uLightDirIntensity.xyz);
    float NdotL    = max(dot(normal, lightDir), 0.0);
    float shadow   = SamplePcfShadow(uShadowMap, vWorldPos,
                                     light.uLightViewProj,
                                     int(light.uShadowParams.x),
                                     light.uShadowParams.y);

    // 极小 ambient 防纯黑（背光面 + 阴影内仍可读 silhouette），main 项
    // 走标准 NdotL 不做 wrap——保留 rim_light 原本"暗内"风格。
    const vec3 kAmbient = vec3(0.03, 0.025, 0.05);
    vec3 mainLit = light.uLightColor.rgb * light.uLightDirIntensity.w * NdotL * shadow;
    vec3 color   = kAmbient + kRimColor * rim * kRimIntensity + mainLit * 0.30;

    outColor = vec4(color, 1.0);
}
