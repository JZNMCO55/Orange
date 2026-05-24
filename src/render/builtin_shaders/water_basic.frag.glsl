#version 450
#extension GL_GOOGLE_include_directive : require

// 内置 water_basic 模板：复用 pbr.vert SPV，frag 端做"水面近似"——
// 时间驱动 UV 扰动模拟波纹 + Schlick Fresnel 边缘高光 + 主光 N·L 漫反
// 射 + PCF shadow 接收。**无真实折射 / 反射 / FFT 波形**——那属于完整
// "water" shader 范畴（未来 v1.x 单独 milestone）。本 template 提供"小水
// 池 / 浅水洼 / 装饰水面"的视觉。
//
// 时间从 light.uFrameInfo.x（per-frame 秒数）取，不需要 per-instance
// uniform；颜色 / 波幅 / 速度三参数 per-instance 调（uBaseColor +
// uMRA 各分量复用）。
//
// uMRA 字段重新解释（与 pbr 不冲突，因为 unlit/water 各自 .template.json
// 独立声明 editor metadata）：
//   * uMRA.x = wave amplitude    （UV 扰动幅度，建议 0.005 ~ 0.05）
//   * uMRA.y = wave speed         （时间乘子，建议 0.5 ~ 2.0）
//   * uMRA.z = fresnel strength   （边缘高光强度，建议 0.3 ~ 1.0）
//   * uMRA.w = reserved

#include "include/shadow_pcf.glsl.inc"

layout(set = 0, binding = 0) uniform sampler2D uShadowMap;
layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;
    vec4 uLightDirIntensity;
    vec4 uLightColor;
    vec4 uShadowParams;
    vec4 uCameraWorldPos;
    vec4 uFrameInfo;          // x = time seconds（water 自驱波纹时间）
} light;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec4 vBaseColor;
layout(location = 4) in vec4 vMRA;

layout(location = 0) out vec4 outColor;

// 简易 2D 噪声（伪随机 sin 组合）—— 比 Perlin 简单几十倍，对装饰性水
// 纹够用；视觉上呈现连续波动而非随机像素。
float Hash21(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 WaveOffset(vec2 uv, float t, float amp, float speed)
{
    // 两层不同频率 sin 波叠加，相位偏移让纹理"流动"而非固定振动。
    float w1 = sin(uv.x * 12.0 + t * speed * 1.3 + uv.y * 5.0) * 0.5;
    float w2 = sin(uv.y * 9.0  + t * speed * 0.9 + uv.x * 7.0) * 0.5;
    return vec2(w1, w2) * amp;
}

// v1.2.7 patch · Caustics 亮度斑（光在水底反射形成的网状光斑）。多层
// sin 叠加产生 0-1 范围的亮度图案；与 normal 扰动正交，让"水波动画"在
// 视线方向 / 法线对齐都不理想（如立面 plane）的场景下仍肉眼可见。
float Caustics(vec2 uv, float t, float speed)
{
    float c1 = sin(uv.x * 30.0 + t * speed * 1.5 + uv.y * 18.0);
    float c2 = sin(uv.y * 25.0 + t * speed * 1.2 + uv.x * 22.0);
    float c3 = sin((uv.x + uv.y) * 18.0 - t * speed * 0.8);
    return (c1 + c2 + c3) / 3.0 * 0.5 + 0.5;  // 归一化到 0..1
}

void main()
{
    const float t        = light.uFrameInfo.x;
    const float waveAmp  = max(vMRA.x, 0.001);
    const float waveSpd  = max(vMRA.y, 0.001);
    const float fresStr  = clamp(vMRA.z, 0.0, 1.0);

    // 扰动 UV，再用扰动后的 UV 派生"虚拟法线"做简易漫反射。这是不引
    // 入真法线贴图的廉价 wave normal 近似。
    vec2 uvWarp = vUV + WaveOffset(vUV, t, waveAmp, waveSpd);

    // 从扰动梯度近似 normal（central diff）。v1.2.7 patch · 系数由 10 升
    // 到 50，让法线扰动幅度在 default Wave Amplitude 0.015 时仍肉眼可见。
    vec2 offX = WaveOffset(vUV + vec2(0.01, 0.0), t, waveAmp, waveSpd);
    vec2 offY = WaveOffset(vUV + vec2(0.0, 0.01), t, waveAmp, waveSpd);
    vec3 N = normalize(vNormal + vec3(
        (offX.x - WaveOffset(vUV, t, waveAmp, waveSpd).x) * 50.0,
        (offY.y - WaveOffset(vUV, t, waveAmp, waveSpd).y) * 50.0,
        0.0));

    // 漫反射 N·L（主光方向已存储为 uLightDirIntensity.xyz）。
    vec3 L = normalize(-light.uLightDirIntensity.xyz);
    float NdotL = max(dot(N, L), 0.0);
    vec3  lightCol = light.uLightColor.rgb * light.uLightDirIntensity.w;

    // v1.2.7 patch · Caustics 亮度斑乘到 baseColor —— 让 "立面水 plane"
    // 或法线扰动方向不利的场景下，"水波动画" 仍通过亮度变化可见。0.7
    // ~ 1.3 范围 ≈ ±30% 亮度起伏，足够肉眼辨识。
    float caustics = mix(0.7, 1.3, Caustics(uvWarp, t, waveSpd));
    vec3  baseLit  = vBaseColor.rgb * caustics;

    vec3  diffuse  = baseLit * lightCol * NdotL;

    // Schlick Fresnel 边缘高光（view 方向越接近切向越亮）。
    vec3 V = normalize(light.uCameraWorldPos.xyz - vWorldPos);
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, 5.0) * fresStr;
    vec3  edge = vec3(fresnel) * lightCol;

    // shadow factor —— 1 = 全亮，0 = 全阴影；与 textured.frag 同源 PCF。
    float shadow = SamplePcfShadow(uShadowMap, vWorldPos,
                                   light.uLightViewProj,
                                   int(light.uShadowParams.x),
                                   light.uShadowParams.y);
    float lighting = mix(0.4, 1.0, shadow);

    vec3 lit = (diffuse + edge) * lighting + baseLit * 0.15;  // 底色环境光基线
    outColor = vec4(lit, 1.0);
}
