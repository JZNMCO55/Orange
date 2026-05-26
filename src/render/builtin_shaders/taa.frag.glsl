#version 450

// 内置 TAA（temporal anti-aliasing）resolve pass。
//
// 把"当前帧（投影做了 per-frame sub-pixel jitter 的几何）"与"上一帧 resolved
// 历史"按 feedback 权重混合。jitter 让每帧在不同亚像素位置采样，多帧累积 =
// 超采样 → 边缘抗锯齿 + 把 GTAO / 接触阴影 / 景深的 jitter 噪点抹平。
//
// 重投影：当前像素 uv + depth 反算 world，再用上一帧 viewProj 投到上一帧 uv，
// 采历史——支持相机运动（静态相机下退化为 uv→uv 恒等）。邻域 clamp：把历史色
// clamp 到当前 3×3 颜色 AABB，抑制 disocclusion / 运动 ghosting（无 per-object
// 运动矢量时的标准兜底）。
//
// 已知简化：仅相机重投影（无 per-object 运动矢量，运动物体靠 clamp 兜底，快速
// 运动仍可能轻微 ghost）；RGB AABB clamp（非 YCoCg）。

layout(set = 0, binding = 0) uniform sampler2D uCurrent;     // 当前帧 HDR（jittered）
layout(set = 0, binding = 1) uniform sampler2D uHistory;     // 上一帧 resolved
layout(set = 0, binding = 2) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 3, std140) uniform TaaUbo
{
    mat4 uInvCurViewProj;   // 当前 jittered viewProj 的逆（depth → world）
    mat4 uPrevViewProj;     // 上一帧 jittered viewProj（world → prev clip）
    vec4 uParams;           // x=feedback(历史权重), y=hasHistory(0/1), z/w 预留
} taa;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 cur = texture(uCurrent, vUV).rgb;

    // 首帧 / resize 后无有效历史 → 直接用当前。
    if (taa.uParams.y < 0.5)
    {
        outColor = vec4(cur, 1.0);
        return;
    }

    // 重投影：当前 uv + depth → world → 上一帧 clip → 上一帧 uv。
    float depth   = texture(uSceneDepth, vUV).r;
    vec4  clip    = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4  worldH  = taa.uInvCurViewProj * clip;
    vec3  world   = worldH.xyz / worldH.w;
    vec4  prevClip= taa.uPrevViewProj * vec4(world, 1.0);
    vec2  prevUV  = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // 重投影出屏 → 无有效历史，用当前（避免拉边缘 smear）。
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0)
    {
        outColor = vec4(cur, 1.0);
        return;
    }

    vec3 hist = texture(uHistory, prevUV).rgb;

    // 邻域 clamp：把历史 clamp 到当前 3×3 颜色 AABB。
    vec2 texel = 1.0 / vec2(textureSize(uCurrent, 0));
    vec3 mn = cur;
    vec3 mx = cur;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            vec3 c = texture(uCurrent, vUV + vec2(float(x), float(y)) * texel).rgb;
            mn = min(mn, c);
            mx = max(mx, c);
        }
    }
    hist = clamp(hist, mn, mx);

    vec3 res = mix(cur, hist, taa.uParams.x);
    outColor = vec4(res, 1.0);
}
