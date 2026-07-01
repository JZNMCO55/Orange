#ifndef ORANGE_ENGINE_NOISE_RNG_H
#define ORANGE_ENGINE_NOISE_RNG_H

// ---------------------------------------------------------------------------
// Rng —— 确定性 seeded PRNG（splitmix64 算法），header-only。
//
// 同 seed → 同序列（可复现），是程序化关卡 / 地形 / 摆位的随机源。splitmix64
// 简单、状态只有一个 64-bit word、雪崩性够游戏用（不做密码学）。helper 全为
// inline，故不标 ORANGE_ENGINE_API。公共头只依赖标准库。
//
// 线程不安全：每线程 / 每系统各持一个实例，不要跨线程共享同一个 Rng。
// ---------------------------------------------------------------------------

#include <cstdint>

namespace Orange::Engine::Noise
{

// 确定性伪随机数生成器 (seeded PRNG)，splitmix64 算法。同 seed → 同序列。
struct Rng
{
    std::uint64_t state{0};

    explicit Rng(std::uint64_t seed = 0) noexcept : state(seed) {}

    // splitmix64 step：产 64-bit 均匀随机。
    std::uint64_t NextU64() noexcept
    {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint32_t NextU32() noexcept { return static_cast<std::uint32_t>(NextU64() >> 32); }

    // [0,1) 单精度浮点（24-bit 尾数）。
    float NextFloat() noexcept
    {
        return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f);
    }

    // [minInclusive, maxExclusive) 浮点。
    float NextFloatRange(float minInclusive, float maxExclusive) noexcept
    {
        return minInclusive + NextFloat() * (maxExclusive - minInclusive);
    }

    // [minInclusive, maxInclusive] 整数（modulo，游戏可接受的轻微偏置；
    // range<=0 返回 min）。
    int NextInt(int minInclusive, int maxInclusive) noexcept
    {
        if (maxInclusive <= minInclusive)
        {
            return minInclusive;
        }
        // 用 64 位算跨度：避免 int 减法在超 INT_MAX 跨度（如 NextInt(INT_MIN,INT_MAX)）
        // 时有符号溢出 UB + span 回绕成 0 导致 NextU32()%0 除零崩溃。span∈[1, 2^32]。
        const std::uint64_t span = static_cast<std::uint64_t>(static_cast<std::int64_t>(maxInclusive) -
                                                              static_cast<std::int64_t>(minInclusive)) +
                                   1ull;
        return static_cast<int>(static_cast<std::int64_t>(minInclusive) +
                                static_cast<std::int64_t>(NextU64() % span));
    }
};

}  // namespace Orange::Engine::Noise

#endif  // ORANGE_ENGINE_NOISE_RNG_H
