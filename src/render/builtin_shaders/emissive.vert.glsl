#version 450

// 内置 emissive 顶点 shader —— 与 textured / toon 同 layout，复用
// pos+uv vertex 输入与 {uMVP, uModel} push constant。仅 vUV 这一个
// fragment 通道会被消费（emissive frag 不需要 worldPos / normal）。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;  // 占位，让 set 0 layout 一致；frag 不读

layout(push_constant, std430) uniform Push
{
    mat4 uMVP;
    mat4 uModel;
} pc;

void main()
{
    vec4 worldPos4 = pc.uModel * vec4(inPosition, 1.0);
    vWorldPos      = worldPos4.xyz;
    vUV            = inUV;
    gl_Position    = pc.uMVP * vec4(inPosition, 1.0);
}
