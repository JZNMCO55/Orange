#version 450
// ---------------------------------------------------------------------------
// IBL 烘焙路径 c1：HDR equirect → cube map resample。
//
// 每个 invocation 负责输出 cube map 的一个 texel：
//   * gl_GlobalInvocationID.xy = 在 cube face 内的 (x, y) 像素坐标
//   * gl_GlobalInvocationID.z  = face index (0..5)
//
// 步骤：
//   1. (x, y) → face 内 [-1, 1] UV；按 face 朝向得到世界方向向量 d
//   2. equirect 公式 d → (u, v) 在 equirect 2D 上的采样坐标
//   3. textureLod 采样 equirect，imageStore 写 cube map 对应 texel
//
// Face 方向约定（与 Lumix `data/shaders/ibl_filter.hlsl` 一致 +
// Vulkan / D3D 通用 cube 约定）：
//   face 0 = +X：N = ( 1,  -v,  -u)
//   face 1 = -X：N = (-1,  -v,   u)
//   face 2 = +Y：N = ( u,   1,   v)
//   face 3 = -Y：N = ( u,  -1,  -v)
//   face 4 = +Z：N = ( u,  -v,   1)
//   face 5 = -Z：N = (-u,  -v,  -1)
//
// Equirect 公式（参 Orange-Wiki techniques/rendering/environment-mapping.md
// § Latitude-Longitude）：
//   phi   = atan(d.z, d.x)       ∈ [-π, π]
//   theta = asin(d.y)            ∈ [-π/2, π/2]
//   u     = phi  / (2π) + 0.5
//   v     = 0.5 - theta / π        （d.y = +1 → v = 0 即 equirect 顶部 /
//                                    北极，与 stb_image 加载 .hdr 时
//                                    "第一行 = 文件顶部" 约定一致）
//
// 兼容性说明：使用 `writeonly imageCube` 直接 storage write 6 face；
// modern desktop GPU（Nvidia / AMD / Intel Vulkan 后端）均稳定支持，
// 不依赖 `imageCubeArray` feature。binding 与 OrangeRender storage
// image compute 路径对齐（参 `tests/rhi/StorageImageComputeTest.cpp`）。
// ---------------------------------------------------------------------------

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D uEquirectHdr;

// 输出 cube map：6 layer，每 invocation 写一个 texel。format 与 host 侧
// CreateTexture 的 mFormat == RGBA16Float 一致。
layout(set = 0, binding = 1, rgba16f) uniform writeonly imageCube uCubeOut;

// push_constant：cube face 边长（per-face 像素数）。compute shader 用
// 它把 invocation id 归一化到 face 内 [-1, 1]。
layout(push_constant) uniform PushConstants
{
    uint uFaceSize;
} pc;

const float kPi    = 3.14159265358979323846;
const float kInv2Pi = 0.15915494309189533577;   // 1 / (2π)
const float kInvPi  = 0.31830988618379067154;   // 1 / π

// 把 face 内归一化 UV ∈ [-1, 1] + face index → 世界方向向量。
vec3 CubeFaceDir(uint face, vec2 uv)
{
    // uv: (-1, -1) 在 face 左下，(+1, +1) 在 face 右上
    if (face == 0u) return vec3( 1.0, -uv.y, -uv.x);   // +X
    if (face == 1u) return vec3(-1.0, -uv.y,  uv.x);   // -X
    if (face == 2u) return vec3( uv.x,  1.0,  uv.y);   // +Y
    if (face == 3u) return vec3( uv.x, -1.0, -uv.y);   // -Y
    if (face == 4u) return vec3( uv.x, -uv.y,  1.0);   // +Z
    return            vec3(-uv.x, -uv.y, -1.0);        // -Z
}

vec2 EquirectUV(vec3 d)
{
    // d 已 normalized
    float phi   = atan(d.z, d.x);
    float theta = asin(clamp(d.y, -1.0, 1.0));
    float u     = phi   * kInv2Pi + 0.5;
    float v     = 0.5  - theta * kInvPi;
    return vec2(u, v);
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

    // 把整数像素坐标 → face 内 [-1, 1] UV（取 texel 中心 +0.5）
    vec2 uv = (vec2(x, y) + 0.5) / float(pc.uFaceSize);   // [0, 1]
    uv = uv * 2.0 - 1.0;                                  // [-1, 1]

    vec3 dir = normalize(CubeFaceDir(face, uv));
    vec2 equirectUv = EquirectUV(dir);

    vec3 color = textureLod(uEquirectHdr, equirectUv, 0.0).rgb;
    imageStore(uCubeOut, ivec3(int(x), int(y), int(face)), vec4(color, 1.0));
}
