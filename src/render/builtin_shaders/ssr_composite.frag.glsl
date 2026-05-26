#version 450

// SSR 合成 pass —— 把 ssr pass 算出的 (反射色 × 权重) 加性 blend 进 HDR
// （pipeline 端 srcOne/dstOne，与 god rays / bloom upsample 同款加性）。
// 用独立 ssrColor 输入避免"采 HDR 同时写 HDR"的 framebuffer 反馈。
// 加性叠加（非 lerp 替换）→ 反射呈"光泽 sheen"，适合 stylized 湿表面。

layout(set = 0, binding = 0) uniform sampler2D uSsr;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(texture(uSsr, vUV).rgb, 1.0);
}
