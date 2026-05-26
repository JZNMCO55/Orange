#version 450

// SSAO 合成 pass —— 对原始(带噪声 tile)AO 做 4×4 box 模糊去掉 noise
// 旋转产生的网格状噪声,输出灰度 AO。pipeline 端用乘法 blend
// (srcFactor=Zero, dstFactor=SrcColor)把 HDR × AO,所以本 shader 直接
// 输出 vec3(ao)。4×4 与 host 端 4×4 noise tile 对齐,刚好抹平。
//
// 注:前向渲染下 AO 乘到的是"已含直接光的 HDR",而非仅环境项 —— 这是
// 无 G-buffer 的简化(会轻微压暗直接光的凹处)。视觉可接受;若要只作用
// 于 ambient,需 depth prepass / MRT 分离 ambient,留作后续。

layout(set = 0, binding = 0) uniform sampler2D uAO;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2  texel = 1.0 / vec2(textureSize(uAO, 0));
    float sum   = 0.0;
    // 4×4 box（偏移 -2..1）抹平 noise tile。
    for (int x = -2; x < 2; ++x)
    {
        for (int y = -2; y < 2; ++y)
        {
            sum += texture(uAO, vUV + vec2(float(x), float(y)) * texel).r;
        }
    }
    float ao = sum / 16.0;
    outColor = vec4(vec3(ao), 1.0);  // 乘法 blend: HDR *= ao
}
