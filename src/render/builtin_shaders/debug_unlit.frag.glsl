#version 450

// debug-view unlit 片段 shader（DebugViewMode::Unlit）：直出 base color，无光照
// / 无贴图。看 albedo 本色，剥离所有光照 / 阴影 / IBL 干扰，纯诊断。

layout(location = 0) in vec4 vBaseColor;
layout(location = 1) in vec2 vUV;      // 声明匹配 vert out location 1（不使用）
layout(location = 2) in vec3 vNormal;  // 声明匹配 vert out location 2（不使用）

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vBaseColor.rgb, 1.0);
}
