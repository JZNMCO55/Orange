#version 450

// Bloom upsample shader —— 9-tap tent filter，把更小的 mip 上采到当前
// 目标 mip 上做 add（pipeline 走 additive blend，shader 输出当前 sample
// 结果，色彩混合器累加到已有内容里）。
//
// tent kernel 权重（中心 4 / 邻 2 / 角 1，归一化）：
//     1 2 1
//     2 4 2     / 16
//     1 2 1
//
// 顶点阶段复用 fullscreen.vert——同 bloom_downsample.frag 一致。

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(uSource, 0));

    vec3 a = texture(uSource, vUV + texelSize * vec2(-1.0, -1.0)).rgb;
    vec3 b = texture(uSource, vUV + texelSize * vec2( 0.0, -1.0)).rgb;
    vec3 c = texture(uSource, vUV + texelSize * vec2( 1.0, -1.0)).rgb;

    vec3 d = texture(uSource, vUV + texelSize * vec2(-1.0,  0.0)).rgb;
    vec3 e = texture(uSource, vUV).rgb;
    vec3 f = texture(uSource, vUV + texelSize * vec2( 1.0,  0.0)).rgb;

    vec3 g = texture(uSource, vUV + texelSize * vec2(-1.0,  1.0)).rgb;
    vec3 h = texture(uSource, vUV + texelSize * vec2( 0.0,  1.0)).rgb;
    vec3 i = texture(uSource, vUV + texelSize * vec2( 1.0,  1.0)).rgb;

    vec3 result = e * 4.0
                + (b + d + f + h) * 2.0
                + (a + c + g + i);
    result *= (1.0 / 16.0);

    // additive blend 由 pipeline color attachment 配置（Src=One Dst=One Add）
    // 完成；fragment 直接输出 sample 结果即可。
    outColor = vec4(result, 1.0);
}
