#version 450

// 内置 emissive 片元 shader —— 极简：直接输出 HDR > 1 的常量色，让
// bloom pass 的 threshold extract 自动拾取产出光晕。无光照计算、无
// shadow 接收——这是 emissive surface 的核心特征。
//
// 颜色 / 强度 hardcode（per-instance uniform 路径上线后再下放）：
// kColor = 暖白；kIntensity 大到让 bloom 显著。

// set 0 layout 与主 pass 其它内置模板对齐——Pipeline::GetOrCompilePipeline
// 给每个 template pipeline 都声明 mainDescLayout，shader 即使不读
// binding 0/1 也得在 layout 里出现，否则 SetDescriptorSet 会失败。
// CSM additive：升 sampler2DArray，类型必须与 descriptor 一致。
layout(set = 0, binding = 0) uniform sampler2DArray uShadowMap;
layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;
    vec4 uLightDirIntensity;
    vec4 uLightColor;
    vec4 uShadowParams;
    vec4 uCameraWorldPos;
    vec4 uFrameInfo;
} light;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3  kColor     = vec3(1.0, 0.85, 0.55);
    const float kIntensity = 4.0;

    // UV 变化成轻微的 vignette 让平面不至于完全平：中心最亮、四角略暗。
    // 不计算光照——这是 emissive 的本质（自发光面）。
    float r = length(vUV - vec2(0.5)) * 1.4;
    float falloff = clamp(1.0 - r * 0.3, 0.6, 1.0);

    outColor = vec4(kColor * kIntensity * falloff, 1.0);
}
