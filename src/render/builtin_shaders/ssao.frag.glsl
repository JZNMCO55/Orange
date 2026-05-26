#version 450

// 内置 SSAO 片元 shader —— Crytek / LearnOpenGL 风格的 view-space 半球
// 采样环境光遮蔽。从 sceneDepth **重建** view-space 位置,法线则采自法线
// 预通道渲出的 normalBuffer(真实几何法线,替代早期的深度差分重建——后者在
// 边缘 / 薄物体 / 接缝处出锯齿与错误遮蔽),再用半球 kernel 采样估遮蔽。
//
//   * 重建 view pos:invProj × (ndc.xy, depth) → 透视除。Vulkan depth 已是
//     [0,1],ndc.z = depth 直接用。用 fullscreen.vert 透传的 vUV 反推
//     ndc.xy = vUV*2-1(与主 pass 同款 y-flip 投影自洽,invProj 抵消)。
//   * 法线:采 normalBuffer(view-space,n*0.5+0.5 编码)解码 + 归一化,
//     强制朝相机(view -z 方向;预通道不翻面)。
//   * kernel:host 端生成的半球样本,经 noise 旋转的 TBN 变到 view 空间,
//     沿表面外推 radius,投回屏幕采深度比较 + range check。
//
// 输出单通道 AO ∈ [0,1](1=无遮蔽)。后续 ssao_apply 模糊 + 乘进 HDR。

// 必须与 C++ Pipeline::Impl::kSsaoKernelSize 一致（否则 UBO std140 布局错位
// → uParams/uParams2 从错误偏移读出垃圾 → kernelSize=0 → AO 恒为 1）。
#define ORANGE_SSAO_MAX_KERNEL 32

layout(set = 0, binding = 0) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 1, std140) uniform SsaoUbo
{
    mat4 uProj;       // view → clip(与主 pass 同款,含 y-flip)
    mat4 uInvProj;    // clip → view
    vec4 uKernel[ORANGE_SSAO_MAX_KERNEL];  // xyz = 半球样本(切线空间)
    vec4 uParams;     // x=radius, y=bias, z=strength, w=power
    vec4 uParams2;    // x=kernelSize, y/z/w 预留
} ssao;
layout(set = 0, binding = 2) uniform sampler2D uNormal;  // view-space 法线(n*0.5+0.5)

// per-pixel 程序化随机旋转角(替代 noise 纹理)：gl_FragCoord hash → 角度。
// 高频噪声由后续 ssao_apply 的 4×4 box 模糊抹平。
float Hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// 由屏幕 uv + sceneDepth 重建 view-space 位置。
vec3 ViewPosFromUV(vec2 uv)
{
    float depth = texture(uSceneDepth, uv).r;
    vec4  ndc   = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4  vp    = ssao.uInvProj * ndc;
    return vp.xyz / vp.w;
}

void main()
{
    float depth = texture(uSceneDepth, vUV).r;
    // 远平面(天空/无几何):depth≈1 → 不算遮蔽,直接全亮。
    if (depth >= 0.9999)
    {
        outColor = vec4(1.0);
        return;
    }

    vec3 P = ViewPosFromUV(vUV);

    // 采 normalBuffer 的 view-space 法线(解码 + 归一化);强制朝向相机
    // (view 空间相机在原点、看 -z,可见面法线应满足 dot(N, -P) > 0)。
    vec3 N = normalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);
    if (dot(N, -P) < 0.0) { N = -N; }

    int   kernelSize = int(ssao.uParams2.x);
    float radius     = ssao.uParams.x;
    float bias       = ssao.uParams.y;

    // per-pixel 随机旋转向量(程序化 hash)→ Gram-Schmidt 构 TBN。
    float ang      = Hash12(gl_FragCoord.xy) * 6.28318530718;
    vec3  randomVec = vec3(cos(ang), sin(ang), 0.0);
    vec3 tangent   = normalize(randomVec - N * dot(randomVec, N));
    vec3 bitangent = cross(N, tangent);
    mat3 TBN       = mat3(tangent, bitangent, N);

    float occlusion = 0.0;
    for (int i = 0; i < kernelSize; ++i)
    {
        // 切线空间半球样本 → view 空间,沿表面外推 radius。
        vec3 samplePos = P + (TBN * ssao.uKernel[i].xyz) * radius;

        // 投回屏幕 uv(用同款 y-flip 投影,与重建自洽)。
        vec4 off = ssao.uProj * vec4(samplePos, 1.0);
        off.xyz /= off.w;
        vec2 sampleUV = off.xy * 0.5 + 0.5;

        // 该屏幕位置真实几何的 view-z。
        float sampleZ = ViewPosFromUV(sampleUV).z;

        // view-z 越大越靠近相机(RH,z 负向远离)。几何比样本点更靠前 → 遮蔽。
        // range check:深度差超 radius 的(远处轮廓)不计,避免 halo。
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(P.z - sampleZ), 1e-4));
        occlusion += (sampleZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - occlusion / float(max(kernelSize, 1));
    ao       = pow(clamp(ao, 0.0, 1.0), ssao.uParams.w);   // power 提对比
    ao       = mix(1.0, ao, ssao.uParams.z);               // strength 整体强度
    outColor = vec4(vec3(ao), 1.0);
}
