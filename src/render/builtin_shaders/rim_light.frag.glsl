#version 450

// 内置 rim-light 片元 shader：fresnel 风格 rim glow——用 dFdx / dFdy
// 在 fragment 阶段推 flat face normal，再用 (uViewPos - vModelPos) 当
// view direction，rim = pow(1 - max(N·V, 0), uRimPower)。
//
// uViewPos 当前是 model-space 的退化解（Pipeline 还没按 Material 路由
// 把 view-space 转换喂进来——Phase 3 / Task 04 才上），视觉上仍能给出
// "立方体边沿发光" 的 rim 效果，足以验证模板侧 schema 落地。Task 06
// 引入 world-space view position 时 push-constant block 不变、只是 uViewPos
// 的语义升级为 world-space camera position。

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vModelPos;

layout(location = 0) out vec4 outColor;

layout(push_constant, std430) uniform RimLight {
    mat4  uMVP;
    vec3  uViewPos;
    vec3  uRimColor;
    float uRimPower;
    float uRimIntensity;
} pc;

void main()
{
    vec3 dx = dFdx(vModelPos);
    vec3 dy = dFdy(vModelPos);
    vec3 normal = normalize(cross(dx, dy));

    vec3 viewDir = normalize(pc.uViewPos - vModelPos);
    float rim   = pow(1.0 - max(dot(normal, viewDir), 0.0), pc.uRimPower);
    vec3  color = pc.uRimColor * rim * pc.uRimIntensity;

    outColor = vec4(color, rim);
}
