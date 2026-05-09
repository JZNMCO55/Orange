#version 450

// ---------------------------------------------------------------------------
// additive_billboard.frag —— 引擎内置粒子 fragment shader。
//
// 在 quad 局部空间（vQuadUV ∈ [-1, 1]）做径向软 alpha + 寿命淡入淡出，
// 配合 pipeline 上 additive blending（srcAlpha = ONE, dstAlpha = ONE）
// 把发光值线性叠加到 HDR target——颜色 a > 1 时溢出 LDR 阈值会被既有
// bloom pass 自动拾取为发光晕。
// ---------------------------------------------------------------------------

layout(location = 0) in  vec2  vQuadUV;
layout(location = 1) in  vec4  vColor;
layout(location = 2) in  float vAge01;

layout(location = 0) out vec4 outColor;

void main()
{
    // 径向软 alpha：四次方衰减给出"亮核 + 光晕"的发光感。
    float r = length(vQuadUV);
    if (r > 1.0)
    {
        discard;
    }
    float alpha = 1.0 - r;
    alpha *= alpha;

    // 寿命曲线：开局 0..0.05 ramp-in 避免 spawn 跳变；末尾 0.7..1.0
    // 平滑淡出。其余阶段维持满强度——保证粒子在主寿命段视觉稳定。
    float fade = 1.0 - smoothstep(0.7, 1.0, vAge01);
    fade *= smoothstep(0.0, 0.05, vAge01);

    vec3 col = vColor.rgb * vColor.a * alpha * fade;

    // Premultiplied 形态——pipeline 用 srcAlpha=ONE/dstAlpha=ONE 加和，
    // 这里直接输出加进 framebuffer 的线性增量。
    outColor = vec4(col, alpha * fade);
}
