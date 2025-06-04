#version 450

// 输入属性
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// 输出到片段着色器
layout(location = 0) out vec3 fragColor;

// 统一缓冲区（暂时使用单位矩阵）
// 后续可以添加MVP矩阵支持
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main() {
    // 应用MVP变换
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    
    // 传递颜色到片段着色器
    fragColor = inColor;
} 