#version 450

// 编辑器内置 fullscreen 顶点 shader（big-triangle 技巧 —— gl_VertexIndex
// 推出覆盖 [-1, 1]² 的三角形，UV 透传给 fragment）。
//
// 与 OrangeEngine 端的 fullscreen.vert.glsl 是字字相同的 logic copy —— 该
// 文件不属编辑器专属审美（任何 fullscreen pass 都用同款模板），但编辑器
// 维持自家 spv 编译路径避免依赖 engine 端运行时 shader 文件，符合
// "engine 端 grid 真迁出后剔除所有 editor 审美 shader" 的 v1.3.0 目标。
//
// 调用方 Draw(3, 1, 0, 0) 即可；不需要 vertex buffer / vertex layout。

layout(location = 0) out vec2 vUV;

void main()
{
    vec2 pos = vec2((gl_VertexIndex == 1) ?  3.0 : -1.0,
                    (gl_VertexIndex == 2) ?  3.0 : -1.0);
    vUV         = (pos + vec2(1.0)) * 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
