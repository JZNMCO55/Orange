#version 450

// 内置 toon-shading 顶点 shader：透传 uv，把 model-space position 也透
// 给 fragment 用作 face normal 推算（dFdx / dFdy）。push-constant 与
// fragment 阶段共用同一 block——std430 layout 与 Material 描述符里的
// uniform 顺序一一对应（Phase 3 / Task 04 起 Pipeline 把 MaterialInstance
// 覆盖按这个顺序打包写入）。
//
// 顶点输入布局必须与 src/render/Pipeline.cpp 中 VertexInputLayoutDesc
// 的两个 attribute 一一对应（location 0: float3 pos, location 1:
// float2 uv，stride 20 bytes）。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vModelPos;

layout(push_constant, std430) uniform Toon {
    mat4  uMVP;             //   0  64
    vec3  uColorWarm;        //  64  16  (vec3 + 4B 尾 pad)
    vec3  uColorCool;        //  80  16
    vec3  uLightDir;         //  96  16
    float uShadowThreshold;  // 112   4
} pc;

void main()
{
    gl_Position = pc.uMVP * vec4(inPosition, 1.0);
    vUV         = inUV;
    vModelPos   = inPosition;
}
