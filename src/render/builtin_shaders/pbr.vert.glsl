#version 450

// 内置 PBR 顶点 shader：与 textured_mesh / toon / rim_light 同输入布局——
// pos + uv + normal，stride 32 字节，由 Pipeline 的 FillVertexInputLayout
// 喂入。push_constant 仅 {uMVP, uModel} 共 128 B，与 BuiltinMaterials::
// LoadPbr 装配的 Material.uniforms 序列一致。
//
// 输出：
//   * vUV       —— 留给后续五通道 texture 采样（base color / MR / normal /
//                  AO）；当前 frag 端 hardcoded 标量参数暂不消费。
//   * vWorldPos —— frag 端做 shadow PCF 采样与 view 方向计算用。
//   * vNormal   —— 经 mat3(uModel) 翻到 world space（假定无非均匀缩放，
//                  与 textured_mesh / toon / rim_light 同款近似；mat3
//                  inverse-transpose 升级延后到非均匀缩放进入需求时）。

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
