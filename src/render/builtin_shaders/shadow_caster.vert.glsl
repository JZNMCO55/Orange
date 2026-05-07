#version 450

// 内置 shadow caster 顶点 shader：把 model-space position 变换到 light
// clip space，写到 depth attachment。fragment shader 是空 main，仅写
// 深度（Vulkan 默认 GL_LESS 接管）。
//
// push-constant 结构（≤128 B）：
//   * uLightViewProj —— per-frame，从 DirectionalLight.direction +
//     scene bbox 算出来；
//   * uModel         —— per-draw，drawable 的 world transform。
//
// 顶点输入 layout 必须与 src/render/Pipeline.cpp 中 VertexInputLayoutDesc
// 的 attribute 0（float3 pos）兼容。location 1（uv）shadow caster 不
// 关心，但顶点 buffer 共享，所以这里仍声明 location 0/1，frag 只读 depth。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;     // 不使用，仅占位与 textured_mesh 对齐

layout(push_constant, std430) uniform ShadowCaster {
    mat4 uLightViewProj;   //   0  64
    mat4 uModel;           //  64  64
} pc;

void main()
{
    gl_Position = pc.uLightViewProj * pc.uModel * vec4(inPosition, 1.0);
}
