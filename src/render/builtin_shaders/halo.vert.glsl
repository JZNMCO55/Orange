#version 450

// 内置 halo 顶点 shader —— GAP-2026-05-11 G3 可见光晕路径。Pipeline 为
// 每个 haloEnabled=true 的 PointLight record 一次本 shader 的 draw：
// model = translate(light.position) * scale(haloRadius)，几何走标准
// MeshAsset vertex input（pos + uv + normal，stride 48B），与 textured /
// toon / emissive 同 layout，便于 GetOrCompilePipeline 复用主 forward 的
// vertex input layout。
//
// vert 阶段把 emissive 颜色 × 强度乘子打包传给 frag（避免 frag stage 也
// 读 push constant，与 emissive 同款"push constant only VS"模式），fragment
// 直接 output 该色——纯自发光，无光照 / 阴影计算。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;     // 占位，halo frag 不读
layout(location = 2) in vec3 inNormal; // 占位，halo frag 不读

// vHaloColor = light.color * (light.intensity * light.haloIntensity)，
// 已预乘强度乘子，frag 直接喂 outColor。
layout(location = 0) out vec3 vHaloColor;
// 占位 varying（保持与 emissive / textured 同 layout，frag 不读由 driver
// dead-code-elim；与 emissive.vert.glsl 同款"为复用 mainDescLayout 而保留
// 占位"的兼容写法）。
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vNormal;

layout(push_constant, std430) uniform Push
{
    mat4 uMVP;
    mat4 uModel;
    // .rgb = light.color，.a = light.intensity * light.haloIntensity
    //（vert stage 预乘强度，frag 用 outColor.rgb = vHaloColor 直接发光）。
    vec4 uHaloColorIntensity;
} pc;

void main()
{
    vHaloColor  = pc.uHaloColorIntensity.rgb * pc.uHaloColorIntensity.a;
    vWorldPos   = vec3(0.0);
    vNormal     = vec3(0.0);
    gl_Position = pc.uMVP * vec4(inPosition, 1.0);
}
