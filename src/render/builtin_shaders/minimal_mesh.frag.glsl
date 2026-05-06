#version 450

// 内置 minimal-mesh 片元 shader：固定橙色输出，不采样纹理、不查
// uniform。配合 minimal_mesh.vert 一起作为 Pipeline 接通 OrangeRender
// 的最小可运行 pipeline——证明 RHI / Renderer / SwapChain / draw call
// 链路完整可用。后续真实材质会替换这一对 shader。

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(1.0, 0.5, 0.2, 1.0);  // OrangeEngine 主色
}
