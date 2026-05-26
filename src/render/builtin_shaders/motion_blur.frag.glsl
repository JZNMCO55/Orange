#version 450

// 内置相机运动模糊（camera motion blur）gather pass —— 从 sceneDepth 重建 world
// pos，用上一帧（未 jitter）viewProj 投影算屏幕空间速度矢量，沿速度方向在
// hdrColor 上多 tap 累加 → 独立 motionBlurColor（避免读写同 target 反馈，与 DoF /
// SSR 同款分离）。motion_blur_composite（复用 dof_composite）再 replace 回 HDR。
//
// 速度来源：仅相机重投影（cur uv + depth → world → 上一帧 clip → 上一帧 uv），
// velocity = (cur - prev) uv。与 TAA 重投影同一数学，但 motion blur 主动沿
// velocity 拉糊（TAA 反之去抖）。无 per-object 运动矢量 → 只表现相机运动模糊
// （平移 / 旋转 / 推拉），运动物体本身不单独拖影——stylized / 相机运镜够用。
//
// 已知简化：① 仅相机速度；② 均匀直线 tap（无 reconstruction filter / tile-max
// 速度扩散，UE / Naughty Dog 风格的 dilate 留作后续）；③ 速度按 maxRadius clamp
// 防超长拖影采到无关像素。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1, std140) uniform MotionBlurUbo
{
    mat4 uInvCurViewProj;   // 当前（未 jitter）viewProj 的逆：depth → world
    mat4 uPrevViewProj;     // 上一帧（未 jitter）viewProj：world → prev clip
    vec4 uParams;           // x=intensity, y=maxRadius(uv), z=sampleCount, w=hasHistory(0/1)
} mb;
layout(set = 0, binding = 2) uniform sampler2D uSceneDepth;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 base = texture(uHdrColor, vUV).rgb;

    // 首帧 / resize 后无有效历史 → 直接返回（无 prev 可重投影）。
    if (mb.uParams.w < 0.5)
    {
        outColor = vec4(base, 1.0);
        return;
    }

    // 重投影：cur uv + depth → world → 上一帧 clip → 上一帧 uv。
    float depth    = texture(uSceneDepth, vUV).r;
    vec4  clip     = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4  worldH   = mb.uInvCurViewProj * clip;
    vec3  world    = worldH.xyz / worldH.w;
    vec4  prevClip = mb.uPrevViewProj * vec4(world, 1.0);
    vec2  prevUV   = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // 屏幕空间速度矢量（像素这一帧走过的轨迹），乘强度。
    vec2  velocity = (vUV - prevUV) * mb.uParams.x;

    // clamp 到 maxRadius，防超长拖影采到无关像素。
    float maxR = mb.uParams.y;
    float len  = length(velocity);
    if (len > maxR)
    {
        velocity *= maxR / len;
        len = maxR;
    }

    // 速度极小（近似静止）→ 省 gather 直接返回锐利。
    if (len < 0.0008)
    {
        outColor = vec4(base, 1.0);
        return;
    }

    // 沿速度方向居中均匀 tap（t ∈ [-0.5, 0.5]），累加平均。
    int  samples = max(int(mb.uParams.z), 2);
    vec3 sum     = vec3(0.0);
    for (int i = 0; i < samples; ++i)
    {
        float t = float(i) / float(samples - 1) - 0.5;
        sum += texture(uHdrColor, vUV + velocity * t).rgb;
    }
    outColor = vec4(sum / float(samples), 1.0);
}
