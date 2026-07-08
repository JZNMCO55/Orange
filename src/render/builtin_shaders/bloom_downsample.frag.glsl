#version 450

// Bloom downsample shader —— Call of Duty: Advanced Warfare 风格 13-tap
// 采样 + Karis 平均防 fireflies。每跳把上一级 mip 缩到一半。
//
// 13 个 tap 分两组：
//   - 中心组：center + 8 个相邻 1-texel 偏移（共 9 tap）→ 取 4 个 2x2 平均块
//   - 外围组：4 个 (±2 texels, ±2 texels) 偏移块 → 4 个 2x2 平均块
// 总共 5 个 2x2 块按 COD AW 论文给出的权重（0.125 / 0.5 等）合成一个
// 单像素输出。
//
// 第一跳（HDR → bloom_mip[0]）走 bright-pass：超过 uThreshold 的部分才
// 进入 bloom 累加；后续跳设 uThreshold == 0，相当于不做 bright-pass。
// Karis 平均（按 1 / (1 + luma) 加权）抑制单点亮像素的 firefly artifact。
//
// 顶点阶段复用 fullscreen.vert（big-triangle gl_VertexIndex 驱动），
// 不需要单独的 bloom_downsample.vert——`fullscreenVs` 模块在 Pipeline
// 里同时被 passthrough / bloom downsample / bloom upsample / composite
// 复用。

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant, std430) uniform Push
{
    float uThreshold;     //  4 字节  —— bright-pass 阈值，0 表示不做 bright-pass
    float uClampMax;      //  8      —— 亮源钳制上限（bright-pass 跳生效；<=0 不钳）
    float uPad1;          // 12
    float uPad2;          // 16  （std430 对齐 16 字节边界）
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

float Luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 KarisAverage(vec3 a, vec3 b, vec3 c, vec3 d)
{
    // 按 1 / (1 + luma) 加权 4 个 sample —— 高亮 firefly 权重被压低，
    // 防止下采时单像素 spike 被放大到 mip 链。
    float wa = 1.0 / (1.0 + Luminance(a));
    float wb = 1.0 / (1.0 + Luminance(b));
    float wc = 1.0 / (1.0 + Luminance(c));
    float wd = 1.0 / (1.0 + Luminance(d));
    float total = wa + wb + wc + wd;
    return (a * wa + b * wb + c * wc + d * wd) / total;
}

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(uSource, 0));

    // 13 tap 偏移（论文图示）：
    //   外围组：(±2, ±2)
    //   中心组：(0, 0) + (±1, ±1) + (±1, 0) + (0, ±1)
    vec3 a = texture(uSource, vUV + texelSize * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(uSource, vUV + texelSize * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(uSource, vUV + texelSize * vec2( 2.0, -2.0)).rgb;

    vec3 d = texture(uSource, vUV + texelSize * vec2(-1.0, -1.0)).rgb;
    vec3 e = texture(uSource, vUV + texelSize * vec2( 1.0, -1.0)).rgb;

    vec3 f = texture(uSource, vUV + texelSize * vec2(-2.0,  0.0)).rgb;
    vec3 g = texture(uSource, vUV).rgb;
    vec3 h = texture(uSource, vUV + texelSize * vec2( 2.0,  0.0)).rgb;

    vec3 i = texture(uSource, vUV + texelSize * vec2(-1.0,  1.0)).rgb;
    vec3 j = texture(uSource, vUV + texelSize * vec2( 1.0,  1.0)).rgb;

    vec3 k = texture(uSource, vUV + texelSize * vec2(-2.0,  2.0)).rgb;
    vec3 l = texture(uSource, vUV + texelSize * vec2( 0.0,  2.0)).rgb;
    vec3 m = texture(uSource, vUV + texelSize * vec2( 2.0,  2.0)).rgb;

    vec3 result;
    if (pc.uThreshold > 0.0)
    {
        // 第一跳（HDR → bloom_mip0）：每个 2x2 块走 Karis 平均；后再
        // 按 COD AW 5 块权重合成。
        vec3 block0 = KarisAverage(d, e, i, j);                 // 中心 4 sample
        vec3 block1 = KarisAverage(a, b, f, g);                 // 左上
        vec3 block2 = KarisAverage(b, c, g, h);                 // 右上
        vec3 block3 = KarisAverage(f, g, k, l);                 // 左下
        vec3 block4 = KarisAverage(g, h, l, m);                 // 右下

        // 论文权重：中心 0.5、四角 0.125。
        result = block0 * 0.5
               + block1 * 0.125
               + block2 * 0.125
               + block3 * 0.125
               + block4 * 0.125;

        // bright-pass：每分量减阈值，clamp 到 0+。
        result = max(result - vec3(pc.uThreshold), vec3(0.0));

        // 亮源钳制：HDR 环境里的极亮源（夜景路灯等百量级 radiance）不封顶
        // 会在 mip 链扩散成巨型光晕；钳到 uClampMax 保留"发光"但止住白团。
        if (pc.uClampMax > 0.0)
        {
            result = min(result, vec3(pc.uClampMax));
        }
    }
    else
    {
        // 后续跳：直接 5 块平均，权重同上、不再走 Karis（链尾 firefly
        // 已经被 mip0 的 Karis 抑制掉了）。
        vec3 block0 = (d + e + i + j) * 0.25;
        vec3 block1 = (a + b + f + g) * 0.25;
        vec3 block2 = (b + c + g + h) * 0.25;
        vec3 block3 = (f + g + k + l) * 0.25;
        vec3 block4 = (g + h + l + m) * 0.25;

        result = block0 * 0.5
               + block1 * 0.125
               + block2 * 0.125
               + block3 * 0.125
               + block4 * 0.125;
    }

    outColor = vec4(result, 1.0);
}
