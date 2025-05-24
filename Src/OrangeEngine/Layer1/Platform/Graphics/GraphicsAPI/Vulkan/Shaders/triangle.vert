#version 450

// 顶点属性
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// 输出到片段着色器
layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
} 