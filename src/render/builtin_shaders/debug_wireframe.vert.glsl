#version 450

// 内置 debug-view wireframe 顶点 shader（DebugViewMode::Wireframe）：只做 MVP
// 变换，frag 输出纯色边线（pipeline 的 polygonMode=Line 把三角形画成线框）。顶点
// 输入布局与 pbr/textured 同款（pos+uv+normal，由 FillVertexInputLayout 喂入；本
// material usesTangentVertex=false，不声明 location 3 tangent）。push constant 复用
// {uMVP, uModel} 128B（drawable loop 的 pcSize>=128 分支）。inUV / inNormal 读入并
// 透传——保证 location 1/2 被 vertex shader 消费，不触发 validation 警告。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 vUV;      // 透传，保证 location 1 被消费
layout(location = 1) out vec3 vNormal;  // 透传，保证 location 2 被消费

layout(push_constant, std430) uniform Push {
    mat4 uMVP;    //   0  64
    mat4 uModel;  //  64  64（wireframe 不读，仅占位令 push 大小与 drawable loop 喂入一致）
} pc;

void main()
{
    vUV         = inUV;
    vNormal     = inNormal;
    gl_Position = pc.uMVP * vec4(inPosition, 1.0);
}
