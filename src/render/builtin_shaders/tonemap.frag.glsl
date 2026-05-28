#version 450

// Tonemap 片元 shader —— stage B PostProcessChain TonemapPass 路径，双
// sampler (HDR + bloom 末态) → swap-chain (BGRA8Unorm)。chain 含
// TonemapPass 时由它代替 06.03 的 passthrough 完成"离屏 HDR → swap-chain
// LDR"的最后一脚。
//
// v1.3.3 GAP-2026-05-27-tonemap-operator-selection：4 算子可选（ACES
// Narkowicz / AgX / Reinhard / Linear），通过 push constant uOperator 槽
// 位选择，shader 内 switch dispatch；与 PostProcessPasses.h 的 enum class
// TonemapOperator 对应。
//
// 顶点阶段对应 tonemap.vert.glsl，与 fullscreen.vert 内容一致——独立命
// 名让 GPU profiling 工具能区分 fullscreen passthrough 与 tonemap pass。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1) uniform sampler2D uBloomColor;

layout(push_constant, std430) uniform Push
{
    float uExposure;        // 来自 TonemapPass.exposure
    float uBloomIntensity;  // 来自 BloomPass.intensity（chain 没 BloomPass 时为 0）
    uint  uOperator;        // TonemapOperator enum: 0=ACES_Narkowicz / 1=AgX / 2=Reinhard / 3=Linear
    float uPad1;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// ---- ACES Narkowicz ---------------------------------------------------
// Krzysztof Narkowicz, "ACES Filmic Tone Mapping Curve", 2015。
// 5 系数拟合 ACES 算子；输入线性 HDR，输出 [0, 1] LDR。AAA 引擎 2015-2020
// 期间事实标准；对鲜艳饱和色（红 / 黄 / 紫）会偏色压暗。
vec3 ACESNarkowicz(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3(0.0), vec3(1.0));
}

// ---- AgX (Troy Sobotka minimal fit) -----------------------------------
// Troy Sobotka 设计的现代 tonemap，Blender 4.0+ 默认。分三段：input 矩阵
// 转 AgX color space → log space sigmoid 曲线 → output 矩阵转回 sRGB。
// 对饱和色处理远优于 ACES，过亮区域不偏色。
// Reference: https://github.com/sobotka/AgX-S2O3 (minimal fit)
vec3 AgXDefaultContrastApprox(vec3 x)
{
    // 6 系数多项式拟合 AgX log-space sigmoid，与 OCIO AgX_Base.spi1d 工作
    // 空间一致；性能等价于 ACES Narkowicz 一阶 ALU。
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}

vec3 AgX(vec3 hdr)
{
    // Input matrix: sRGB → AgX wide gamut（与 Sobotka 公开矩阵一致）
    const mat3 agx_mat = mat3(
        0.842479062253094 , 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772 , 0.0784336              ,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );
    // Output matrix: AgX → sRGB
    const mat3 agx_mat_inv = mat3(
         1.19687900512017  , -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368,  1.15190312990417  , -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433,  1.15107367264116
    );

    // Log2 编码：min ev -12.47393 (= 0.0001), max ev 4.026069 (= ~16.3)
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;

    vec3 v = agx_mat * hdr;
    v = clamp(log2(v), min_ev, max_ev);
    v = (v - min_ev) / (max_ev - min_ev);  // normalize to [0, 1]
    v = AgXDefaultContrastApprox(v);
    v = agx_mat_inv * v;
    return clamp(v, vec3(0.0), vec3(1.0));
}

// ---- Reinhard ---------------------------------------------------------
// Reinhard et al. 2002 经典 `x / (1 + x)` per-channel。学术 baseline；HDR
// 高光被强压，无饱和色保护，所有颜色统一压缩。适合调试 / 风格化对照。
vec3 Reinhard(vec3 x)
{
    return clamp(x / (vec3(1.0) + x), vec3(0.0), vec3(1.0));
}

// ---- Linear -----------------------------------------------------------
// 不做曲线压缩，直接 clamp 到 [0, 1]。HDR > 1 像素硬 clamp，emissive 物
// 体边缘"硬边亮带染色"。仅用于调试 raw HDR 值是否符合预期，不适合
// shipping。
vec3 LinearClamp(vec3 x)
{
    return clamp(x, vec3(0.0), vec3(1.0));
}

// ---- Dispatch ---------------------------------------------------------
vec3 ApplyTonemap(vec3 hdr, uint op)
{
    // 所有 fragment 共享同一 push constant，switch 无 divergence；GPU 静
    // 态分支预测，性能等价 if-elseif 链。
    switch (op)
    {
        case 0u: return ACESNarkowicz(hdr);
        case 1u: return AgX(hdr);
        case 2u: return Reinhard(hdr);
        case 3u: return LinearClamp(hdr);
        default: return ACESNarkowicz(hdr);  // unknown enum → ACES 兜底
    }
}

void main()
{
    vec3 hdr   = texture(uHdrColor,   vUV).rgb;
    vec3 bloom = texture(uBloomColor, vUV).rgb;

    // 复合 = HDR + bloom * intensity，再喂 exposure 给所选 tonemap 算子。
    vec3 combined = (hdr + bloom * pc.uBloomIntensity) * pc.uExposure;
    outColor = vec4(ApplyTonemap(combined, pc.uOperator), 1.0);
}
