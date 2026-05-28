#version 450
#extension GL_GOOGLE_include_directive : require

// 内置 textured-mesh 片元 shader（Task 07 重构版）：从 uv 程序式合成
// 8×8 棋盘 + 微 tint，再乘 PCF 阴影系数 + 主光颜色——让 plane 类的"地
// 面"接收 cube / sphere 投下的 shadow，验证 shadow pass → 主 pass 端
// 到端贯通。
//
// 没装 DirectionalLight 的场景（sample 03 / 04）走 Pipeline 的 "中性
// 光 + shadow map 清成远深度" 路径：shadow_pcf 取 1.0 = 全亮，颜色
// = checker × 主光（默认白光），与 06.04 / 06.05 视觉等价。
//
// Phase 6 / Material UBO 上线后，调用方可以通过 MaterialInstance 覆盖
// "warm / cool 色阶" "棋盘 cell 数" 等参数；当前所有非 lighting / 非
// shadow 的参数 hardcode 进 shader。

#include "include/shadow_pcf.glsl.inc"

// CSM additive：升 sampler2DArray，固定 layer=0（同 toon 取舍）。
layout(set = 0, binding = 0) uniform sampler2DArray uShadowMap;

layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;
    vec4 uLightDirIntensity;
    vec4 uLightColor;
    vec4 uShadowParams;       // x = pcfKernelRadius, y = depthBias
    vec4 uCameraWorldPos;     // xyz = camera worldPos —— textured 不用，但 UBO layout 必须对齐
    vec4 uFrameInfo;          // x = time seconds —— textured 不用，但 UBO layout 必须对齐
} light;

layout(location = 0) in  vec2 vUV;
layout(location = 1) in  vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    // 8×8 棋盘 + uv 渐变 tint（保留与原 textured 视觉一致）
    vec2  cell = floor(vUV * 8.0);
    float odd  = mod(cell.x + cell.y, 2.0);
    vec3  warm = vec3(1.0, 0.55, 0.20);
    vec3  cool = vec3(0.10, 0.18, 0.32);
    vec3  base = mix(cool, warm, odd);
    base      *= 0.85 + 0.15 * vec3(vUV.x, vUV.y, 1.0 - vUV.x);

    // shadow factor —— 1 = 全亮，0 = 全阴影。
    float shadow = SamplePcfShadowArray(uShadowMap, 0, vWorldPos,
                                        light.uLightViewProj,
                                        int(light.uShadowParams.x),
                                        light.uShadowParams.y);

    // shadow 区域降到 30% 亮度（不全黑——让 checker pattern 仍可读）。
    float lighting = mix(0.3, 1.0, shadow);

    // 主光颜色乘到 checker；阴影区域用 lighting 因子做 attenuation。
    vec3 lit = base * (light.uLightColor.rgb * light.uLightDirIntensity.w) * lighting;
    outColor = vec4(lit, 1.0);
}
