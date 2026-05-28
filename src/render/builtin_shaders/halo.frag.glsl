#version 450

// 内置 halo 片元 shader —— GAP-2026-05-11 G3 可见光晕路径。极简：把
// vert stage 已经预乘强度的 vHaloColor 直接输出，HDR > 1 触发既有
// BloomPass 自然散光产生 glow（"billboard 自发光 sphere mesh + bloom
// 自动散光近似" 的 sphere 路径实现）。
//
// 无光照计算（emissive surface 本质）+ 不读 set 0 binding 0/1 但仍声明
// 占位（与 mainDescLayout 兼容，让 GetOrCompilePipeline 复用主 forward
// 的 descriptor set layout，halo loop 紧接 mesh forward 后复用其
// mainDescSet 无需重 bind；与 emissive.frag.glsl 同款占位模式）。

// set 0 占位 binding —— frag 不读，driver dead-code-elim；只为与
// emissive / textured / toon / pbr 等同 mainDescLayout 兼容。CSM 升
// sampler2DArray 类型必须与 emissive.frag.glsl 一致。
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

layout(location = 0) in vec3 vHaloColor;
layout(location = 1) in vec3 vWorldPos;  // 占位，本 frag 不读
layout(location = 2) in vec3 vNormal;    // 占位，本 frag 不读

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vHaloColor, 1.0);
}
