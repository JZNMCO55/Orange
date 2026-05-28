#version 450
#extension GL_GOOGLE_include_directive : require

// 内置 toon-shading 片元 shader（Task 07 重构版）：
//   * NdotL 走 light UBO 提供的 uLightDir + 颜色；
//   * shadow factor 走 shadow_pcf.glsl.inc，按 ShadowConfig 的 PCF 半径
//     与 depth bias 采样 set 0 binding 0 的 shadow map；
//   * 二阶 cel banding 阈值 hardcode 为 0.5（per-instance 调参留给
//     Phase 6 Material UBO）；
//   * warm / cool 双色 hardcode 为 OrangeEngine 主色阶（以前在 push
//     constant 里）；颜色定制留给后续 Material UBO。
//
// 顶点 shader 已经把 worldPos 算好透过 vWorldPos 传进来（Task 07 起
// uModel 进 push constant），fragment 端不需要 uModel。
//
// shadow_pcf.glsl.inc 走 sampler2D + textureLod 路径，与 sampler2DShadow
// 兼容——Pipeline 的 shadow sampler 当前是普通 linear sampler（非
// compare-mode），shader 自己做 currentDepth <= closestDepth 比较。

#include "include/shadow_pcf.glsl.inc"

// CSM additive（GAP-2026-05-27）：sampler 升 sampler2DArray，shadow 采样侧
// 改 Array 变体（固定 layer=0 = cascade 0；toon 不消费 CSM cascade selection，
// 始终采近段 cascade 即可，多 cascade 时远景不投影属可接受的 stylized 退化）。
layout(set = 0, binding = 0) uniform sampler2DArray uShadowMap;

layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;     //   0  64
    vec4 uLightDirIntensity; //  64  16  (xyz = direction, w = intensity)
    vec4 uLightColor;        //  80  16  (xyz = rgb, w 未用)
    vec4 uShadowParams;      //  96  16  (x = pcfKernelRadius, y = depthBias, z/w 未用)
    vec4 uCameraWorldPos;    // 112  16  (xyz = camera worldPos, w 未用) —— toon 不用
    vec4 uFrameInfo;         // 128  16  (x = time seconds, y/z/w 预留) —— toon 不用
} light;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vNormal;

layout(location = 0) out vec4 outColor;

void main()
{
    // world-space smooth normal —— GAP-2026-05-17 起 MeshAsset 携带
    // per-vertex normal，shader 直接读 vNormal 即可，不再依赖 dFdx/dFdy
    // 推 flat face normal。vert shader 已经把 normal 乘 mat3(uModel)
    // 翻到 world space（假设模型无非均匀缩放）；frag 只需 normalize。
    vec3 normal = normalize(vNormal);

    // -uLightDir 是 "从表面指向光源" 的方向；NdotL 越大代表越正对光。
    vec3  lightDir = normalize(-light.uLightDirIntensity.xyz);
    float NdotL    = max(dot(normal, lightDir), 0.0);

    // 阴影系数：1 = 完全照亮，0 = 完全阴影。
    float shadow = SamplePcfShadowArray(uShadowMap, 0, vWorldPos,
                                        light.uLightViewProj,
                                        int(light.uShadowParams.x),
                                        light.uShadowParams.y);

    // 三阶 cel banding：阈值 0.25 / 0.65 把 litness 划成 cool / mid / warm
    // 三段。比单阈 step(0.5) 多出一段中间色，cube 的多个面在常见光照
    // 角度下能形成清晰的"亮 / 半 / 暗"过渡，方向感更易读，但仍保留
    // toon 的硬阶过渡感。改色 / 改阈值留给 Phase 6 Material UBO。
    float litness = NdotL * shadow;
    float band    = step(0.25, litness) + step(0.65, litness);  // 0 / 1 / 2
    band         *= 0.5;                                          // → 0 / 0.5 / 1

    const vec3 kWarm = vec3(1.00, 0.55, 0.20);
    const vec3 kMid  = vec3(0.45, 0.32, 0.30);
    const vec3 kCool = vec3(0.10, 0.18, 0.32);
    vec3 base = (band < 0.25) ? kCool
              : (band < 0.75) ? kMid
              :                 kWarm;

    // 主光颜色 / 强度调制——与 Phase 3 视觉基线对齐：在 cool 区也保留
    // 一定主光色调。
    vec3 lit  = base * (light.uLightColor.rgb * light.uLightDirIntensity.w);
    outColor  = vec4(lit, 1.0);
}
