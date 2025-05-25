#version 450

// 输入顶点属性
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// 输出到片段着色器
layout(location = 0) out vec3 fragColor;

void main() {
    // 直接传递位置（已经是NDC坐标）
    gl_Position = vec4(inPosition, 1.0);
    
    // 传递颜色到片段着色器
    fragColor = inColor;
} 