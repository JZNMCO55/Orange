#version 450

// debug-view normals 片段 shader（DebugViewMode::Normals）：world-space normal
// 映射到 RGB（[-1,1] → [0,1]）作可视化。无光照 / 无贴图，纯诊断。

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;  // 声明匹配 vert out location 1（不使用）

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 n = normalize(vNormal);
    outColor = vec4(n * 0.5 + 0.5, 1.0);
}
