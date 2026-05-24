#version 450

// 内置 unlit 模板：直接输出 push-constant uBaseColor.rgb，无光照、无
// shadow、无 IBL。vertex shader 复用 pbr.vert（标准 uMVP + uModel +
// uBaseColor + uMRA push-constant；uMRA 在 unlit 不消费但 push-constant
// 布局必须与 vert 对齐，所以 .template.json 保留 uMRA 字段并 hidden）。
//
// 用途：UI 装饰物、纯色 debug 可视化、不参与光照的几何（如远景背板）。
//
// 与 emissive 的区别：
//   * emissive 输出 HDR > 1 让 bloom pass 自动拾取产出光晕；unlit 输出
//     LDR 由用户自由调色（包含 < 1 的常规颜色）
//   * emissive 颜色 / 强度 hardcode 进 frag；unlit 完全 per-instance 调
//     参（uBaseColor 走 push-constant）

// set 0 layout 与主 pass 其它内置模板对齐——shader 即使不读 binding 0/1，
// 也得在 layout 里声明，否则 SetDescriptorSet 会失败。
layout(set = 0, binding = 0) uniform sampler2D uShadowMap;
layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;
    vec4 uLightDirIntensity;
    vec4 uLightColor;
    vec4 uShadowParams;
    vec4 uCameraWorldPos;
    vec4 uFrameInfo;
} light;

// 与 pbr.vert 输出 location 一一对齐（unlit 仅消费 vBaseColor，其余声明
// 但不读——GLSL 编译器会按未使用消除生成）。
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec4 vBaseColor;
layout(location = 4) in vec4 vMRA;

layout(location = 0) out vec4 outColor;

void main()
{
    // 直接输出 baseColor.rgb，alpha 透传（forward opaque pass blend = 1，
    // alpha 不参与混色但下游 post-process / tonemap 仍接受 vec4）。
    outColor = vec4(vBaseColor.rgb, 1.0);
}
