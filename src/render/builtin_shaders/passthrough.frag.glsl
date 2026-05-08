#version 450

// 内置 passthrough 片元 shader：直接把离屏 HDR scene color 采样写到目
// 标 attachment（swap-chain），无 tonemap / 无 vignette / 无 LUT。
//
// 双段 frame 流程的 stage B 默认收尾：当 PostProcessChain 为空、或仅含
// HdrPass、或链尾 LutPass 持有无效 LUT handle 时走本 fragment——视觉
// 等价于"Pipeline 渲到 HDR → HDR 直接复制到屏幕"。HDR 像素超过 [0, 1]
// 时由 BGRA8Unorm 的 swap-chain attachment 自动 clamp，与 06.02 完成态
// 在 LDR 域字节级一致。
//
// descriptor set 0 binding 0 = combined image sampler，由 Pipeline 在
// 每帧 / 每次 resize 后 UpdateDescriptorSet 喂入当前的 HDR view + sampler。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(uHdrColor, vUV);
}
