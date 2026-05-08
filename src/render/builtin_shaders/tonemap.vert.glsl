#version 450

// Tonemap 顶点 shader —— big-triangle，与 fullscreen.vert 同思路。单
// 列出来是为了让 tonemap 在 PR review / debug capture 工具里有独立的
// shader name，方便 GPU profiling 分类。功能上完全等价于 fullscreen.vert。

layout(location = 0) out vec2 vUV;

void main()
{
    vec2 pos = vec2((gl_VertexIndex == 1) ?  3.0 : -1.0,
                    (gl_VertexIndex == 2) ?  3.0 : -1.0);
    vUV         = (pos + vec2(1.0)) * 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
