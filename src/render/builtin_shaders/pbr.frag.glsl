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
    vec4 uIblFactor;          // xyz = EnvironmentComponent.tint * intensity（host 端预乘），w 预留
} light;
layout(set = 0, binding = 2) uniform samplerCube uIrradiance;       // diffuse IBL — dummy zero in direct-only baseline
layout(set = 0, binding = 3) uniform samplerCube uPrefilteredEnv;   // specular IBL — dummy zero in direct-only baseline
layout(set = 0, binding = 4) uniform sampler2D   uBrdfLut;          // BRDF split-sum LUT — dummy zero in direct-only baseline

// GAP-2026-05-11 G2：PointLights UBO（独立 binding 5，host 端 Pipeline
// 把场景内 cap=8 个 PointLight 喂进来；超出截断）。其他内置 shader 不引
// 用本 binding（dead-code）。
#define ORANGE_MAX_POINT_LIGHTS 8
struct PointLightData
{
    vec4 posRange;       // xyz = world pos, w = range
    vec4 colorIntensity; // xyz = linear rgb, w = intensity
};
layout(set = 0, binding = 5, std140) uniform PointLightsUbo
{
    uvec4          uPointLightCountPad;  // x = count, y/z/w pad
    PointLightData uPointLights[ORANGE_MAX_POINT_LIGHTS];
} pointLights;

// GAP-2026-05-26 G1：SpotLights UBO（独立 binding 6）。锥光 = point 物理基
// inverse-square 衰减 × 锥角软边。host 端 cap=8，超出截断。
#define ORANGE_MAX_SPOT_LIGHTS 8
struct SpotLightData
{
    vec4 posRange;       // xyz = world pos, w = range
    vec4 dirCosOuter;    // xyz = spot dir（normalized）, w = cos(outerConeAngle)
    vec4 colorIntensity; // xyz = linear rgb, w = intensity
    vec4 cosInnerShadow; // x = cos(innerConeAngle), y = shadow index（G2；<0=无）, z/w pad
};
layout(set = 0, binding = 6, std140) uniform SpotLightsUbo
{
    uvec4         uSpotLightCountPad;  // x = count, y/z/w pad
    SpotLightData uSpotLights[ORANGE_MAX_SPOT_LIGHTS];
} spotLights;

// GAP-2026-05-26 G2：spot 透视阴影。binding 7 = sampler2DArray（每个
// castsShadow spot 占一层），binding 8 = per-caster light view-proj 数组。
// SpotLightData.cosInnerShadow.y 是 layer index（<0 = 该 spot 无阴影）。
#define ORANGE_MAX_SPOT_SHADOWS 4
layout(set = 0, binding = 7) uniform sampler2DArray uSpotShadowMaps;
layout(set = 0, binding = 8, std140) uniform SpotShadowUbo
{
    uvec4 uSpotShadowCountPad;
    mat4  uSpotLightViewProj[ORANGE_MAX_SPOT_SHADOWS];
} spotShadow;

// set 1 = per-instance material 贴图（GAP-2026-05-25 A2 / G1）。Pipeline 按
// MaterialInstance 的 texture 槽分配 / 更新本 set；未绑的槽喂 default 贴图
// （白 baseColor/MR/AO + flat-normal (0.5,0.5,1)），使采样结果 ×scalar = scalar、
// 法线不扰动 —— 没绑贴图时输出与纯 scalar PBR 完全一致（零回归）。
//   binding 0: baseColor（sRGB→linear 已由贴图 format 处理，这里按 linear 用）
//   binding 1: tangent-space normal map（RGB 编码 [0,1] → [-1,1]）
//   binding 2: metalRough（glTF 约定 G=roughness, B=metallic）
//   binding 3: ambient occlusion（R 通道）
layout(set = 1, binding = 0) uniform sampler2D uBaseColorTex;
layout(set = 1, binding = 1) uniform sampler2D uNormalTex;
layout(set = 1, binding = 2) uniform sampler2D uMetalRoughTex;
layout(set = 1, binding = 3) uniform sampler2D uAoTex;

layout(location = 0) in vec2  vUV;
layout(location = 1) in vec3  vWorldPos;
layout(location = 2) in vec3  vNormal;
layout(location = 3) in vec4  vBaseColor;
layout(location = 4) in vec4  vMRA;
layout(location = 5) in vec3  vWorldTangent;
layout(location = 6) in float vTangentSign;

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
    // ---- PBR 材质参数（per-instance scalar override × set 1 贴图）------------
    //   scalar 来自 MaterialInstance 的 uBaseColor / uMRA（经 vert push constant
    //   透传成 varying）；贴图来自 set 1（未绑时为 default 白 / flat-normal，
    //   乘子 = 1 → 退化为纯 scalar）。glTF 约定 metalRough 贴图 G=roughness、
    //   B=metallic；ao 贴图取 R。
    vec4  baseTex   = texture(uBaseColorTex, vUV);
    vec3  baseColor = vBaseColor.rgb * baseTex.rgb;
    vec3  mrTex     = texture(uMetalRoughTex, vUV).rgb;
    float metallic  = clamp(vMRA.x * mrTex.b, 0.0, 1.0);
    float roughness = clamp(vMRA.y * mrTex.g, 0.04, 1.0);  // 下限避开 D_GGX α→0 奇异
    float ao        = clamp(vMRA.z * texture(uAoTex, vUV).r, 0.0, 1.0);

    // α = roughness²（Disney convention，感知线性）
    float alpha = roughness * roughness;

    // F0：非金属 ≈ 0.04（典型介电），金属 = baseColor（金属"吸收"非反射）
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    // ---- 法线：几何法线 + 切线空间法线贴图扰动（TBN）----------------------
    //   Gram-Schmidt 把插值后的世界切线对几何法线正交化，副切线由手性符号
    //   叉乘得到。退化切线（length≈0：default (1,0,0) 恰与 +X 法线平行 / 零缩
    //   放）时跳过法线贴图、直接用几何法线，避免 mat3 列含 NaN 被 0×NaN 传染。
    vec3  Ngeom = normalize(vNormal);
    vec3  tProj = vWorldTangent - Ngeom * dot(Ngeom, vWorldTangent);
    float tLen  = length(tProj);
    vec3  N;
    if (tLen > 1e-4)
    {
        vec3 T = tProj / tLen;
        vec3 B = cross(Ngeom, T) * vTangentSign;
        vec3 nTex = texture(uNormalTex, vUV).xyz * 2.0 - 1.0;
        N = normalize(mat3(T, B, Ngeom) * nTex);
    }
    else
    {
        N = Ngeom;
    }
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
    vec3  iblSpec_ss = prefiltered * (Fibl * brdf.x + brdf.y);
    // Multi-scatter 能量补偿（Fdez-Aguero 2019 简化 / Filament `light_indirect.fs`
    // 同款）：single-scatter GGX 在 high roughness 段把数十百分之能量散失到
    // multi-bounce 域，BRDF LUT 只记 single-bounce 命中——furnace test 顶行右
    // metallic=1 roughness=0.9 几乎纯黑即此症状。补偿系数 1 + F0·(1/brdf.y - 1)
    // 把缺失能量按 "所有丢失光在表面继续反射" 近似补回，white furnace + 任意
    // 材质应近似输出全白。
    vec3  iblSpec    = iblSpec_ss * (1.0 + F0 * (1.0 / max(brdf.y, 1e-4) - 1.0));

    // EnvironmentComponent.tint * intensity（host 端预乘成 uIblFactor.rgb）
    // 一次性作用到 diffuse + specular IBL 两项。dummy IBL 阶段
    // irradiance / prefiltered 全 0 → 相乘仍 0，本乘子无视觉影响；c7
    // SetIblTextures 喂入真实烘焙产物后立即生效，无需再改 shader。
    vec3  iblLo      = (iblDiffuse + iblSpec) * ao * light.uIblFactor.rgb;

    // ---- Point lights（GAP-2026-05-11 G2）：cap=8 物理基 inverse-square +
    //      smoothstep range cutoff。castsShadow 字段当前忽略（omnidirectional
    //      shadow 是 Phase 10 量级）。无 light 时 count=0 → loop 跳过 0 次开销。
    vec3 ptLo = vec3(0.0);
    uint pointLightCount = min(pointLights.uPointLightCountPad.x,
                                uint(ORANGE_MAX_POINT_LIGHTS));
    for (uint i = 0u; i < pointLightCount; ++i)
    {
        vec3  pLightPos = pointLights.uPointLights[i].posRange.xyz;
        float pRange    = pointLights.uPointLights[i].posRange.w;
        vec3  pColor    = pointLights.uPointLights[i].colorIntensity.rgb;
        float pInten    = pointLights.uPointLights[i].colorIntensity.w;
        if (pRange <= 0.0) { continue; }

        vec3  pL_unnorm = pLightPos - vWorldPos;
        float pDist     = length(pL_unnorm);
        if (pDist > pRange) { continue; }
        vec3  pL  = pL_unnorm / max(pDist, 1e-5);
        vec3  pH  = normalize(V + pL);
        float pNoL = max(dot(N, pL), 0.0);
        if (pNoL <= 0.0) { continue; }
        float pNoH = max(dot(N, pH), 0.0);
        float pVoH = max(dot(V, pH), 0.0);

        float pD  = DistributionGGX(pNoH, alpha);
        float pVs = VisibilitySmithCorrelated(NoV, pNoL, alpha);
        vec3  pF  = FresnelSchlick(pVoH, F0);

        vec3  pSpec = pD * pVs * pF;
        vec3  pkS   = pF;
        vec3  pkD   = (1.0 - pkS) * (1.0 - metallic);
        vec3  pDiff = pkD * baseColor / kPi;

        // 物理基 inverse-square + smoothstep range cutoff：dist=0 时
        // attenuation 接近上限（防 0 除），dist→range 平滑到 0。与 Cocos
        // pointLight / Godot OmniLight 同款。
        float minD2     = 0.01;  // 1cm 防 0 除
        float dist2     = max(pDist * pDist, minD2);
        float invSquare = 1.0 / dist2;
        float fade      = smoothstep(pRange, 0.0, pDist);  // dist=0→1, dist=range→0
        float atten     = invSquare * fade;

        vec3 pRadiance = pColor * pInten * atten;
        ptLo += (pDiff + pSpec) * pRadiance * pNoL;
    }

    // ---- Spot lights（GAP-2026-05-26 G1）：point 物理基衰减 × 锥角软边。
    //      cone = smoothstep(cosOuter, cosInner, dot(spotDir, -L))；spotDir 是
    //      光的传播方向，-L 是从光指向表面的方向，二者越对齐越在锥心。
    //      castsShadow（cosInnerShadow.y >= 0）的阴影采样留 G2。
    vec3 spotLo = vec3(0.0);
    uint spotLightCount = min(spotLights.uSpotLightCountPad.x,
                               uint(ORANGE_MAX_SPOT_LIGHTS));
    for (uint i = 0u; i < spotLightCount; ++i)
    {
        vec3  sLightPos = spotLights.uSpotLights[i].posRange.xyz;
        float sRange    = spotLights.uSpotLights[i].posRange.w;
        vec3  sDir      = spotLights.uSpotLights[i].dirCosOuter.xyz;
        float sCosOuter = spotLights.uSpotLights[i].dirCosOuter.w;
        vec3  sColor    = spotLights.uSpotLights[i].colorIntensity.rgb;
        float sInten    = spotLights.uSpotLights[i].colorIntensity.w;
        float sCosInner = spotLights.uSpotLights[i].cosInnerShadow.x;
        int   sShadowIdx = int(spotLights.uSpotLights[i].cosInnerShadow.y);  // <0 = 无阴影
        if (sRange <= 0.0) { continue; }

        vec3  sL_unnorm = sLightPos - vWorldPos;
        float sDist     = length(sL_unnorm);
        if (sDist > sRange) { continue; }
        vec3  sL = sL_unnorm / max(sDist, 1e-5);

        // 锥角软边：dot(spotDir, -sL) = surface 在锥轴上的投影 cos。
        float spotCos    = dot(normalize(sDir), -sL);
        float coneFactor = smoothstep(sCosOuter, sCosInner, spotCos);
        if (coneFactor <= 0.0) { continue; }

        vec3  sH   = normalize(V + sL);
        float sNoL = max(dot(N, sL), 0.0);
        if (sNoL <= 0.0) { continue; }
        float sNoH = max(dot(N, sH), 0.0);
        float sVoH = max(dot(V, sH), 0.0);

        float sD  = DistributionGGX(sNoH, alpha);
        float sVs = VisibilitySmithCorrelated(NoV, sNoL, alpha);
        vec3  sF  = FresnelSchlick(sVoH, F0);

        vec3  sSpec = sD * sVs * sF;
        vec3  skS   = sF;
        vec3  skD   = (1.0 - skS) * (1.0 - metallic);
        vec3  sDiff = skD * baseColor / kPi;

        // 物理基 inverse-square + smoothstep range cutoff（同 point light）。
        float sMinD2     = 0.01;
        float sDist2     = max(sDist * sDist, sMinD2);
        float sInvSquare = 1.0 / sDist2;
        float sFade      = smoothstep(sRange, 0.0, sDist);
        float sAtten     = sInvSquare * sFade * coneFactor;

        // 透视阴影：castsShadow 的 spot 有有效 layer index 时采 spot shadow
        // array + PCF；否则 sShadow = 1（无阴影，与 G1 行为一致）。
        float sShadow = 1.0;
        if (sShadowIdx >= 0 && sShadowIdx < ORANGE_MAX_SPOT_SHADOWS)
        {
            sShadow = SamplePcfShadowArray(uSpotShadowMaps, sShadowIdx, vWorldPos,
                                           spotShadow.uSpotLightViewProj[sShadowIdx],
                                           int(light.uShadowParams.x),
                                           light.uShadowParams.y);
        }

        vec3 sRadiance = sColor * sInten * sAtten;
        spotLo += (sDiff + sSpec) * sRadiance * sNoL * sShadow;
    }

    // ---- 合成 ----
    vec3 color = directLo + ptLo + spotLo + iblLo;
    outColor   = vec4(color, 1.0);
}
