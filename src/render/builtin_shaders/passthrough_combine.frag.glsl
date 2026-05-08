#version 450

// Passthrough + Bloom composite —— 双段 frame 流程的 stage B 收尾，当
// PostProcessChain 含 BloomPass 时走本 fragment。把离屏 HDR scene color
// 与 bloom 末态（bloom_mip[0] 累加完成）按 intensity 加在一起，写到
// swap-chain。BGRA8Unorm 自动做 [0, 1] clamp。
//
// chain 不含 BloomPass 时 Pipeline 走 06.03 的纯 HDR `passthrough.frag.glsl`，
// 不需要 binding 1 的 bloom sampler。
//
// 顶点阶段复用 fullscreen.vert。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1) uniform sampler2D uBloomColor;

layout(push_constant, std430) uniform Push
{
    float uBloomIntensity;  // 与 BloomPass.intensity 一一对应；0 等价于无 bloom
    float uPad0;
    float uPad1;
    float uPad2;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 hdr   = texture(uHdrColor,   vUV).rgb;
    vec3 bloom = texture(uBloomColor, vUV).rgb;
    outColor   = vec4(hdr + bloom * pc.uBloomIntensity, 1.0);
}
