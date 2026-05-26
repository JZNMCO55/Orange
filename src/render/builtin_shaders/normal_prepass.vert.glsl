#version 450

// 内置法线预通道顶点 shader：把 model-space 顶点变换到 clip space（与主 pass
// 同款 y-flip 投影，保证逐像素与 sceneDepth / hdrColor 对齐），并把法线翻到
// view space 透传给 frag。SSAO / SSR 据此采真实几何法线，替代深度差分重建。
//
// push-constant（≤128 B，与 shadow caster 同尺寸）：
//   * uMVP       —— viewProj * model，写 gl_Position；
//   * uModelView —— view * model，mat3 部分把法线翻到 view space（假定无非
//                   均匀缩放，与 pbr.vert 的 mat3(uModel) 近似同源；非均匀缩放
//                   升级到 inverse-transpose 延后到实际需求触发）。
//
// 顶点输入 layout 与 Pipeline::FillVertexInputLayout 一致（pos / uv / normal /
// tangent）；本 shader 只消费 pos + normal，uv 占位对齐（与 shadow_caster 同款）。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;     // 不使用，仅占位对齐 textured_mesh
layout(location = 2) in vec3 inNormal;

layout(push_constant, std430) uniform Push {
    mat4 uMVP;        //   0  64
    mat4 uModelView;  //  64  64
} pc;

layout(location = 0) out vec3 vViewNormal;

void main()
{
    vViewNormal = mat3(pc.uModelView) * inNormal;
    gl_Position = pc.uMVP * vec4(inPosition, 1.0);
}
