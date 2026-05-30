#version 450

// debug-view overdraw 片段 shader（DebugViewMode::Overdraw）：每个片段输出同一个
// 小常量色，配合 pipeline 的 additive blend（src ONE + dst ONE）+ depth test 关
// 累加——同一像素被覆盖越多次，累积越亮，形成 overdraw 热图（暗=覆盖少，亮=覆盖
// 多 / 过绘严重）。RGB 略偏暖（R>G>B）让热图从暗红→橙→黄→白渐变，单次覆盖也
// 可见；约 5~6 次覆盖逼近白。

layout(location = 0) in vec2 vUV;      // 声明匹配 vert out location 0（不使用）
layout(location = 1) in vec3 vNormal;  // 声明匹配 vert out location 1（不使用）

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(0.18, 0.11, 0.04, 1.0);
}
