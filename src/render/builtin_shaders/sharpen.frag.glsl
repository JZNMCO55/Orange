#version 450

// 内置锐化（sharpen）pass —— CAS 式（AMD FidelityFX Contrast Adaptive Sharpening
// 简化版）自适应锐化，TAA 的标准搭档：TAA resolve 多帧累积会软化画面，本 pass
// 恢复高频细节。读 hdrColor 邻域写独立 sharpColor（与 DoF/lens 同款读写分离），
// composite（复用 dof_composite）再 replace 回 HDR。无 depth 依赖。
//
// CAS 思路：用 3×3 十字邻域的局部 min/max 估计局部对比，按"低对比区多锐、高对比
// 区（已是边缘）少锐"自适应权重，避免传统 unsharp 在强边缘 over-shoot 出黑白镶边。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1, std140) uniform SharpenUbo
{
    vec4 uParams;   // x=sharpness(0..1), y/z/w 预留
} sh;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
    float sharpness = sh.uParams.x;

    vec2 texel = 1.0 / vec2(textureSize(uHdrColor, 0));

    // 十字 + 中心 5 tap。
    vec3 c = texture(uHdrColor, vUV).rgb;
    if (sharpness < 1e-4)
    {
        outColor = vec4(c, 1.0);
        return;
    }
    vec3 up = texture(uHdrColor, vUV + vec2(0.0, -texel.y)).rgb;
    vec3 dn = texture(uHdrColor, vUV + vec2(0.0,  texel.y)).rgb;
    vec3 lf = texture(uHdrColor, vUV + vec2(-texel.x, 0.0)).rgb;
    vec3 rt = texture(uHdrColor, vUV + vec2( texel.x, 0.0)).rgb;

    // 局部 luma min/max → 对比度。CAS：低对比区给更高锐化增益。
    float lc = Luma(c);
    float mn = min(lc, min(min(Luma(up), Luma(dn)), min(Luma(lf), Luma(rt))));
    float mx = max(lc, max(max(Luma(up), Luma(dn)), max(Luma(lf), Luma(rt))));
    // 越接近黑/白边缘（mx 高、动态范围用满）增益越低，避免 over-shoot。
    float contrast = mx - mn;
    float adapt    = 1.0 - clamp(contrast, 0.0, 1.0);  // 低对比 → 接近 1

    // unsharp 核：中心减十字均值，按 sharpness × adapt 加回。
    vec3 blur  = (up + dn + lf + rt) * 0.25;
    vec3 sharp = c + (c - blur) * (sharpness * adapt);

    // clamp 到邻域 luma 范围放大一点，防极端 ringing。
    outColor = vec4(max(sharp, vec3(0.0)), 1.0);
}
