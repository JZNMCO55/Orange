#version 450

// 内置 dissolve 顶点 shader —— 与 textured / toon / rim_light 同 layout：
// push-constant {uMVP, uModel}，输出 vUV + vWorldPos 两个常用通道，让
// fragment 端跑 noise(uv) 与按 worldPos 衰减的扩展接得上。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;

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
    gl_Position    = pc.uMVP * vec4(inPosition, 1.0);
}
