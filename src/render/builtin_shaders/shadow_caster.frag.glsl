#version 450

// 内置 shadow caster 片段 shader：空 main —— Vulkan 允许 fragment shader
// 完全省略，但留一个空 main 让 SPIR-V pipeline 默认行为可预测（不依赖
// driver fallback）。深度写由 depth attachment + 默认 GL_LESS 接管，无
// 需手动写 gl_FragDepth。
//
// 没有任何 uniform / 输入 / 输出——shadow pass 不读 uv、不写 color。

void main()
{
}
