#version 450

// 从顶点着色器接收颜色
layout(location = 0) in vec3 fragColor;

// 输出颜色
layout(location = 0) out vec4 outColor;

void main() {
    // 直接使用插值后的颜色
    outColor = vec4(fragColor, 1.0);
} 