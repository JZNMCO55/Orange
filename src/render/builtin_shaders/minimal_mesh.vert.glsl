#version 450

// 内置 minimal-mesh 顶点 shader：用 gl_VertexIndex 生成一个全屏三角
// 形（顶点写死，不读 vertex buffer）。这是 Pipeline 接通 OrangeRender
// RenderGraph 的最小验证路径——后续 task 把 drawable 列表的 mesh
// 数据接进来时，会替换为带 vertex input layout 的版本。

vec2 kPositions[3] = vec2[](
    vec2( 0.0, -0.6),
    vec2(-0.6,  0.6),
    vec2( 0.6,  0.6)
);

void main()
{
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
}
