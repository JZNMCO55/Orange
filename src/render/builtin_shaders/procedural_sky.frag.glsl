#version 450

// ---------------------------------------------------------------------------
// 程序化天空 fragment shader —— fullscreen.vert（big-triangle）配套。无
// cubemap 依赖：每像素反推 world-space view dir，按 dir.y 做 3 色 horizon
// gradient（zenith 天蓝 / horizon 浅白 / ground 深灰），再叠加一个由
// DirectionalLight 方向驱动的太阳圆盘 + 光晕。bakedEnvCube 没烘焙时
// Pipeline 走这条分支替代 cubemap sky pass（Cocos / Godot / Unity HDRP
// 默认天空效果同款思路）。
//
// 与 cubemap sky pass 共用同一插点（主 pass 之前，LoadOp::Clear；主 pass
// 接 LoadOp::Load）；与 sky.frag.glsl 同款 depth attachment 缺省（不写
// 深度，主 pass 自己 Clear depth = 1.0）。
//
// push constant 112 字节（必须与 Pipeline 端 ProceduralSkyPush 字节布局
// 严格一致）：
//   * uInvViewProj : mat4，64B
//   * uCameraPos   : vec3，12B
//   * uPad0        : float，4B
//   * uSunDir      : vec3，12B（指向太阳的方向 = -light direction）
//   * uSunSize     : float，4B（disc cos 阈值，越接近 1 太阳越小；典型 0.9995）
//   * uSunColor    : vec3，12B
//   * uSunIntensity: float，4B（disc 强度乘子）
// 天空 palette（zenith / horizon / ground）在 shader 内 hardcode —— Vulkan
// 最低保证 push 128B，留出余量给未来扩展。需要"夜晚 sky"切换时再升级为
// push 字段或独立 UBO。
// ---------------------------------------------------------------------------

layout(push_constant, std430) uniform Push
{
    mat4  uInvViewProj;
    vec3  uCameraPos;
    float uPad0;
    vec3  uSunDir;
    float uSunSize;
    vec3  uSunColor;
    float uSunIntensity;
} pc;

// 白天 sky palette：zenith 偏天蓝（冷色），horizon 偏浅白（接近大气散射
// 地平线效果），ground 中性深灰。颜色取自 Unity HDRP / Godot 默认 sky
// 平均，tonemap 后接近"晴朗午后"。
const vec3 kZenithColor  = vec3(0.30, 0.55, 0.90);
const vec3 kHorizonColor = vec3(0.75, 0.85, 0.92);
const vec3 kGroundColor  = vec3(0.18, 0.18, 0.20);

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    // 1. 反推 world-space view direction
    vec2 ndc = vUV * 2.0 - 1.0;
    vec4 farH = pc.uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 farPos = farH.xyz / max(abs(farH.w), 1e-6) * sign(farH.w + 1e-6);
    vec3 dir = normalize(farPos - pc.uCameraPos);

    // 2. Horizon gradient —— dir.y 决定上下混合
    //    dir.y >  0.0 → 天空段：horizon → zenith 平滑插值
    //    dir.y <= 0.0 → 地面段：horizon → ground 平滑插值
    //    幂次让接缝柔和并把渐变集中在地平线附近
    vec3 sky;
    if (dir.y >= 0.0)
    {
        float t = pow(clamp(dir.y, 0.0, 1.0), 0.5);
        sky = mix(kHorizonColor, kZenithColor, t);
    }
    else
    {
        float t = pow(clamp(-dir.y, 0.0, 1.0), 0.5);
        sky = mix(kHorizonColor, kGroundColor, t);
    }

    // 3. 太阳：disc + 光晕。dot(viewDir, sunDir) 越接近 1 = 越靠近太阳中心
    float cosAng = dot(dir, normalize(pc.uSunDir));

    //   * disc：smoothstep 阈值附近一个锐利圆盘（uSunSize 典型 0.9995 ≈
    //     视角 ~1.8°，与真实太阳视角 0.53° 比偏大，编辑器观感优先）
    float disc = smoothstep(pc.uSunSize, pc.uSunSize + 0.0002, cosAng);

    //   * 光晕：cosAng^N，N 越大光晕越紧。N=256 让光晕半径 ~5°（Mie 散射
    //     的视觉近似）。再叠加一层 N=8 软光晕给天空整体着色
    float glowTight = pow(max(cosAng, 0.0), 256.0);
    float glowWide  = pow(max(cosAng, 0.0),   8.0);

    vec3 sun = pc.uSunColor * (
        disc * pc.uSunIntensity                  // 圆盘
      + glowTight * pc.uSunIntensity * 0.30     // 紧光晕
      + glowWide  * pc.uSunIntensity * 0.06     // 软散射
    );

    // 太阳在地平线下不画（避免地下也有太阳光斑这种穿帮）
    sun *= step(0.0, pc.uSunDir.y);

    outColor = vec4(sky + sun, 1.0);
}
