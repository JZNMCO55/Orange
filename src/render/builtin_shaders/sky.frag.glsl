#version 450

// ---------------------------------------------------------------------------
// 天空盒 fragment shader —— fullscreen.vert（big-triangle）配套。每像素从
// vUV 反推 NDC，再用 invViewProj 反推到 world-space 远平面点，得到
// world-space 视线方向；samplerCube 采样烘焙好的 environment cubemap，乘
// 上 EnvironmentComponent 的 tint * intensity 写到 hdrColor。
//
// Pipeline 把本 pass 安排在主几何 pass **之前**：
//   * color: LoadOp::Clear（sky 自己覆盖整屏，clear 颜色仅作 fallback）
//   * depth: LoadOp::Clear，mClear.mDepth = 1.0
//   * depth test / write: 全关（sky 永远在最远处）
// 主 pass 接 LoadOp::Load 把几何叠上去；depth = 1.0 作初始让任何 fragment
// 都通过 depth test。
//
// **不**走 gl_FragDepth 写 1.0 + 主 pass LoadOp::DontCare 路线，因为部分
// pass（god rays / bloom）会再次读 sceneDepth，要求 depth 在主 pass 完整
// 写入。让 sky 不写 depth 是更稳妥的策略——主 pass 自己 clear+写 depth。
//
// push constant 96 字节（必须与 Pipeline 端 SkyPush 字节布局严格一致）：
//   * invViewProj : mat4，64B；vUV → NDC → world far plane reconstruct
//   * cameraPos   : vec3，12B；归一化方向用 (farPos - cameraPos)
//   * intensity   : float，4B；EnvironmentComponent.intensity 直通
//   * tint        : vec3，12B；EnvironmentComponent.tint 直通
//   * pad         : float，4B；对齐 16B
//
// Y 轴翻转：fullscreen.vert 的 vUV = (NDC + 1) / 2，NDC.y 与 vUV.y 同向。
// Vulkan viewport y-flip 通常在 proj 矩阵里完成（glm::perspective 之后再
// flipY），所以 invViewProj * vec4(ndc, 1, 1) 直接给出 world 远平面点，
// 不需要在本 shader 内做额外翻转。
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform samplerCube uSkyCube;

layout(push_constant, std430) uniform Push
{
    mat4  uInvViewProj;
    vec3  uCameraPos;
    float uIntensity;
    vec3  uTint;
    float uPad0;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    // vUV ∈ [0, 1]² → NDC ∈ [-1, 1]²；z = 1.0 取远平面（透视除以后等同方向）
    vec2 ndc = vUV * 2.0 - 1.0;
    vec4 farH = pc.uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 farPos = farH.xyz / max(abs(farH.w), 1e-6) * sign(farH.w + 1e-6);

    vec3 dir = normalize(farPos - pc.uCameraPos);

    // textureLod mip 0：sky 用未经 prefilter 的原始 cubemap，避免 GGX 卷积
    // 给天空染上 roughness >0 的偏差。如未来要"软天空"看 cloudy 效果，
    // 切 textureLod(.., 较高 mip) 即可。
    vec3 sky = textureLod(uSkyCube, dir, 0.0).rgb;

    outColor = vec4(sky * pc.uTint * pc.uIntensity, 1.0);
}
