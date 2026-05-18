#version 450
// ---------------------------------------------------------------------------
// IBL 烘焙路径 c3：irradiance cube map（Lambertian 漫反射 IBL 预积分）。
//
// 数学（Orange-Wiki `techniques/rendering/irradiance-environment-mapping.md`
// § 方法 1：Irradiance Cube Map）：
//   E(n) = ∫_Ω Li(l) (n · l)+ dω
//   PBR Lambertian: Lo = albedo / π · E(n)
//
// 着色端 pbr.frag.glsl 写的是 `iblDiffuse = kDibl * irradiance * baseColor`
// （不再除 π），所以本烘焙输出 **E/π** 而非 E 本身——把 1/π 项 fold 进
// LUT，这是 LearnOpenGL / glTF reference / Filament 通用约定。
//
// 采样策略：cos-weighted Hammersley + Malley's method。
//   PDF p(ω) = cos(θ) / π
//   Monte Carlo: E ≈ (1/N) Σ Li(ωi) cos(θi) / p(ωi)
//                  = (1/N) Σ Li(ωi) · π
//                  = π/N · Σ Li(ωi)
//   E/π ≈ (1/N) · Σ Li(ωi)
//
// 即 cos-weighted 采样 + 累加 + /N 直接得 E/π，无须显式 cos 权重项。
// 收敛快、低分辨率 (32×32) 下 512 样本视觉上足够；高于 1024 边际收益小。
//
// 每个 invocation 输出 cube map 一个 texel：
//   gl_GlobalInvocationID.xy = face 内 (x, y) 像素
//   gl_GlobalInvocationID.z  = face index (0..5)
//
// Cube face 方向约定与 c1 ibl_equirect_to_cube.comp.glsl 一致。
// ---------------------------------------------------------------------------

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform samplerCube uEnvCube;
layout(set = 0, binding = 1, rgba16f) uniform writeonly imageCube uIrradianceOut;

layout(push_constant) uniform PushConstants
{
    uint uFaceSize;     // 典型 32
    uint uSampleCount;  // 典型 512
} pc;

const float kPi = 3.14159265358979323846;

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

// Malley's method —— concentric disk → cos-weighted hemisphere。
// xi.x 控制 r²，xi.y 控制 phi；返回半球切空间下的方向，z 沿法线轴。
vec3 SampleHemisphereCosine(vec2 xi)
{
    float r   = sqrt(xi.x);
    float phi = 2.0 * kPi * xi.y;
    vec3 h;
    h.x = r * cos(phi);
    h.y = r * sin(phi);
    h.z = sqrt(max(0.0, 1.0 - xi.x));
    return h;
}

// 把 face index + face 内 [-1, 1] uv → 世界方向。与 c1 同款。
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
    uint x    = gl_GlobalInvocationID.x;
    uint y    = gl_GlobalInvocationID.y;
    uint face = gl_GlobalInvocationID.z;

    if (x >= pc.uFaceSize || y >= pc.uFaceSize || face >= 6u)
    {
        return;
    }

    vec2 uv = (vec2(x, y) + 0.5) / float(pc.uFaceSize);
    uv = uv * 2.0 - 1.0;

    vec3 n = normalize(CubeFaceDir(face, uv));

    // 构造法线切空间正交基（与 c1 / c2 importance sample 同款）
    vec3 up        = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);

    vec3 irradianceOverPi = vec3(0.0);
    for (uint k = 0u; k < pc.uSampleCount; ++k)
    {
        vec2 xi = Hammersley(k, pc.uSampleCount);
        vec3 hT = SampleHemisphereCosine(xi);
        vec3 l  = normalize(tangent * hT.x + bitangent * hT.y + n * hT.z);

        // cos-weighted MC + Malley → 直接累加 sample，最后 /N 即得 E/π。
        // textureLod mip 0 因为 envCube 当前只有 1 mip（c1 输出未生成 mip
        // 链；后续若要做 trilinear 反走样可在 envCube 生成阶段加 mip）。
        irradianceOverPi += textureLod(uEnvCube, l, 0.0).rgb;
    }
    irradianceOverPi /= float(pc.uSampleCount);

    imageStore(uIrradianceOut, ivec3(int(x), int(y), int(face)),
               vec4(irradianceOverPi, 1.0));
}
