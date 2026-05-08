#version 450

// Tonemap 片元 shader —— ACES Narkowicz fit + 双 sampler（HDR + bloom
// 末态）→ 写到 swap-chain（BGRA8Unorm）。Phase 3 / Task 06.05 的链尾收
// 尾路径——chain 含 TonemapPass 时由它代替 06.03 的 passthrough 完成
// "离屏 HDR → swap-chain LDR" 的最后一脚。
//
// 算子选 Narkowicz 5 行 fit：单步 ALU、不需要 LUT，与卡通 / 平台跳跃类
// 视觉风格契合。LUT-based 真色彩分级（17×17×17 unrolled）等到 Phase 6
// 资产管线就绪再切 LutPass 实施；本阶段 LutPass 是 no-op。
//
// 顶点阶段对应 tonemap.vert.glsl，与 fullscreen.vert 内容一致——独立
// 命名让 GPU profiling 工具能区分 fullscreen passthrough 与 tonemap pass。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1) uniform sampler2D uBloomColor;

layout(push_constant, std430) uniform Push
{
    float uExposure;        // 来自 TonemapPass.exposure
    float uBloomIntensity;  // 来自 BloomPass.intensity（chain 没 BloomPass 时为 0）
    float uPad0;
    float uPad1;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 ACESNarkowicz(vec3 x)
{
    // Krzysztof Narkowicz, "ACES Filmic Tone Mapping Curve", 2015。
    // 5 系数拟合 ACES 算子；输入线性 HDR，输出 [0, 1] LDR。
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3(0.0), vec3(1.0));
}

void main()
{
    vec3 hdr   = texture(uHdrColor,   vUV).rgb;
    vec3 bloom = texture(uBloomColor, vUV).rgb;

    // 复合 = HDR + bloom * intensity，再喂 exposure 给 ACES。
    vec3 combined = (hdr + bloom * pc.uBloomIntensity) * pc.uExposure;
    outColor = vec4(ACESNarkowicz(combined), 1.0);
}
