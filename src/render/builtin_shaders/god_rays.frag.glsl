#version 450

// 内置 god rays 片元 shader —— Mitchell 2007 屏幕空间径向模糊。
//
// 沿"sun 屏幕坐标 → 当前像素"的反方向 numSamples 跳采样：每跳同时检
// 查两个条件 ——
//   * sceneDepth ≈ 1.0（far plane）→ 没有几何遮挡这条光线；
//   * sample 位置距 sun uv 距离 < 一个软盘 radius（exp 衰减）→ 当前
//     sample 真的位于"屏幕上的太阳盘"内。
// 两条同时满足才把 sun_color 累积进 ray。这条 sun-disk falloff 是对原
// Mitchell 算法里"sun 渲成屏内亮斑（仅未遮挡像素）"那一步的等价替代：
// 不需要单独建 sun-only target，单 fragment 直接判定。粒子 / emissive
// cube 不写 depth 落在 far plane 后被穿透；写 depth 的几何（dissolve
// cube / 主 pass mesh）会让 sun disk 在其轮廓处熄灭——godrays 在该几
// 何后方"被掐断"，呈现典型光柱穿透感。
//
// 输出 vec4(rgb, 1)；pipeline 端用 srcAlpha=ONE/dstAlpha=ONE 的加性
// blending 把 god rays 累积到既有 HDR target（已含 main + 粒子 + bloom），
// 再交给 tonemap 一并 ACES 压回 LDR。
//
// 顶点阶段复用 fullscreen.vert（big-triangle gl_VertexIndex 驱动），与
// passthrough / bloom downsample / bloom upsample 共用同一份 vert。

layout(set = 0, binding = 0) uniform sampler2D uSceneDepth;

layout(push_constant, std430) uniform Push
{
    vec2  uSunUV;        // 屏幕空间 sun 位置（[0,1]² uv 域）
    float uDensity;
    float uDecay;

    vec3  uSunColor;
    float uWeight;

    float uExposure;
    float uPad0;
    float uPad1;
    float uPad2;

    int   uNumSamples;
    int   uPadI0;
    int   uPadI1;
    int   uPadI2;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    // 屏外光源处理：即使 uSunUV 在 [0,1] 之外 sample loop 仍能跑（采样
    // 进 clamp/edge 区域），但 visibility 几乎处处 0 → 自然消失，与
    // wiki "屏幕空间 god rays 角度太掠 → 看不到效果" 的限制一致。

    vec2  ray     = vUV - pc.uSunUV;
    int   N       = max(pc.uNumSamples, 1);
    vec2  rayStep = ray / float(N);   // 不能命名为 `step` —— 与 GLSL 内建函数同名

    vec2  pos   = vUV;
    float illum = 1.0;
    vec3  accum = vec3(0.0);

    // sun disk 的"软盘 radius" —— sample 距 sun uv 越远贡献越弱。kSharpness
    // 控制盘的紧致度：越大盘越小越锐利。这条 hardcode 留给后续可调，
    // 当前值在 1280×720 / 60° fov 下视觉合宜。
    const float kSharpness = 60.0;

    for (int i = 0; i < N; ++i)
    {
        pos -= rayStep;

        // texture() 自带 sampler 的 clamp 行为；越界 sample 不会报错。
        float d = texture(uSceneDepth, pos).r;

        // depth ≈ 1.0 → 当像素后面没有几何遮挡 → 当前 sample 不被场景
        // 挡住。用 step(0.999, d) 而不是 == 1.0：避免浮点精度边沿抖动。
        float depthVisible = step(0.999, d);

        // Sun disk falloff：sample 距 sun 位置越远，sun disk 越暗。这条
        // 把"god rays = 整屏 background"约束成"god rays = sun 周围沿径
        // 向 fan-out 的光柱"——没有这条整个屏幕都会被拉成发光面。
        vec2  d2v   = pos - pc.uSunUV;
        float distSq = dot(d2v, d2v);
        float sunDisk = exp(-distSq * kSharpness);

        accum += pc.uSunColor * depthVisible * sunDisk * illum * pc.uWeight;
        illum *= pc.uDecay;
    }

    outColor = vec4(accum * pc.uDensity * pc.uExposure, 1.0);
}
