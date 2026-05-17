#version 450

// 内置 textured-mesh 顶点 shader（Task 07 重构版）：与 toon / rim_light
// 同模式，push-constant 收为 {uMVP, uModel} 128 B，输出 vWorldPos 让
// fragment 端能跑 shadow_pcf 采样。
//
// 顶点输入布局必须与 src/render/Pipeline.cpp 中 VertexInputLayoutDesc
// 的三个 attribute 一一对应（location 0: float3 pos, location 1:
// float2 uv, location 2: float3 normal，stride 32 字节）。
//
// vNormal 走 mat3(uModel) 变换到 world space —— PBR 前的视觉基线统一
// 用近似变换（假设 model 不带非均匀缩放）；非均匀缩放兼容由后续
// inverse-transpose mat3 升级接入，本 shader 不预先承担。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vNormal;

layout(push_constant, std430) uniform Push {
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
