#version 450

// 内置 PBR 顶点 shader：与 textured_mesh / toon / rim_light 同输入布局——
// pos + uv + normal，stride 32 字节，由 Pipeline 的 FillVertexInputLayout
// 喂入。push_constant 在 toon / rim_light 的 {uMVP, uModel}（128 B）基础上
// 扩两条 vec4：uBaseColor + uMRA，共 160 B；OrangeRender 当前
// PushConstantRange 只允许声明 Vertex stage，所以 fragment 需要的 PBR 参
// 数（baseColor / metallic / roughness / ao）由本 vert 读出后透传 varying
// 喂给 frag。等到 RHI 支持 multi-stage push constant 或者上线 per-instance
// material UBO 时，此 transport 路径可改成直接绑 set 1。
//
// 输出：
//   * vUV       —— 五通道 texture 采样（base color / MR / normal / AO）的
//                  uv 入口；本期 frag 端无 texture 绑定路径暂不消费。
//   * vWorldPos —— frag 端做 shadow PCF 采样与 view 方向计算用。
//   * vNormal   —— 经 mat3(uModel) 翻到 world space（假定无非均匀缩放，
//                  与 textured_mesh / toon / rim_light 同款近似；mat3
//                  inverse-transpose 升级延后到非均匀缩放进入需求时）。
//   * vBaseColor —— per-instance albedo，rgb 用，a 预留（暂传 1）。
//   * vMRA       —— (metallic, roughness, ao, _reserved)。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vNormal;
layout(location = 3) out vec4 vBaseColor;
layout(location = 4) out vec4 vMRA;

layout(push_constant, std430) uniform Push {
    mat4 uMVP;        //   0  64
    mat4 uModel;      //  64  64
    vec4 uBaseColor;  // 128  16
    vec4 uMRA;        // 144  16   (metallic, roughness, ao, _reserved)
} pc;

void main()
{
    vec4 worldPos4 = pc.uModel * vec4(inPosition, 1.0);
    vWorldPos      = worldPos4.xyz;
    vUV            = inUV;
    vNormal        = mat3(pc.uModel) * inNormal;
    vBaseColor     = pc.uBaseColor;
    vMRA           = pc.uMRA;
    gl_Position    = pc.uMVP * vec4(inPosition, 1.0);
}
