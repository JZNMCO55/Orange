#version 450

// ---------------------------------------------------------------------------
// 编辑器 viewport grid fragment shader —— fullscreen.vert（big-triangle）
// 配套。每像素从 vUV 反推 world-space 视线，与 Y=0 平面求交，按 Ben Golus
// "Pristine Grid" 算法在交点处计算双层 grid alpha（细线每 1 m，粗线每 10 m），
// 距离衰减后 alpha-blend 到 hdrColor 上。
//
// **深度遮挡走采样 sceneDepth + 手动 compare + discard** 而非 gl_FragDepth
// + depth test Less —— gl_FragDepth 路径在 Y=0 hit point 落在 camera 后方
// （ndcH.w < 0）或 cube 内部时撞精度问题让 grid 穿透几何（v0 实装的视觉
// bug）。直接采 sceneDepth 自己跟 grid hit 的 NDC z 比，硬性 discard 更
// 稳健。Pipeline 端为此 grid pipeline 关掉 depth attachment，改加 binding 0
// 的 sampler2D 接 sceneDepth。
//
// Pipeline 把本 pass 安排在主几何 pass **之后**、bloom 之前；color attachment
// LoadOp::Load + alpha blend，sceneDepth 在调用前 transition 到 ShaderReadOnly
// 供本 pass 采样，结束后由调用方按需翻回（god rays / 下一帧主 pass）。
//
// push constant 128 字节：
//   * uInvViewProj : mat4，64B；vUV → world reconstruct
//   * uViewProj    : mat4，64B；交点 world → NDC.z 与 sceneDepth 比较
// ---------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D uSceneDepth;

layout(push_constant, std430) uniform Push
{
    mat4 uInvViewProj;
    mat4 uViewProj;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// Pristine Grid 单层（Ben Golus 2022, "The Best Darn Grid Shader Yet"）。
// uv : grid 坐标（沿 plane 的 2 个 axis），lineWidth : 每条线占格子的比例。
float PristineGrid(vec2 uv, float lineWidth)
{
    vec4 uvDDXY = vec4(dFdx(uv), dFdy(uv));
    vec2 uvDeriv = vec2(length(uvDDXY.xz), length(uvDDXY.yw));
    bool invertLine = lineWidth > 0.5;
    float targetWidth = invertLine ? 1.0 - lineWidth : lineWidth;
    vec2 drawWidth = clamp(vec2(targetWidth), uvDeriv, vec2(0.5));
    vec2 lineAA = uvDeriv * 1.5;
    vec2 gridUV = abs(fract(uv) * 2.0 - 1.0);
    if (invertLine) { gridUV = 1.0 - gridUV; }
    vec2 grid2 = smoothstep(drawWidth + lineAA,
                            drawWidth - lineAA,
                            gridUV);
    grid2 *= clamp(targetWidth / drawWidth, 0.0, 1.0);
    grid2 = mix(grid2, vec2(targetWidth), clamp(uvDeriv * 2.0 - 1.0, 0.0, 1.0));
    if (invertLine) { grid2 = 1.0 - grid2; }
    return mix(grid2.x, 1.0, grid2.y);
}

void main()
{
    // 1. 反推 world-space ray：相机位置 = invViewProj * (0,0,0,1) 取 w
    //    归一化；ray dir = far - cameraPos。
    vec4 nearH = pc.uInvViewProj * vec4(0.0, 0.0, 0.0, 1.0);
    vec3 cameraPos = nearH.xyz / max(abs(nearH.w), 1e-6);

    vec2 ndc = vUV * 2.0 - 1.0;
    vec4 farH = pc.uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 farPos = farH.xyz / max(abs(farH.w), 1e-6) * sign(farH.w + 1e-6);

    vec3 rd = normalize(farPos - cameraPos);

    // 2. 与 Y=0 平面求交：t > 0 + 视线指向平面（cam.y > 0 && rd.y < 0 或
    //    反之）才有可见交点；同号视线打到平面背面或平行 → discard。
    float t = -cameraPos.y / rd.y;
    if (t <= 0.0 || !(t < 1.0e6)) { discard; }

    vec3 hit = cameraPos + t * rd;

    // 3. 投回 NDC 取 depth；ndcH.w < 0 表示 hit 在 camera 后方（数值上撞
    //    极端透视除法），保险起见 discard。
    vec4 ndcH = pc.uViewProj * vec4(hit, 1.0);
    if (ndcH.w <= 0.0) { discard; }
    float gridDepth = ndcH.z / ndcH.w;

    // 4. 与 sceneDepth 比较：grid 在场景几何后方 → discard（被遮挡）。
    //    Vulkan framebuffer depth = NDC z 范围 [0, 1]；主 pass 用 Clear depth = 1.0
    //    所以空像素 sceneDepth = 1.0 → grid 永远赢（背景区域）。
    float sceneDepth = texture(uSceneDepth, vUV).r;
    if (gridDepth >= sceneDepth) { discard; }

    // 5. PristineGrid 算法：两层 —— 细线（每 1 m，浅色低 alpha）+ 粗线
    //    （每 10 m，深色高 alpha），后者占主视觉锚点。
    vec2 fine  = hit.xz * 1.0;       // 每 1 m 一条
    vec2 major = hit.xz * 0.1;       // 每 10 m 一条

    float fineLine  = PristineGrid(fine,  0.01);
    float majorLine = PristineGrid(major, 0.02);

    // 6. 距离衰减：grid 远到 ~80 m 完全淡出。
    float dist = length(hit - cameraPos);
    float fade = 1.0 - smoothstep(20.0, 80.0, dist);

    // 7. 颜色合成：粗线灰白、细线灰；alpha 受 fade 调制；粗线 alpha 优先
    //    （max 不 add，避免叠加过亮）。
    vec3  colorFine  = vec3(0.42);
    vec3  colorMajor = vec3(0.65);
    float alphaFine  = fineLine  * 0.35 * fade;
    float alphaMajor = majorLine * 0.85 * fade;

    vec3  rgb   = mix(colorFine, colorMajor, clamp(majorLine, 0.0, 1.0));
    float alpha = max(alphaFine, alphaMajor);

    if (alpha <= 1e-4) { discard; }

    outColor = vec4(rgb, alpha);
}
