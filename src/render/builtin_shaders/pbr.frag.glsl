#version 450
#extension GL_GOOGLE_include_directive : require

// 内置 PBR 片元 shader：monolithic Cook-Torrance + GGX + Smith correlated +
// Schlick fresnel + Lambert diffuse + IBL split-sum 三槽位。一份 shader 含
// direct + IBL 全路径，IBL 三纹理当前绑全局 dummy 1×1 黑（cube / 2D），
// 采样结果 = 0 自然退化为 direct-only；后续 dummy 替换为真实烘焙产物
// （BRDF LUT / irradiance / prefiltered specular），shader 一行不改。
//
// PBR 材质五通道：baseColor / metallic / roughness / ao 由 MaterialInstance
// 的 uBaseColor + uMRA 两条 vec4 uniform 驱动，经 vert push constant 透传
// 进 vBaseColor / vMRA varying；normal 通道暂走 vNormal vertex 插值，等
// tangent + 法线贴图基础设施落地后再上 texture 路径。texture binding（基
// 础色 / MR / AO / 法线贴图）整体延后到 per-instance descriptor set 路径
// 上线时一并接通。
//
// 数学约定（参 vendor/Orange-Wiki §microfacet-theory §fresnel-reflectance）：
//   D_GGX  = α² / (π · (NoH² · (α² - 1) + 1)²)
//   G_Smith correlated = 0.5 / (NoV·sqrt(α² + NoL²·(1-α²)) + NoL·sqrt(α² + NoV²·(1-α²)))
//   F_Schlick = F0 + (1 - F0) · (1 - VoH)^5
//   f_spec = D · G · F / (4 · NoL · NoV)  ←  G_Smith correlated 把 4·NoL·NoV 吸进去
//   F0 = lerp(0.04, baseColor, metallic)
//   kS = F；kD = (1 - kS) · (1 - metallic)
//   diffuse = kD · baseColor / π
//   α = roughness²（Disney convention，感知线性）

#include "include/shadow_pcf.glsl.inc"

const float kPi = 3.14159265359;

// set 0 与 textured_mesh / toon / rim_light / dissolve / emissive 全套共
// 用——Pipeline 在 Initialize 期统一布局。binding 2/3/4 由 PBR shader
// 引入，其他 shader 不引用即 dead-code，Vulkan spec 允许 shader-USED ⊆
// layout-DECLARED。
layout(set = 0, binding = 0) uniform sampler2D   uShadowMap;
layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;
    vec4 uLightDirIntensity;  // xyz = world direction（光从该方向"射出"），w = intensity
    vec4 uLightColor;         // xyz = rgb，w 未用
    vec4 uShadowParams;       // x = pcfKernelRadius，y = depthBias，z/w 预留
    vec4 uCameraWorldPos;     // xyz = camera worldPos
    vec4 uFrameInfo;          // x = time 秒，y/z/w 预留
} light;
layout(set = 0, binding = 2) uniform samplerCube uIrradiance;       // diffuse IBL — dummy zero in direct-only baseline
layout(set = 0, binding = 3) uniform samplerCube uPrefilteredEnv;   // specular IBL — dummy zero in direct-only baseline
layout(set = 0, binding = 4) uniform sampler2D   uBrdfLut;          // BRDF split-sum LUT — dummy zero in direct-only baseline

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec4 vBaseColor;
layout(location = 4) in vec4 vMRA;

layout(location = 0) out vec4 outColor;

// ----- BRDF helper（all in tangent / world space symmetric formulae）---------

float DistributionGGX(float NoH, float alpha)
{
    float a2     = alpha * alpha;
    float denom  = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * denom * denom);
}

// 高度相关 Smith G2（Heitz / Frostbite 推荐；与可分离形式同开销，粗糙
// 金属边缘更稳）。返回值已吸收 BRDF 分母里的 (4·NoL·NoV)，因此调用方
// f_spec = D · F · V（不再 /4·NoL·NoV）。
float VisibilitySmithCorrelated(float NoV, float NoL, float alpha)
{
    float a2 = alpha * alpha;
    float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (1.0 - F0) * f;
}

// IBL 端为了能量守恒还要带 roughness 因子的 Schlick 变体（Lagarde 2014）。
// dummy 时三纹理采样结果 = 0，IBL 贡献整体 = 0；该函数留在路径上等真实
// 烘焙纹理上线生效。
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * f;
}

// ----- main -----------------------------------------------------------------

void main()
{
    // ---- PBR 材质参数（per-instance，来自 MaterialInstance 的 uBaseColor /
    //      uMRA override，经 vert push constant 透传成 varying）
    vec3  baseColor = vBaseColor.rgb;
    float metallic  = clamp(vMRA.x, 0.0, 1.0);
    float roughness = clamp(vMRA.y, 0.04, 1.0);  // 下限避开 D_GGX α→0 奇异
    float ao        = clamp(vMRA.z, 0.0, 1.0);

    // α = roughness²（Disney convention，感知线性）
    float alpha = roughness * roughness;

    // F0：非金属 ≈ 0.04（典型介电），金属 = baseColor（金属"吸收"非反射）
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    // 几何 / 视图向量
    vec3 N = normalize(vNormal);
    vec3 V = normalize(light.uCameraWorldPos.xyz - vWorldPos);

    // 主 directional light 方向：uLightDirIntensity.xyz 是光的"传播方向"，
    // 取反 = 从表面指向光源
    vec3  L   = normalize(-light.uLightDirIntensity.xyz);
    vec3  H   = normalize(V + L);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 1e-5);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    // ---- Direct lighting（Cook-Torrance specular + Lambert diffuse）--------
    float D = DistributionGGX(NoH, alpha);
    float Vs = VisibilitySmithCorrelated(NoV, NoL, alpha);  // 含 1/(4·NoL·NoV)
    vec3  F = FresnelSchlick(VoH, F0);

    vec3 specular = D * Vs * F;
    vec3 kS       = F;
    vec3 kD       = (1.0 - kS) * (1.0 - metallic);
    vec3 diffuse  = kD * baseColor / kPi;

    vec3  radiance = light.uLightColor.rgb * light.uLightDirIntensity.w;
    float shadow   = SamplePcfShadow(uShadowMap, vWorldPos,
                                     light.uLightViewProj,
                                     int(light.uShadowParams.x),
                                     light.uShadowParams.y);
    vec3 directLo  = (diffuse + specular) * radiance * NoL * shadow;

    // ---- IBL（split-sum 近似；dummy 纹理全 0 → 贡献 = 0）-------------------
    vec3  Fibl       = FresnelSchlickRoughness(NoV, F0, roughness);
    vec3  kSibl      = Fibl;
    vec3  kDibl      = (1.0 - kSibl) * (1.0 - metallic);

    vec3  irradiance = texture(uIrradiance, N).rgb;
    vec3  iblDiffuse = kDibl * irradiance * baseColor;

    // Prefiltered specular cubemap 的 mip level ↔ roughness 映射；真实
    // 烘焙时按 mipCount-1 喂入。dummy 纹理只有 1 mip，textureLod 钳到 0
    // 自然安全（Vulkan textureLod 超界 clamp 到合法 mip）。
    const float kMaxReflectionLod = 4.0;
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(uPrefilteredEnv, R, roughness * kMaxReflectionLod).rgb;

    vec2  brdf       = texture(uBrdfLut, vec2(NoV, roughness)).rg;
    vec3  iblSpec    = prefiltered * (Fibl * brdf.x + brdf.y);

    vec3  iblLo      = (iblDiffuse + iblSpec) * ao;

    // ---- 合成 ----
    vec3 color = directLo + iblLo;
    outColor   = vec4(color, 1.0);
}
