#version 450

// 内置 rim-light 顶点 shader（Task 07 重构版）—— 与 toon.vert 同形态：
// push-constant {uMVP, uModel} = 128 B；输出 vUV / vWorldPos / vModelPos /
// vNormal（rim 计算需要 view direction = camera - worldPos，但当前阶
// 段 view position 不在 push constant 里，frag 端用 vModelPos 当 view-
// pos 的退化解；rim 强度依赖法线 dot view，GAP-2026-05-17 起读 vNormal
// 而不是 dFdx/dFdy）。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vModelPos;
layout(location = 3) out vec3 vNormal;

layout(push_constant, std430) uniform Push
{
    mat4 uMVP;     //   0  64
    mat4 uModel;   //  64  64
} pc;

void main()
{
    vec4 worldPos4 = pc.uModel * vec4(inPosition, 1.0);
    vWorldPos      = worldPos4.xyz;
    vModelPos      = inPosition;
    vUV            = inUV;
    vNormal        = mat3(pc.uModel) * inNormal;
    gl_Position    = pc.uMVP * vec4(inPosition, 1.0);
}
