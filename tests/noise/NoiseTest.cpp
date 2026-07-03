// Noise / Rng 的 headless 单元测试：确定性 seeded PRNG + value / Perlin / fBm
// 噪声。全部纯确定性 scalar 数学，无 World / GPU / GUI。裸 main() + <cassert>。
//
// 覆盖点：
//   * Rng —— 同 seed 同序列 / 不同 seed 不同 / NextFloat 范围 / NextFloatRange /
//     NextInt 范围 + 退化；
//   * ValueNoise2D —— 确定性 / [0,1] 范围 / 不同 seed 不同 / 连续性；
//   * PerlinNoise2D/3D —— 确定性 / 约 [-1,1] / 整数格点 ≈0 / 连续性 / 不同 seed；
//   * Fbm2D —— 确定性 / 约 [-1,1] / octaves=1 退化回 Perlin / octaves 增细节 /
//     octaves<1 当 1。

#include "orange/engine/noise/Noise.h"
#include "orange/engine/noise/Rng.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace Noise = ::Orange::Engine::Noise;

namespace
{

    bool Near(float a, float b, float eps)
    {
        return std::fabs(a - b) < eps;
    }

} // namespace

int main()
{
    // ===== 1. Rng：确定性 —— 同 seed 两个实例产完全相同的序列 =====
    {
        Noise::Rng a{12345u};
        Noise::Rng b{12345u};
        for (int i = 0; i < 64; ++i)
        {
            assert(a.NextU64() == b.NextU64() && "同 seed → 同 U64 序列");
        }
        Noise::Rng c{777u};
        Noise::Rng d{777u};
        for (int i = 0; i < 64; ++i)
        {
            assert(c.NextFloat() == d.NextFloat() && "同 seed → 同 float 序列");
        }
        std::fprintf(stdout, "  [PASS] Rng 确定性（同 seed 同序列）\n");
    }

    // ===== 2. Rng：不同 seed → 序列不同 =====
    {
        Noise::Rng a{1u};
        Noise::Rng b{2u};
        bool       anyDiff = false;
        for (int i = 0; i < 16; ++i)
        {
            if (a.NextU64() != b.NextU64())
            {
                anyDiff = true;
            }
        }
        assert(anyDiff && "不同 seed → 序列应不同");
        std::fprintf(stdout, "  [PASS] Rng 不同 seed 序列不同\n");
    }

    // ===== 3. Rng：范围契约 —— NextFloat / NextFloatRange / NextInt =====
    {
        Noise::Rng r{999u};
        for (int i = 0; i < 4096; ++i)
        {
            const float f = r.NextFloat();
            assert(f >= 0.0f && f < 1.0f && "NextFloat ∈ [0,1)");

            const float fr = r.NextFloatRange(2.0f, 5.0f);
            assert(fr >= 2.0f && fr < 5.0f && "NextFloatRange(2,5) ∈ [2,5)");

            const int n = r.NextInt(3, 7);
            assert(n >= 3 && n <= 7 && "NextInt(3,7) ∈ [3,7]");
        }
        // 退化区间：max<=min → 返回 min。
        assert(r.NextInt(5, 5) == 5 && "NextInt(5,5) 退化返回 5");
        assert(r.NextInt(9, 4) == 9 && "NextInt(max<min) 退化返回 min");
        std::fprintf(stdout, "  [PASS] Rng 范围 + 退化契约\n");
    }

    // ===== 4. ValueNoise2D：确定性 + [0,1] 范围 =====
    {
        for (int i = 0; i < 200; ++i)
        {
            const float x  = static_cast<float>(i) * 0.37f - 20.0f; // 含负坐标
            const float y  = static_cast<float>(i) * -0.53f + 11.0f;
            const float n1 = Noise::ValueNoise2D(x, y, 42u);
            const float n2 = Noise::ValueNoise2D(x, y, 42u);
            assert(n1 == n2 && "ValueNoise2D 确定性（逐位相等）");
            assert(n1 >= 0.0f && n1 <= 1.0f && "ValueNoise2D ∈ [0,1]");
        }
        std::fprintf(stdout, "  [PASS] ValueNoise2D 确定性 + [0,1] 范围\n");
    }

    // ===== 5. ValueNoise2D：不同 seed 输出不同 + 连续性 =====
    {
        bool anyDiff = false;
        for (int i = 0; i < 100; ++i)
        {
            const float x = static_cast<float>(i) * 0.21f;
            const float y = static_cast<float>(i) * 0.13f;
            if (!Near(Noise::ValueNoise2D(x, y, 1u), Noise::ValueNoise2D(x, y, 2u), 1e-6f))
            {
                anyDiff = true;
            }
        }
        assert(anyDiff && "不同 seed → ValueNoise 应不同");

        // 连续性：相邻小 delta 输出变化小（非跳变）。
        for (int i = 0; i < 100; ++i)
        {
            const float x  = static_cast<float>(i) * 0.31f - 5.0f;
            const float y  = static_cast<float>(i) * 0.17f + 3.0f;
            const float n0 = Noise::ValueNoise2D(x, y, 7u);
            const float nx = Noise::ValueNoise2D(x + 0.01f, y, 7u);
            const float ny = Noise::ValueNoise2D(x, y + 0.01f, 7u);
            assert(std::fabs(nx - n0) < 0.2f && "ValueNoise x 方向连续");
            assert(std::fabs(ny - n0) < 0.2f && "ValueNoise y 方向连续");
        }
        std::fprintf(stdout, "  [PASS] ValueNoise2D 不同 seed + 连续性\n");
    }

    // ===== 6. PerlinNoise2D：确定性 + 约 [-1,1] 范围 =====
    {
        for (int i = 0; i < 300; ++i)
        {
            const float x  = static_cast<float>(i) * 0.29f - 30.0f; // 含负坐标
            const float y  = static_cast<float>(i) * 0.41f - 15.0f;
            const float n1 = Noise::PerlinNoise2D(x, y, 3u);
            const float n2 = Noise::PerlinNoise2D(x, y, 3u);
            assert(n1 == n2 && "PerlinNoise2D 确定性");
            assert(n1 >= -1.05f && n1 <= 1.05f && "PerlinNoise2D 约 [-1,1]");
        }
        std::fprintf(stdout, "  [PASS] PerlinNoise2D 确定性 + 范围\n");
    }

    // ===== 7. PerlinNoise2D：整数格点 ≈0（梯度·零偏移）=====
    {
        for (int xi = -5; xi <= 5; ++xi)
        {
            for (int yi = -5; yi <= 5; ++yi)
            {
                const float n = Noise::PerlinNoise2D(static_cast<float>(xi),
                                                     static_cast<float>(yi), 11u);
                assert(std::fabs(n) < 1e-3f && "Perlin 整数格点应 ≈0");
            }
        }
        // 非整数点通常非 0（否则实现退化）。
        assert(std::fabs(Noise::PerlinNoise2D(3.5f, 5.5f, 11u)) > 1e-4f &&
               "Perlin 非整数点应非 0");
        std::fprintf(stdout, "  [PASS] PerlinNoise2D 整数格点 ≈0\n");
    }

    // ===== 8. PerlinNoise2D：连续性 + 不同 seed =====
    {
        for (int i = 0; i < 100; ++i)
        {
            const float x  = static_cast<float>(i) * 0.23f - 4.0f;
            const float y  = static_cast<float>(i) * 0.19f + 2.0f;
            const float n0 = Noise::PerlinNoise2D(x, y, 5u);
            const float nx = Noise::PerlinNoise2D(x + 0.01f, y, 5u);
            const float ny = Noise::PerlinNoise2D(x, y + 0.01f, 5u);
            assert(std::fabs(nx - n0) < 0.2f && "Perlin x 方向连续");
            assert(std::fabs(ny - n0) < 0.2f && "Perlin y 方向连续");
        }
        bool anyDiff = false;
        for (int i = 0; i < 100; ++i)
        {
            const float x = static_cast<float>(i) * 0.33f + 0.5f;
            const float y = static_cast<float>(i) * 0.27f + 0.5f;
            if (!Near(Noise::PerlinNoise2D(x, y, 1u), Noise::PerlinNoise2D(x, y, 2u), 1e-6f))
            {
                anyDiff = true;
            }
        }
        assert(anyDiff && "不同 seed → Perlin 应不同");
        std::fprintf(stdout, "  [PASS] PerlinNoise2D 连续性 + 不同 seed\n");
    }

    // ===== 9. PerlinNoise3D：确定性 + 约 [-1,1] + 整数格点 ≈0 + 连续性 =====
    {
        for (int i = 0; i < 200; ++i)
        {
            const float x  = static_cast<float>(i) * 0.31f - 10.0f;
            const float y  = static_cast<float>(i) * 0.22f - 5.0f;
            const float z  = static_cast<float>(i) * 0.17f + 1.0f;
            const float n1 = Noise::PerlinNoise3D(x, y, z, 4u);
            const float n2 = Noise::PerlinNoise3D(x, y, z, 4u);
            assert(n1 == n2 && "PerlinNoise3D 确定性");
            assert(n1 >= -1.05f && n1 <= 1.05f && "PerlinNoise3D 约 [-1,1]");
        }
        for (int xi = -3; xi <= 3; ++xi)
        {
            for (int yi = -3; yi <= 3; ++yi)
            {
                for (int zi = -3; zi <= 3; ++zi)
                {
                    const float n = Noise::PerlinNoise3D(
                        static_cast<float>(xi), static_cast<float>(yi),
                        static_cast<float>(zi), 4u);
                    assert(std::fabs(n) < 1e-3f && "Perlin3D 整数格点应 ≈0");
                }
            }
        }
        const float p0 = Noise::PerlinNoise3D(2.5f, 3.5f, 4.5f, 4u);
        const float px = Noise::PerlinNoise3D(2.51f, 3.5f, 4.5f, 4u);
        assert(std::fabs(px - p0) < 0.2f && "Perlin3D 连续");
        std::fprintf(stdout, "  [PASS] PerlinNoise3D 确定性 + 范围 + 整数格点 + 连续\n");
    }

    // ===== 10. Fbm2D：确定性 + 约 [-1,1] 范围 =====
    {
        for (int i = 0; i < 200; ++i)
        {
            const float x  = static_cast<float>(i) * 0.27f - 12.0f;
            const float y  = static_cast<float>(i) * 0.19f - 7.0f;
            const float n1 = Noise::Fbm2D(x, y, 4, 2.0f, 0.5f, 8u);
            const float n2 = Noise::Fbm2D(x, y, 4, 2.0f, 0.5f, 8u);
            assert(n1 == n2 && "Fbm2D 确定性");
            assert(n1 >= -1.05f && n1 <= 1.05f && "Fbm2D 约 [-1,1]");
        }
        std::fprintf(stdout, "  [PASS] Fbm2D 确定性 + 范围\n");
    }

    // ===== 11. Fbm2D：octaves=1 ≈ PerlinNoise2D（同 seed，freq=1，归一 totalAmp=1）=====
    {
        for (int i = 0; i < 100; ++i)
        {
            const float x      = static_cast<float>(i) * 0.37f - 8.0f;
            const float y      = static_cast<float>(i) * 0.29f + 4.0f;
            const float fbm    = Noise::Fbm2D(x, y, 1, 2.0f, 0.5f, 13u);
            const float perlin = Noise::PerlinNoise2D(x, y, 13u);
            assert(Near(fbm, perlin, 1e-5f) && "Fbm2D(octaves=1) ≈ PerlinNoise2D");
        }
        std::fprintf(stdout, "  [PASS] Fbm2D octaves=1 退化回 Perlin\n");
    }

    // ===== 12. Fbm2D：octaves<1 当 1；多 octaves 增细节（与单层不同）=====
    {
        // octaves=0 / 负 → 视为 1，等价 octaves=1。
        const float f0 = Noise::Fbm2D(1.3f, 2.7f, 0, 2.0f, 0.5f, 21u);
        const float fn = Noise::Fbm2D(1.3f, 2.7f, -5, 2.0f, 0.5f, 21u);
        const float f1 = Noise::Fbm2D(1.3f, 2.7f, 1, 2.0f, 0.5f, 21u);
        assert(Near(f0, f1, 1e-6f) && "Fbm2D(octaves=0) 当 1");
        assert(Near(fn, f1, 1e-6f) && "Fbm2D(octaves<0) 当 1");

        // 多 octaves 叠加高频细节：至少某些点上 octaves=4 与 octaves=1 输出不同。
        bool anyDiff = false;
        for (int i = 0; i < 100; ++i)
        {
            const float x  = static_cast<float>(i) * 0.41f - 3.0f;
            const float y  = static_cast<float>(i) * 0.23f + 1.0f;
            const float o1 = Noise::Fbm2D(x, y, 1, 2.0f, 0.5f, 21u);
            const float o4 = Noise::Fbm2D(x, y, 4, 2.0f, 0.5f, 21u);
            if (!Near(o1, o4, 1e-4f))
            {
                anyDiff = true;
            }
            assert(o4 >= -1.05f && o4 <= 1.05f && "Fbm2D(octaves=4) 仍在范围内");
        }
        assert(anyDiff && "octaves 增多应引入新细节（与单层不同）");
        std::fprintf(stdout, "  [PASS] Fbm2D octaves<1 当 1 + 多 octaves 增细节\n");
    }

    // NextInt 大/全 int 跨度回归（对抗式复核逮到）：跨度超 INT_MAX（如 INT_MIN..INT_MAX）
    // 时旧实现 int 减法有符号溢出 → span 回绕成 0 → NextU32()%0 除零崩溃。修复后不崩、
    // 结果落在 [min,max]。
    {
        Noise::Rng rng(12345u);
        for (int i = 0; i < 1000; ++i)
        {
            // 满 int 范围：任何 int 都合法，主要验不崩 / 无 UB（旧实现此处除零崩溃）。
            const int r = rng.NextInt(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            (void)r;
        }
        Noise::Rng rng2(777u);
        for (int i = 0; i < 1000; ++i)
        {
            const int r = rng2.NextInt(-2000000000, 2000000000); // 跨度 4e9 > INT_MAX
            assert(r >= -2000000000 && r <= 2000000000 && "大跨度 NextInt 结果在范围内");
        }
        std::fprintf(stdout, "  [PASS] NextInt 大/全 int 跨度不崩、在范围内\n");
    }

    // Fbm2D 负 gain 范围回归（对抗式复核逮到）：负 gain 下带符号 totalAmp 归一化会超
    // [-1,1]；改 fabs 归一化后任意 gain 都不越约 [-1,1]。
    {
        for (int k = 0; k < 200; ++k)
        {
            const float fx = static_cast<float>(k) * 0.137f;
            const float n  = Noise::Fbm2D(fx, fx * 0.5f, 5, 2.0f, -0.5f, 99u);
            assert(n >= -1.05f && n <= 1.05f && "负 gain Fbm2D 仍在约 [-1,1]");
        }
        std::fprintf(stdout, "  [PASS] Fbm2D 负 gain 范围守约\n");
    }

    std::fprintf(stdout, "noise_test: ALL PASS\n");
    return 0;
}
