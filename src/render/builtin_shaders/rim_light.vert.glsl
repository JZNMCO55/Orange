#version 450

// 内置 rim-light 顶点 shader：透传 uv 与 model-space position（fragment
// 阶段用 dFdx / dFdy 推 face normal 用）。push-constant 与 fragment 阶
// 段共用同一 block——std430 字段顺序与 Material 描述符里 uniforms 列
// 表一一对应。
//
// 顶点输入布局必须与 src/render/Pipeline.cpp 中 VertexInputLayoutDesc
// 的两个 attribute 一一对应（location 0: float3 pos, location 1:
// float2 uv，stride 20 bytes）。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vModelPos;

layout(push_constant, std430) uniform RimLight {
    mat4  uMVP;            //   0  64
    vec3  uViewPos;        //  64  16
    vec3  uRimColor;       //  80  16
    float uRimPower;       //  96   4
    float uRimIntensity;   // 100   4
} pc;

void main()
{
    gl_Position = pc.uMVP * vec4(inPosition, 1.0);
    vUV         = inUV;
    vModelPos   = inPosition;
}
