// shadow_pcf.glsl.inc —— PCF 软阴影采样辅助函数。
//
// 通过 glslangValidator 的 `-I src/render/builtin_shaders` include path
// 在编译期被 #include 进各 fragment shader（toon / rim_light / 游戏侧
// 自定义 shader）。**不是独立 ShaderAsset**——这是一个源代码片段，所
// 有展开都在编译期，运行时仍然只加载完整 .spv。
//
// 调用约定：
//   * 调用方提供 sampler2D shadowMap（或 sampler2DShadow 也兼容；这里
//     按 sampler2D 写，让调用方自己决定 compare-mode）；
//   * lightViewProj 是 light view * proj，把 worldPos 投到 NDC；
//   * kernelRadius = 0 / 1 / 2，对应 1×1 / 3×3 / 5×5 box filter；
//   * depthBias 抑制 acne，调用方按 ShadowConfig.depthBias 喂。
//
// 返回值：[0, 1] 区间的阴影系数 —— 1 = 完全照亮，0 = 完全阴影。乘到
// 主光的 NdotL 项上即可。
//
// 边界处理：worldPos 投影出 [0, 1] uv 范围外（光锥外）按"完全照亮"处
// 理（return 1.0），与 OpenGL 的 GL_CLAMP_TO_BORDER + 1.0 边色等价。

#ifndef ORANGE_ENGINE_BUILTIN_SHADER_SHADOW_PCF_INC
#define ORANGE_ENGINE_BUILTIN_SHADER_SHADOW_PCF_INC

float SamplePcfShadow(sampler2D shadowMap,
                      vec3      worldPos,
                      mat4      lightViewProj,
                      int       kernelRadius,
                      float     depthBias)
{
    vec4 lightClip = lightViewProj * vec4(worldPos, 1.0);
    vec3 ndc       = lightClip.xyz / lightClip.w;
    // Vulkan NDC: x/y ∈ [-1,1] → uv ∈ [0,1]; z ∈ [0,1] 不需要 remap
    vec2 uv        = ndc.xy * 0.5 + 0.5;

    // 光锥外 → 完全照亮
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0)
    {
        return 1.0;
    }

    float currentDepth = ndc.z - depthBias;
    vec2  texel        = 1.0 / vec2(textureSize(shadowMap, 0));

    float shadow = 0.0;
    int   count  = 0;
    for (int x = -kernelRadius; x <= kernelRadius; ++x)
    {
        for (int y = -kernelRadius; y <= kernelRadius; ++y)
        {
            vec2  offset = vec2(float(x), float(y)) * texel;
            float closestDepth = texture(shadowMap, uv + offset).r;
            shadow += (currentDepth <= closestDepth) ? 1.0 : 0.0;
            ++count;
        }
    }

    return shadow / float(count);
}

// sampler2DArray 变体（GAP-2026-05-26 G2）：多 shadow caster 各占一层，
// 调用方按 shadow index 选 layer。语义与 SamplePcfShadow 一致——光锥外
// （uv 越界 / z>1）按完全照亮返回 1.0。
float SamplePcfShadowArray(sampler2DArray shadowArray,
                           int            layer,
                           vec3           worldPos,
                           mat4           lightViewProj,
                           int            kernelRadius,
                           float          depthBias)
{
    vec4 lightClip = lightViewProj * vec4(worldPos, 1.0);
    vec3 ndc       = lightClip.xyz / lightClip.w;
    vec2 uv        = ndc.xy * 0.5 + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0)
    {
        return 1.0;
    }

    float currentDepth = ndc.z - depthBias;
    vec2  texel        = 1.0 / vec2(textureSize(shadowArray, 0).xy);

    float shadow = 0.0;
    int   count  = 0;
    for (int x = -kernelRadius; x <= kernelRadius; ++x)
    {
        for (int y = -kernelRadius; y <= kernelRadius; ++y)
        {
            vec2  offset = vec2(float(x), float(y)) * texel;
            float closestDepth = texture(shadowArray, vec3(uv + offset, float(layer))).r;
            shadow += (currentDepth <= closestDepth) ? 1.0 : 0.0;
            ++count;
        }
    }

    return shadow / float(count);
}

// ---------------------------------------------------------------------------
// PCSS（percentage-closer soft shadows）—— 可变半影软阴影。
//
// 三步：(1) blocker search 在 lightSize 比例的区域里找比 receiver 更近的
// 遮挡 texel，求平均遮挡深度；(2) penumbra 估计 = (receiver - avgBlocker) /
// avgBlocker，受影体离遮挡面越远半影越宽（接触处硬、远处软）；(3) 可变半径
// PCF，filter 半径 = clamp(penumbra * lightSize, ..., lightSize)。
//
// lightSize（shadow map texel 单位）<= 0 时直接退回固定半径 PCF（默认行为
// 不变）。采样数固定（search / PCF 各 5×5），偏移按半径缩放——避免动态 loop
// 次数，GPU 友好。penumbra 用 NDC 深度比例近似（directional ortho 下精确，
// spot perspective 下定性正确，stylized 够用）。
// ---------------------------------------------------------------------------

float SamplePcssShadow(sampler2D shadowMap,
                       vec3      worldPos,
                       mat4      lightViewProj,
                       int       maxKernelRadius,
                       float     depthBias,
                       float     lightSize)
{
    if (lightSize <= 0.0)
    {
        return SamplePcfShadow(shadowMap, worldPos, lightViewProj, maxKernelRadius, depthBias);
    }

    vec4 lightClip = lightViewProj * vec4(worldPos, 1.0);
    vec3 ndc       = lightClip.xyz / lightClip.w;
    vec2 uv        = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0)
    {
        return 1.0;
    }

    float receiver = ndc.z;
    vec2  texel    = 1.0 / vec2(textureSize(shadowMap, 0));

    // 1. blocker search
    float blockerSum = 0.0;
    float blockerCnt = 0.0;
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            vec2  off = vec2(float(x), float(y)) * texel * lightSize;
            float d   = texture(shadowMap, uv + off).r;
            if (d < receiver - depthBias)
            {
                blockerSum += d;
                blockerCnt += 1.0;
            }
        }
    }
    if (blockerCnt < 0.5)
    {
        return 1.0;   // 无遮挡 → 全亮
    }
    float avgBlocker = blockerSum / blockerCnt;

    // 2. penumbra 估计 → filter 半径（texel 单位，上限 lightSize）
    float penumbra     = (receiver - avgBlocker) / max(avgBlocker, 1e-4);
    float filterRadius = clamp(penumbra * lightSize, 0.5, lightSize);

    // 3. 可变半径 PCF
    float curDepth = receiver - depthBias;
    float shadow   = 0.0;
    float cnt      = 0.0;
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            vec2  off     = (vec2(float(x), float(y)) / 2.0) * filterRadius * texel;
            float closest = texture(shadowMap, uv + off).r;
            shadow += (curDepth <= closest) ? 1.0 : 0.0;
            cnt    += 1.0;
        }
    }
    return shadow / cnt;
}

// sampler2DArray 变体（spot 多 caster 各占一层）。语义与 SamplePcssShadow
// 一致，lightSize <= 0 退回 SamplePcfShadowArray。
float SamplePcssShadowArray(sampler2DArray shadowArray,
                            int            layer,
                            vec3           worldPos,
                            mat4           lightViewProj,
                            int            maxKernelRadius,
                            float          depthBias,
                            float          lightSize)
{
    if (lightSize <= 0.0)
    {
        return SamplePcfShadowArray(shadowArray, layer, worldPos, lightViewProj,
                                    maxKernelRadius, depthBias);
    }

    vec4 lightClip = lightViewProj * vec4(worldPos, 1.0);
    vec3 ndc       = lightClip.xyz / lightClip.w;
    vec2 uv        = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0)
    {
        return 1.0;
    }

    float receiver = ndc.z;
    vec2  texel    = 1.0 / vec2(textureSize(shadowArray, 0).xy);

    float blockerSum = 0.0;
    float blockerCnt = 0.0;
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            vec2  off = vec2(float(x), float(y)) * texel * lightSize;
            float d   = texture(shadowArray, vec3(uv + off, float(layer))).r;
            if (d < receiver - depthBias)
            {
                blockerSum += d;
                blockerCnt += 1.0;
            }
        }
    }
    if (blockerCnt < 0.5)
    {
        return 1.0;
    }
    float avgBlocker   = blockerSum / blockerCnt;
    float penumbra     = (receiver - avgBlocker) / max(avgBlocker, 1e-4);
    float filterRadius = clamp(penumbra * lightSize, 0.5, lightSize);

    float curDepth = receiver - depthBias;
    float shadow   = 0.0;
    float cnt      = 0.0;
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            vec2  off     = (vec2(float(x), float(y)) / 2.0) * filterRadius * texel;
            float closest = texture(shadowArray, vec3(uv + off, float(layer))).r;
            shadow += (curDepth <= closest) ? 1.0 : 0.0;
            cnt    += 1.0;
        }
    }
    return shadow / cnt;
}

#endif  // ORANGE_ENGINE_BUILTIN_SHADER_SHADOW_PCF_INC
