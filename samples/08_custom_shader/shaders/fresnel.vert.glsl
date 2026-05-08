#version 450

// 自定义 fresnel shader 顶点段（sample 08）。
//
// 顶点输入 layout 与内置 toon / rim_light / textured 完全一致：
//   location 0 = vec3 inPosition
//   location 1 = vec2 inUV
//   stride 20 字节，由 Pipeline 的 InterleavedVertex 决定
//
// push_constant block 也与 toon / rim_light 同形态（uMVP + uModel = 128 B），
// 让 Pipeline 主 pass 的 "pcSize >= 128 → push 128 B" 分支能直接命中——
// 自定义 shader 无需改引擎一行就拿到 mvp + model。
//
// fresnel 颜色 / 幂指数 / 时间脉动速率本期 hardcode 在 fragment 里（per-instance
// 调参等 Phase 6 Material UBO）；时间则从 frame UBO 的 uFrameInfo.x 拿。

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
