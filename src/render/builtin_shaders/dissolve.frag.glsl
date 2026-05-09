#version 450

// 内置 dissolve 片元 shader —— 按 noise(uv) + 时间驱动阈值做 alpha
// discard，阈值附近 ±kEdgeWidth 区间输出 HDR > 1 的 emissive 边沿色，
// 让 bloom pass 自动产出"消融边沿发光"的视觉。
//
// 时间源：light UBO 的 uFrameInfo.x（elapsed seconds）—— Pipeline 主
// pass 的 set 0 binding 1 已被 toon / rim_light 等共用，本 shader 同
// 样消费。dissolve_t 走 0..1..0 pingpong (`(1 - cos(time)) * 0.5`)，
// 让 demo 不带任何调用方代码就能看到完整 fade-in / fade-out 循环。
//
// 参数 hardcode（per-instance uniform 路径需要 Pipeline 把
// MaterialInstance.uniformOverrides 真打进 push constant，那条路上线
// 后这里再迁回 per-instance 描述符）：noise scale / edge width / edge
// color 都写在常量里。

layout(set = 0, binding = 0) uniform sampler2D uShadowMap;  // 不消费，但 set 0 layout 要求声明
layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;
    vec4 uLightDirIntensity;
    vec4 uLightColor;
    vec4 uShadowParams;
    vec4 uCameraWorldPos;
    vec4 uFrameInfo;          // x = time seconds
} light;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

// hash → [0,1)；用作值噪声（value noise）的格点采样源。
float Hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// 值噪声：取整数格点 hash 再 bilinear 平滑。比纯 Hash(uv) 视觉连续，
// 让"消融边缘"形成可识别的色块前沿，而不是单像素闪烁噪点（后者还会
// 被 bloom downsample 的 Karis 平均当 firefly 抑制掉）。
float ValueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    // smoothstep 平滑 cell 内插值，避免 mip-link 上的色带断口。
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = Hash(i + vec2(0.0, 0.0));
    float b = Hash(i + vec2(1.0, 0.0));
    float c = Hash(i + vec2(0.0, 1.0));
    float d = Hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

void main()
{
    // 0..1..0 pingpong：cos(t)∈[-1,1]，(1-cos)/2∈[0,1]。周期 2π ≈ 6.28s。
    float t         = light.uFrameInfo.x;
    float dissolveT = (1.0 - cos(t)) * 0.5;

    // 频率压低 → 每面 ~3×3 个色块；边沿带宽度做大让 bloom 拾取后形成
    // 可见 halo（Karis 平均偏好"成片亮区"而不是单像素 firefly）。
    const float kNoiseScale = 2.8;
    const float kEdgeWidth  = 0.18;
    const vec3  kEdgeColor  = vec3(2.4, 0.85, 0.18);  // 显著 HDR > 1
    const vec3  kBaseColor  = vec3(0.55, 0.22, 0.08);

    float n = ValueNoise(vUV * kNoiseScale);

    // noise < dissolveT 的部分被 discard——视觉上形成 "面被啃掉" 的效果。
    if (n < dissolveT)
    {
        discard;
    }

    // 边沿带：噪声值刚刚高过阈值 → 输出发光色；远离阈值 → base。
    float edge = 1.0 - smoothstep(0.0, kEdgeWidth, n - dissolveT);
    vec3  col  = mix(kBaseColor, kEdgeColor, edge);

    outColor = vec4(col, 1.0);
}
