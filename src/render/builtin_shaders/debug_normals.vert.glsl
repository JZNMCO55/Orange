#version 450

// 内置 debug-view normals 顶点 shader（DebugViewMode::Normals）：把 world-space
// normal 透传到 frag，frag 映射到 RGB 作法线可视化。顶点输入布局与 pbr/textured
// 同款（pos+uv+normal，由 FillVertexInputLayout 喂入；本 material usesTangentVertex
// = false，不声明 location 3 tangent）。push constant 复用 {uMVP, uModel} 128B
// （drawable loop 的 pcSize>=128 分支）。inUV 读入并透传——保证 location 1 被
// vertex shader 消费，不触发 "location 1 not consumed" validation 警告。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;

layout(push_constant, std430) uniform Push {
    mat4 uMVP;    //   0  64
    mat4 uModel;  //  64  64
} pc;

void main()
{
    // 无非均匀缩放近似（与 pbr.vert 同款 mat3(uModel)）。
    vNormal     = mat3(pc.uModel) * inNormal;
    vUV         = inUV;  // 透传，保证 location 1 被消费
    gl_Position = pc.uMVP * vec4(inPosition, 1.0);
}
