#version 450

// 内置 fullscreen 顶点 shader：big-triangle 技巧——按 gl_VertexIndex
// 推出三角形覆盖 [-1, 1]² 的 NDC，UV 在 [0, 1]² 范围内透传给 fragment。
// 不需要 vertex buffer / vertex layout，调用方 Draw(3, 1, 0, 0) 即可。
//
// 双段 frame 流程的 stage B 用本 vert 配合 passthrough.frag 把离屏 HDR
// 采样到 swap-chain；后续 bloom_upsample / tonemap 等 fullscreen pass
// 复用同一 vert（vUV 透传给各自 fragment）。

layout(location = 0) out vec2 vUV;

void main()
{
    vec2 pos = vec2((gl_VertexIndex == 1) ?  3.0 : -1.0,
                    (gl_VertexIndex == 2) ?  3.0 : -1.0);
    vUV         = (pos + vec2(1.0)) * 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
