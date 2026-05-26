#version 450

// 内置色彩分级（color grading）pass —— 线性 HDR 空间、tonemap 之前的画面定调：
// 曝光 → 白平衡 → 对比度（围绕中灰 pivot）→ 饱和度（向 luma 收）。结果写独立
// gradeColor，composite 再 replace 回 HDR（避免读写同 target）。
//
// 在 tonemap（ACES）之前的线性空间做：曝光是物理的（场景亮度缩放），白平衡 /
// 对比度 / 饱和度在线性下也自洽。ACES 之后的 display-space 微调留给未来 LUT。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1, std140) uniform GradeUbo
{
    vec4 uParams;        // x=exposure(stops), y=contrast, z=saturation, w 预留
    vec4 uWhiteBalance;  // rgb = 白平衡乘子（host 由 temperature/tint 算），w 预留
} grade;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 c = texture(uHdrColor, vUV).rgb;

    // 曝光（stops）：c *= 2^exposure。
    c *= exp2(grade.uParams.x);

    // 白平衡：per-channel 乘子（暖/冷 + 绿/品红）。
    c *= grade.uWhiteBalance.rgb;

    // 对比度：围绕线性中灰 pivot=0.18 拉伸。
    const float kPivot = 0.18;
    c = (c - kPivot) * grade.uParams.y + kPivot;

    // 饱和度：向感知 luma 收 / 放。
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, grade.uParams.z);

    outColor = vec4(max(c, vec3(0.0)), 1.0);
}
