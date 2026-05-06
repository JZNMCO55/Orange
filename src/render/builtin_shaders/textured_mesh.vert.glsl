#version 450

// 内置 textured-mesh 顶点 shader：从 vertex buffer 读 pos + uv，按
// push-constant MVP 投影到 clip space，把 uv 透传给 fragment 用于
// "程序式 checker"采样。
//
// 顶点输入布局必须与 src/render/Pipeline.cpp 中 VertexInputLayoutDesc
// 的两个 attribute 一一对应（location 0: float3 pos, location 1:
// float2 uv，stride 20 bytes）。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;

layout(push_constant) uniform PushConstants {
    mat4 uMVP;
} pc;

void main()
{
    gl_Position = pc.uMVP * vec4(inPosition, 1.0);
    vUV         = inUV;
}
