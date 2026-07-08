#version 450
// ---------------------------------------------------------------------------
// IBL 烘焙路径 c4：prefiltered specular cubemap（Karis split-sum 第一项）。
//
// 每个 mip level 对应一个固定 roughness，shader 对每像素 dispatch 一遍
// GGX importance sampling 卷积：
//   r=0  → mip 0（base）：复制环境（卷积核为 delta）
//   r=1  → mip N-1：roughness 1 完全模糊
//   中间 mip：r = mip / (mipCount - 1)
//
// **走 graphics fullscreen pass 而非 compute** 的原因：
//   * OrangeRender 当前 descriptor write（CombinedImageSampler / StorageImage）
//     都走 texture.GetDefaultView()（覆盖全 mip + 全 layer），不暴露子资源
//     view 作为 descriptor 资源。要 per-mip storage write cube 只能改走
//     ColorAttachment 路径——`CreateTextureView` 子资源 view 在
//     `ColorAttachment::mpView` 上已端到端验过（FEATURE-2026-05-17.T2/T3/T5
//     of OrangeRender），是 OrangeRender 本批 feature 的设计消费场景。
//   * 同时与 Lumix `data/shaders/ibl_filter.hlsl`（//@surface = fragment）
//     的实现风格对齐：face / mip / roughness 作 push constant 选维度。
//
// 数学（Orange-Wiki `techniques/rendering/image-based-lighting.md` § 项 1：
// 预过滤环境贴图 + `concepts/rendering/microfacet-theory.md` § GGX）：
//   prefiltered(n, r) = ∫_Ω Li(l) (n·l)+ dω  with importance sampling on
//                       GGX(NDF, r) for half-vector h, l = reflect(-v, h)
//   Karis 简化：v = n（避免 (n, v, r) 三维 LUT），结果在掠射角有偏差，由
//   c2 BRDF LUT 的 bias 项部分补偿
// ---------------------------------------------------------------------------

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube uEnvCube;

layout(push_constant) uniform PushConstants
{
    uint  uFaceIdx;       // 0..5
    float uRoughness;     // [0, 1]
    uint  uSampleCount;   // 典型 1024
    uint  uPad;           // 16 B 对齐占位
} pc;

const float kPi = 3.14159265358979323846;

// firefly 抑制：极亮 HDR texel（夜景路灯动辄数百量级 radiance）被单个 IS
// 样本命中会在粗糙 mip 留下亮斑点。逐样本钳制——反射里灯仍亮但不再出斑。
const float kMaxSampleRadiance = 24.0;

float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u)  | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u)  | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u)  | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u)  | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverseVdC(i));
}

// GGX D importance sampling —— 与 c2 BRDF LUT 同款公式（Karis 2013 /
// LearnOpenGL），保证烘焙↔着色阶段使用同一 NDF。
vec3 ImportanceSampleGGX(vec2 xi, vec3 n, float roughness)
{
    float a = roughness * roughness;

    float phi      = 2.0 * kPi * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    vec3 hTan;
    hTan.x = cos(phi) * sinTheta;
    hTan.y = sin(phi) * sinTheta;
    hTan.z = cosTheta;

    vec3 up        = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);

    return normalize(tangent * hTan.x + bitangent * hTan.y + n * hTan.z);
}

// face index + face 内 [-1, 1] uv → 世界方向。与 c1 / c3 同款。
vec3 CubeFaceDir(uint face, vec2 uv)
{
    if (face == 0u) return vec3( 1.0, -uv.y, -uv.x);   // +X
    if (face == 1u) return vec3(-1.0, -uv.y,  uv.x);   // -X
    if (face == 2u) return vec3( uv.x,  1.0,  uv.y);   // +Y
    if (face == 3u) return vec3( uv.x, -1.0, -uv.y);   // -Y
    if (face == 4u) return vec3( uv.x, -uv.y,  1.0);   // +Z
    return            vec3(-uv.x, -uv.y, -1.0);        // -Z
}

void main()
{
    // vUV ∈ [0, 1]²（fullscreen.vert big-triangle 输出）→ face 内 [-1, 1]²
    vec2 uv = vUV * 2.0 - 1.0;
    vec3 N = normalize(CubeFaceDir(pc.uFaceIdx, uv));

    // Karis 简化：v = n（避免 v 作为额外维度，使预积分仅依赖 (n, r)）。
    // 副作用：掠射角丢失各向异性 stretching，部分由 c2 BRDF LUT bias 补偿。
    vec3 R = N;
    vec3 V = R;

    vec3  prefilteredColor = vec3(0.0);
    float totalWeight      = 0.0;

    for (uint k = 0u; k < pc.uSampleCount; ++k)
    {
        vec2 xi = Hammersley(k, pc.uSampleCount);
        vec3 H  = ImportanceSampleGGX(xi, N, pc.uRoughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0)
        {
            // mip 0（roughness ≈ 0）下，IS 公式分母趋零，textureLod mip 0
            // 直接采样 envCube；这里仍走 weighted 平均，N=1024 数值稳定。
            vec3 li = min(textureLod(uEnvCube, L, 0.0).rgb,
                          vec3(kMaxSampleRadiance));
            prefilteredColor += li * NoL;
            totalWeight      += NoL;
        }
    }
    prefilteredColor /= max(totalWeight, 1e-4);

    outColor = vec4(prefilteredColor, 1.0);
}
