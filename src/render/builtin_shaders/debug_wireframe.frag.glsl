#version 450

// debug-view wireframe 片段 shader（DebugViewMode::Wireframe）：输出纯亮绿色。
// pipeline 用 polygonMode=Line（需 device feature fillModeNonSolid）把三角形画成
// 边线，用于诊断网格密度 / 拓扑 / UV 接缝 / 隐藏几何。无光照 / 无贴图，纯诊断。

layout(location = 0) in vec2 vUV;      // 声明匹配 vert out location 0（不使用）
layout(location = 1) in vec3 vNormal;  // 声明匹配 vert out location 1（不使用）

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(0.40, 1.0, 0.55, 1.0);
}
