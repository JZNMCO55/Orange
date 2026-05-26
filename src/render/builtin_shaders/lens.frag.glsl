#version 450

// 内置镜头效果（lens）pass —— 色散（chromatic aberration）+ 暗角（vignette），
// 一个 fullscreen pass 内合成。读 hdrColor 写独立 lensColor（CA 沿径向偏移采样
// 邻域，故需读写分离，与 DoF/SSR 同款），lens_composite（复用 dof_composite）
// 再 replace 回 HDR。
//
// 色散：沿屏幕径向（中心 → 边缘）给 R / B 通道反向偏移采样，模拟镜头 RGB 折射
// 不同步——边缘越远偏移越大（×dist），中心无色散。
// 暗角：径向距离的平滑衰减乘子，边缘压暗、聚焦画面中心。
//
// 二者均"量为 0 即无效果"，pass 的 enabled 只总控是否录制；调用方单独把某项
// 调 0 即可只留另一项。stylized 2.5D 平台跳跃常用（聚焦 + 镜头质感）。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1, std140) uniform LensUbo
{
    // x=chromaticAberration(径向 uv 偏移量), y=vignetteIntensity(0..1),
    // z=vignetteSmoothness(过渡软硬), w 预留
    vec4 uParams;
} lens;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    float ca         = lens.uParams.x;
    float vigInt     = lens.uParams.y;
    float vigSmooth  = lens.uParams.z;

    // 屏幕中心 → 当前像素的径向矢量（aspect 暂不校正，stylized 圆形足够）。
    vec2  dir  = vUV - vec2(0.5);
    float dist = length(dir) * 2.0;   // 0 中心 .. ~1.41 角落

    // ---- 色散：沿径向给 R / B 反向偏移采样（量 × dist，中心无偏移）----
    vec3 col;
    if (ca > 1e-6)
    {
        vec2 off = dir * ca * dist;
        col.r = texture(uHdrColor, vUV - off).r;
        col.g = texture(uHdrColor, vUV).g;
        col.b = texture(uHdrColor, vUV + off).b;
    }
    else
    {
        col = texture(uHdrColor, vUV).rgb;
    }

    // ---- 暗角：径向平滑衰减，边缘压暗 ----
    if (vigInt > 1e-6)
    {
        // vigSmooth 决定暗角从哪个半径开始渐变（越大越往中心、渐变越宽）；
        // vmask 0（中心不变）.. 1（半径 1.0 处最暗）；vigInt 决定最大压暗量。
        float vstart = 1.0 - clamp(vigSmooth, 0.0, 1.0);
        float vmask  = smoothstep(vstart, 1.0, dist);
        col *= 1.0 - vmask * vigInt;
    }

    outColor = vec4(col, 1.0);
}
