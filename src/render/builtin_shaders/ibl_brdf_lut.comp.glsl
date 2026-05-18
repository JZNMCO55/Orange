#version 450
// ---------------------------------------------------------------------------
// IBL 烘焙路径 c2：BRDF LUT 2D RG16F 预积分。
//
// 烘焙 split-sum 近似的第二项 —— BRDF 在半球的积分，仅依赖 (NoV, roughness)。
// 把 Schlick fresnel `F = F0 + (1-F0)(1-VoH)^5` 拆开后 F0 提到积分外，
// 得到 `Integral = F0 · scale(NoV, r) + bias(NoV, r)`，(scale, bias) 即
// 输出 LUT 的两通道。
//
// 数学参考（Orange-Wiki `techniques/rendering/image-based-lighting.md`
// § 项 2：BRDF 2D LUT）：
//   v = (sqrt(1 - NoV²), 0, NoV)，n = (0, 0, 1)
//   for k in 0..N-1:
//     xi = Hammersley(k, N)
//     h  = ImportanceSampleGGX(xi, n, roughness)
//     l  = reflect(-v, h)
//     NoL = max(l.z, 0), NoH = max(h.z, 0), VoH = max(dot(v, h), 0)
//     if NoL > 0:
//       G   = Smith G2 correlated(NoL, NoV, roughness²)
//       Fc  = (1 - VoH)^5
//       vis = G * VoH * NoL / NoH
//       scale += (1 - Fc) * vis
//       bias  += Fc  * vis
//   scale /= N，bias /= N
//
// 兼容性说明：RG16F (VK_FORMAT_R16G16_SFLOAT) storage image write 在所
// 有 desktop GPU 上事实支持（Filament / UE / glTF reference 都按此走）。
// OrangeRender FormatCoverageTest 当前只显式验过 SampledImage +
// ColorAttachment + TransferDst 三 capability；若未来撞到驱动不支持本
// 格式 storage，需登记 engine-known-gaps + 切 graphics fullscreen
// fragment 路径（同样输出到 RG16F render target，FormatCoverageTest 已
// 验 ColorAttachment 稳定）。
// ---------------------------------------------------------------------------

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rg16f) uniform writeonly image2D uLutOut;

layout(push_constant) uniform PushConstants
{
    uint uLutSize;          // 典型 256
    uint uSampleCount;      // 典型 1024
} pc;

const float kPi = 3.14159265358979323846;

// Van der Corput radical inverse base 2 —— Hammersley low-discrepancy
// 序列的第二维。参考 Holger Dammertz "Hammersley Points on the Hemisphere"。
float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u)  | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u)  | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u)  | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u)  | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;   // 1 / 2^32
}

vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverseVdC(i));
}

// GGX D 的 importance sampling —— 给出半向量 H 的样本，朝向 n。
// 参考 Lumix `ibl_filter.hlsl` `ImportanceSampleGGX`（与 LearnOpenGL /
// Karis 2013 公式一致）。
vec3 ImportanceSampleGGX(vec2 xi, vec3 n, float roughness)
{
    float a = roughness * roughness;

    float phi      = 2.0 * kPi * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    vec3 hTangent;
    hTangent.x = cos(phi) * sinTheta;
    hTangent.y = sin(phi) * sinTheta;
    hTangent.z = cosTheta;

    vec3 up        = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);

    return normalize(tangent * hTangent.x + bitangent * hTangent.y + n * hTangent.z);
}

// Smith G2 correlated（GGX）—— 与 PBR direct shader (`pbr.frag.glsl`)
// 内 G_SmithGGXCorrelated 同款公式；BRDF LUT 烘焙必须用相同 G 项才能
// 跟运行时着色对齐（Karis 2013 + Heitz 2014 一致）。
float SmithG2GGXCorrelated(float NoL, float NoV, float a2)
{
    float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

void main()
{
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;
    if (x >= pc.uLutSize || y >= pc.uLutSize)
    {
        return;
    }

    // texel 中心采样：(x + 0.5) / size ∈ (0, 1]，避开 NoV = 0 的奇异点。
    float NoV       = (float(x) + 0.5) / float(pc.uLutSize);
    float roughness = (float(y) + 0.5) / float(pc.uLutSize);

    // v 沿切平面 (x, 0, z) 排布；n = (0, 0, 1)，shader 内固定。
    vec3 v;
    v.x = sqrt(1.0 - NoV * NoV);
    v.y = 0.0;
    v.z = NoV;

    const vec3 n = vec3(0.0, 0.0, 1.0);

    float a  = roughness * roughness;
    float a2 = a * a;

    float scale = 0.0;
    float bias  = 0.0;

    for (uint k = 0u; k < pc.uSampleCount; ++k)
    {
        vec2 xi = Hammersley(k, pc.uSampleCount);
        vec3 h  = ImportanceSampleGGX(xi, n, roughness);
        vec3 l  = reflect(-v, h);

        float NoL = max(l.z, 0.0);
        float NoH = max(h.z, 0.0);
        float VoH = max(dot(v, h), 0.0);

        if (NoL > 0.0)
        {
            float G   = SmithG2GGXCorrelated(NoL, NoV, a2);
            // PDF 化简后的 visibility 项：G * VoH * NoL / NoH
            float vis = G * VoH * NoL / max(NoH, 1e-5);

            float oneMinusVoH = 1.0 - VoH;
            float Fc = oneMinusVoH * oneMinusVoH;   // (1-VoH)²
            Fc *= Fc;                                // (1-VoH)⁴
            Fc *= oneMinusVoH;                       // (1-VoH)⁵

            scale += (1.0 - Fc) * vis;
            bias  += Fc * vis;
        }
    }
    scale /= float(pc.uSampleCount);
    bias  /= float(pc.uSampleCount);

    imageStore(uLutOut, ivec2(int(x), int(y)), vec4(scale, bias, 0.0, 0.0));
}
