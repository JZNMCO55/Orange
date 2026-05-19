#version 450

// 内置 passthrough 片元 shader：把离屏 HDR scene color 经 ACES Narkowicz
// tonemap 写到目标 attachment（BGRA8Unorm viewportColor / swap-chain 等）。
//
// 双段 frame 流程的 stage B 默认收尾：当 PostProcessChain 为空、或仅含
// HdrPass、或链尾 LutPass 持有无效 LUT handle 时走本 fragment。OrangeEditor
// 的 RenderOffscreen 路径独占本 shader —— 不走 stage B 的 bloom + 主 tonemap
// 路径，所以本 shader 必须自带 ACES 把 HDR 压回 [0, 1]，否则 HDR > 1 像素
// 被 BGRA8Unorm 硬 clamp，emissive 物体看起来"硬边亮带染色"，PBR 物体没
// 经曲线提亮 → 偏暗。
//
// 选 Narkowicz 5 行 fit：与 tonemap.frag.glsl 同款算子，无 LUT 依赖。
//
// **历史变迁**：v0.x baseline 阶段本 shader 是纯 copy（HDR 直接写 BGRA8
// 让 PostProcessChain 走 tonemap.frag 做曲线）。但编辑器 RenderOffscreen
// 没接 chain bloom+tonemap 走 passthrough → emissive 硬 clamp + PBR 偏暗。
// 改成自带 ACES 后两条 sample 路径（编辑器 off-screen / sample 主 swap-chain）
// 都得到正确的 HDR → LDR 处理：
//   * 编辑器：本 shader 直接 tonemap → viewportColor（无 bloom，HDR emissive
//     边缘仍稍硬但至少不 clamp 染色）；
//   * sample 主 swap-chain：仍走 stage B bloom + tonemap，本 passthrough
//     shader 不被采用（passthrough_combine.frag 单独路径）。
//
// descriptor set 0 binding 0 = combined image sampler，Pipeline 在每帧 /
// resize 后 UpdateDescriptorSet 喂入当前 HDR view + sampler。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;

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
    vec3 hdr = texture(uHdrColor, vUV).rgb;
    // exposure 默认 1.0：调用方未来想暴露曝光控制时升级为 push constant。
    outColor = vec4(ACESNarkowicz(hdr), 1.0);
}
