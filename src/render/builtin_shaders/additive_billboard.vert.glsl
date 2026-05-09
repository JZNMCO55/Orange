#version 450

// ---------------------------------------------------------------------------
// additive_billboard.vert —— 引擎内置粒子 vertex shader。
//
// 单 draw call 提交 6 顶点 × N instance。vertex buffer 以 instance rate
// 绑定，每 instance 读一条 ParticleInstance 记录；4 个 quad 角点由
// gl_VertexIndex 合成（CCW 三角形 0/1/2 + 0/2/3），无 per-vertex stream。
//
// Push-constant 与 C++ 端 VfxSystem::DrawParticles 透传的 viewProj 一致：
//     bytes  0..63 : mat4 viewProj
//     bytes 64..79 : vec4 timeAspect (x=time, y=delta, z=aspect, w=unused)
// 当前 sample 不实际用 timeAspect，但 layout 与 OrangeRender particle_field
// sample 对齐，方便将来"时间驱动闪烁 / 抖动"低成本接入。
// ---------------------------------------------------------------------------

layout(location = 0) in vec4 inPosSize;  // xy = world pos, z = size, w = age01
layout(location = 1) in vec4 inColor;    // rgb = color, a = intensity

layout(push_constant) uniform Push {
    mat4 mViewProj;
    vec4 mTimeAspect;
} pc;

layout(location = 0) out vec2  vQuadUV;
layout(location = 1) out vec4  vColor;
layout(location = 2) out float vAge01;

void main()
{
    const vec2 corners[4] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0));
    const int idx[6] = int[](0, 1, 2, 0, 2, 3);
    vec2 corner = corners[idx[gl_VertexIndex]];

    vec2 worldPos = inPosSize.xy + corner * inPosSize.z;
    gl_Position   = pc.mViewProj * vec4(worldPos, 0.0, 1.0);

    vQuadUV = corner;
    vColor  = inColor;
    vAge01  = inPosSize.w;
}
