#version 450

// 内置 dissolve 顶点 shader —— 与 textured / toon / rim_light 同 layout：
// push-constant {uMVP, uModel}，输出 vUV + vWorldPos + vNormal。dissolve
// frag 仅消费 vUV / vWorldPos，vNormal 由 vertex layout 一致性需要而
// 输出（与 Pipeline InterleavedVertex 32B stride 对齐），frag 不读就由
// driver dead-code-elim 掉。

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
