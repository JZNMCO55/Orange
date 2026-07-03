#ifndef ORANGE_ENGINE_NOISE_NOISE_H
#define ORANGE_ENGINE_NOISE_NOISE_H

// ---------------------------------------------------------------------------
// Noise —— 程序化生成用的确定性噪声函数（value / Perlin gradient / fBm）。
//
// 全部确定性：同 (坐标, seed) → 同输出，可复现（seed 换了图案换）。是程序化
// 关卡 / 地形高度场 / 摆位扰动的数学基础，与 tilemap 协同（如按噪声阈值决定
// tile 实心 / 空）。纯 scalar float —— 公共头只依赖标准库，不引 glm。
//
// 约定：
//   * ValueNoise2D 返回 [0,1]；
//   * PerlinNoise2D / PerlinNoise3D 返回约 [-1,1]（整数格点恒为 0）；
//   * Fbm2D 叠加多层 Perlin 后按总振幅归一化，返回约 [-1,1]。
//
// 算法在 Noise.cpp（file-local hash / fade / gradient 均不暴露）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>

namespace Orange::Engine::Noise
{

    // value noise（2D）：hash 格点值 + 五次 fade 双线性插值。返回 [0,1]。
    ORANGE_ENGINE_API float ValueNoise2D(float x, float y, std::uint32_t seed = 0);

    // Perlin gradient noise（2D/3D）：hash 格点梯度 + fade 插值。返回约 [-1,1]。
    ORANGE_ENGINE_API float PerlinNoise2D(float x, float y, std::uint32_t seed = 0);
    ORANGE_ENGINE_API float PerlinNoise3D(float x, float y, float z, std::uint32_t seed = 0);

    // fBm（fractal Brownian motion，2D）：叠加 octaves 层 Perlin，每层频率
    // *lacunarity、振幅 *gain，按总振幅归一化。返回约 [-1,1]。octaves<1 视为 1。
    ORANGE_ENGINE_API float Fbm2D(float x, float y, int octaves, float lacunarity, float gain,
                                  std::uint32_t seed = 0);

} // namespace Orange::Engine::Noise

#endif // ORANGE_ENGINE_NOISE_NOISE_H
