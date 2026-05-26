#version 450

// 内置景深 composite pass —— 把 dof gather 的结果（dofColor）写回 hdrColor
// （replace blend，One/Zero）。dofColor 已是"对焦区锐利 + 离焦模糊"的完整
// 场景色，直接替换 HDR 内容，供后续 bloom / tonemap / passthrough 消费。
//
// 单独一个 copy pass（而非 dof gather 直接写 HDR）是为避免 gather 读 HDR
// 邻域同时写 HDR 同 target 的反馈（与 SSR ssrColor 分离同理）。

layout(set = 0, binding = 0) uniform sampler2D uDofColor;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(texture(uDofColor, vUV).rgb, 1.0);
}
