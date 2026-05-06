#version 450

// 内置 textured-mesh 片元 shader：当前阶段不接 sampler，从 uv 程序
// 式合成可见的 checker（与 OrangeRender textured_mesh sample 同一思
// 路）—— RHI 的 descriptor-set / sampler 路径还没在引擎层接通，但顶
// 点流 + push-constant + 帧生命周期已经全部走真实路径。
//
// Phase 3+ 真正接入 Material / Sampler descriptor 时，这一段会被替
// 换为 `texture(sampler2D(uTex), vUV)` 的标准路径。

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    // 8x8 棋盘 + 一点 uv 渐变，作为"贴图存在"的视觉证据
    vec2  cell  = floor(vUV * 8.0);
    float odd   = mod(cell.x + cell.y, 2.0);
    vec3  warm  = vec3(1.0, 0.55, 0.20);   // OrangeEngine 主色
    vec3  cool  = vec3(0.10, 0.18, 0.32);
    vec3  base  = mix(cool, warm, odd);

    // 沿 uv 加一点 tint，让 checker 不显得机械
    base *= 0.85 + 0.15 * vec3(vUV.x, vUV.y, 1.0 - vUV.x);

    outColor = vec4(base, 1.0);
}
