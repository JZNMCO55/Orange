#version 450

// 内置 toon-shading 片元 shader：用 dFdx / dFdy 在 fragment 阶段推 flat
// face normal（避开 Phase 2 MeshAsset 没 vertex normal attribute 的限
// 制），按 N·L 与 uShadowThreshold 做二阶 cel banding，warm / cool 两
// 色 mix。
//
// 当前 Pipeline 还没按 Material 路由 push-constant（Phase 3 / Task 04
// 才上）——本 shader 只是把模板的 uniform schema 落成可编译的 SPIR-V，
// Material 描述符里列出的 uniform 与这里 push_constant block 的字段
// 一一对应。

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vModelPos;

layout(location = 0) out vec4 outColor;

layout(push_constant, std430) uniform Toon {
    mat4  uMVP;
    vec3  uColorWarm;
    vec3  uColorCool;
    vec3  uLightDir;
    float uShadowThreshold;
} pc;

void main()
{
    // model-space face normal —— flat 着色，曲面 / 平面边界都能稳定取
    // 出 face direction。引入 vertex normal attribute 后切到 attribute-
    // driven smooth normal，本 push-constant block 不变。
    vec3 dx = dFdx(vModelPos);
    vec3 dy = dFdy(vModelPos);
    vec3 normal = normalize(cross(dx, dy));

    float NdotL = max(dot(normal, normalize(pc.uLightDir)), 0.0);
    float band  = step(pc.uShadowThreshold, NdotL);
    vec3  color = mix(pc.uColorCool, pc.uColorWarm, band);

    outColor = vec4(color, 1.0);
}
