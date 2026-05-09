#version 450

// 09_vfx_demo / TintOverlayPass —— 验证 Pipeline::InsertPass 真的把
// game-side 自定义 IRenderPass 串进帧路径的最小可视证据。
//
// 在 vUV.y > 0.88 的屏底 band 处输出 HDR 青色——配合 pipeline 加性
// blend，主图主区域不受任何影响。放屏底而不是屏顶是为了避开 09_vfx_demo
// 里 god rays 的 sun disk（位于屏顶左上），后者会把所有 RGB 同向拉
// 高让叠加 cyan 看起来变成白色；屏底 god rays 衰减干净，tint 保持
// 清晰可辨的青色。
//
// 顶点 shader 复用引擎的 fullscreen.vert（big-triangle），不需要单独
// 写一份。

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    if (vUV.y < 0.88)
    {
        discard;
    }
    // band 内部按 vUV.y 做软边——靠近底端 (vUV.y=1) 最亮、靠近 0.88
    // 上边界淡出。
    float t = smoothstep(0.88, 1.0, vUV.y);
    // HDR cyan：值适度 > 1 让 bloom pass 拾取产出轻微 halo；alpha 留 1
    // （pipeline 用 srcAlpha=ONE/dstAlpha=ONE 加性累加，alpha 仅供
    // bloom luma 计算参考）。
    outColor = vec4(vec3(0.0, 1.4, 1.7) * t, 1.0);
}
