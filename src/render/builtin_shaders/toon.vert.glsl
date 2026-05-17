#version 450

// 内置 toon-shading 顶点 shader（Task 07 重构版）：
//   * push-constant 变为 {uMVP, uModel} = 128 B（Vertex 阶段使用），
//     uModel 让 fragment 端能拿到 worldPos 跑 shadow_pcf；
//   * 输出 vUV / vWorldPos / vNormal —— 旧版 frag 端用 dFdx/dFdy 推
//     face normal，球体会出多面体；GAP-2026-05-17 起改走 per-vertex
//     normal，frag 直接读 vNormal 即可。
//
// 其他原 push-constant 字段（uColorWarm / uColorCool / uLightDir /
// uShadowThreshold）按 Phase 3 / Task 07 设计决策迁移：
//   * uLightDir / uLightColor / uLightIntensity → 主 pass 的 light UBO
//     (descriptor set 0 binding 1)，per-frame；
//   * uColorWarm / uColorCool / uShadowThreshold → 暂时 hardcode 进
//     fragment shader（per-instance 自定义留待 Phase 6 引入 Material UBO）。
//
// 顶点输入 layout 与 textured_mesh / shadow_caster 一致：location 0 =
// pos, location 1 = uv, location 2 = normal，stride 32 字节（Pipeline
// 的 InterleavedVertex）。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vNormal;

layout(push_constant, std430) uniform Push
{
    mat4 uMVP;     //   0  64
    mat4 uModel;   //  64  64
} pc;

void main()
{
    vec4 worldPos4 = pc.uModel * vec4(inPosition, 1.0);
    vWorldPos      = worldPos4.xyz;
    vUV            = inUV;
    vNormal        = mat3(pc.uModel) * inNormal;
    gl_Position    = pc.uMVP * vec4(inPosition, 1.0);
}
