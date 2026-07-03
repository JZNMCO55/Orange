#include <orange/engine/noise/Noise.h>

#include <cmath>
#include <cstdint>

namespace Orange::Engine::Noise
{

    namespace
    {

        // 整数格点 hash：把 (xi,yi,seed) 混成一个去相关的 uint32。混合思路——先把两
        // 坐标各乘一个大奇质数常数异或进种子，再走 murmur3 finalizer（xorshift +
        // 乘法两轮），保证相邻格点（xi 差 1）也充分去相关，不留可见网格纹路。
        std::uint32_t HashCoords(int xi, int yi, std::uint32_t seed)
        {
            std::uint32_t h = seed + 0x9E3779B9u;
            h ^= static_cast<std::uint32_t>(xi) * 0x8DA6B343u;
            h ^= static_cast<std::uint32_t>(yi) * 0xD8163841u;
            // murmur3 finalizer（充分雪崩）
            h ^= h >> 16;
            h *= 0x85EBCA6Bu;
            h ^= h >> 13;
            h *= 0xC2B2AE35u;
            h ^= h >> 16;
            return h;
        }

        // 3D 版：多混一维 zi（再取一个大奇质数常数）。
        std::uint32_t HashCoords(int xi, int yi, int zi, std::uint32_t seed)
        {
            std::uint32_t h = seed + 0x9E3779B9u;
            h ^= static_cast<std::uint32_t>(xi) * 0x8DA6B343u;
            h ^= static_cast<std::uint32_t>(yi) * 0xD8163841u;
            h ^= static_cast<std::uint32_t>(zi) * 0xCB1AB31Fu;
            h ^= h >> 16;
            h *= 0x85EBCA6Bu;
            h ^= h >> 13;
            h *= 0xC2B2AE35u;
            h ^= h >> 16;
            return h;
        }

        // hash uint32 → [0,1)（取高 24 bit 当尾数，与 Rng::NextFloat 同款缩放）。
        float HashToUnitFloat(std::uint32_t h)
        {
            return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
        }

        // Perlin 五次缓动 fade(t) = 6t^5 - 15t^4 + 10t^3，t∈[0,1]。一阶 / 二阶导在
        // 端点均为 0，故跨格点插值无可见接缝。
        float Fade(float t)
        {
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }

        float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        // std::floor 后转 int（正确处理负坐标：floor(-1.5)=-2，故 fx=-1.5-(-2)=0.5）。
        int FloorToInt(float v)
        {
            return static_cast<int>(std::floor(v));
        }

        // 2D 梯度：从 8 个固定单位梯度里按 h&7 选一个，返回梯度·(dx,dy)。斜向分量取
        // ±1/√2 使全部梯度单位长，噪声各向近似同性。
        float Grad2D(std::uint32_t h, float dx, float dy)
        {
            constexpr float        kR2         = 0.70710678f; // 1/√2
            static constexpr float kGrad[8][2] = {
                {1.0f, 0.0f},
                {-1.0f, 0.0f},
                {0.0f, 1.0f},
                {0.0f, -1.0f},
                {kR2, kR2},
                {-kR2, kR2},
                {kR2, -kR2},
                {-kR2, -kR2},
            };
            const int g = static_cast<int>(h & 7u);
            return kGrad[g][0] * dx + kGrad[g][1] * dy;
        }

        // 3D 梯度：从 12 个立方体棱中点梯度里按 h%12 选一个，返回梯度·(dx,dy,dz)。
        // 这是 Ken Perlin improved noise 的经典 12 梯度集合。
        float Grad3D(std::uint32_t h, float dx, float dy, float dz)
        {
            static constexpr float kGrad[12][3] = {
                {1.0f, 1.0f, 0.0f},
                {-1.0f, 1.0f, 0.0f},
                {1.0f, -1.0f, 0.0f},
                {-1.0f, -1.0f, 0.0f},
                {1.0f, 0.0f, 1.0f},
                {-1.0f, 0.0f, 1.0f},
                {1.0f, 0.0f, -1.0f},
                {-1.0f, 0.0f, -1.0f},
                {0.0f, 1.0f, 1.0f},
                {0.0f, -1.0f, 1.0f},
                {0.0f, 1.0f, -1.0f},
                {0.0f, -1.0f, -1.0f},
            };
            const int g = static_cast<int>(h % 12u);
            return kGrad[g][0] * dx + kGrad[g][1] * dy + kGrad[g][2] * dz;
        }

    } // namespace

    float ValueNoise2D(float x, float y, std::uint32_t seed)
    {
        const int   xi = FloorToInt(x);
        const int   yi = FloorToInt(y);
        const float fx = x - static_cast<float>(xi);
        const float fy = y - static_cast<float>(yi);

        // 四角格点值（各 [0,1)）。
        const float v00 = HashToUnitFloat(HashCoords(xi, yi, seed));
        const float v10 = HashToUnitFloat(HashCoords(xi + 1, yi, seed));
        const float v01 = HashToUnitFloat(HashCoords(xi, yi + 1, seed));
        const float v11 = HashToUnitFloat(HashCoords(xi + 1, yi + 1, seed));

        const float u = Fade(fx);
        const float v = Fade(fy);

        return Lerp(Lerp(v00, v10, u), Lerp(v01, v11, u), v);
    }

    float PerlinNoise2D(float x, float y, std::uint32_t seed)
    {
        const int   xi = FloorToInt(x);
        const int   yi = FloorToInt(y);
        const float fx = x - static_cast<float>(xi);
        const float fy = y - static_cast<float>(yi);

        // 四角梯度·（该角 → 采样点）偏移。整数格点处偏移全为 0 → 结果恒 0。
        const float n00 = Grad2D(HashCoords(xi, yi, seed), fx, fy);
        const float n10 = Grad2D(HashCoords(xi + 1, yi, seed), fx - 1.0f, fy);
        const float n01 = Grad2D(HashCoords(xi, yi + 1, seed), fx, fy - 1.0f);
        const float n11 = Grad2D(HashCoords(xi + 1, yi + 1, seed), fx - 1.0f, fy - 1.0f);

        const float u = Fade(fx);
        const float v = Fade(fy);

        return Lerp(Lerp(n00, n10, u), Lerp(n01, n11, u), v);
    }

    float PerlinNoise3D(float x, float y, float z, std::uint32_t seed)
    {
        const int   xi = FloorToInt(x);
        const int   yi = FloorToInt(y);
        const int   zi = FloorToInt(z);
        const float fx = x - static_cast<float>(xi);
        const float fy = y - static_cast<float>(yi);
        const float fz = z - static_cast<float>(zi);

        // 立方体 8 角梯度·偏移。角编号 (i,j,k)，i/j/k ∈ {0,1}。
        const float n000 = Grad3D(HashCoords(xi, yi, zi, seed), fx, fy, fz);
        const float n100 = Grad3D(HashCoords(xi + 1, yi, zi, seed), fx - 1.0f, fy, fz);
        const float n010 = Grad3D(HashCoords(xi, yi + 1, zi, seed), fx, fy - 1.0f, fz);
        const float n110 = Grad3D(HashCoords(xi + 1, yi + 1, zi, seed), fx - 1.0f, fy - 1.0f, fz);
        const float n001 = Grad3D(HashCoords(xi, yi, zi + 1, seed), fx, fy, fz - 1.0f);
        const float n101 = Grad3D(HashCoords(xi + 1, yi, zi + 1, seed), fx - 1.0f, fy, fz - 1.0f);
        const float n011 = Grad3D(HashCoords(xi, yi + 1, zi + 1, seed), fx, fy - 1.0f, fz - 1.0f);
        const float n111 = Grad3D(HashCoords(xi + 1, yi + 1, zi + 1, seed), fx - 1.0f, fy - 1.0f, fz - 1.0f);

        const float u = Fade(fx);
        const float v = Fade(fy);
        const float w = Fade(fz);

        // 三线性 fade 插值：先沿 x，再沿 y，最后沿 z。
        const float x00 = Lerp(n000, n100, u);
        const float x10 = Lerp(n010, n110, u);
        const float x01 = Lerp(n001, n101, u);
        const float x11 = Lerp(n011, n111, u);

        const float y0 = Lerp(x00, x10, v);
        const float y1 = Lerp(x01, x11, v);

        return Lerp(y0, y1, w);
    }

    float Fbm2D(float x, float y, int octaves, float lacunarity, float gain, std::uint32_t seed)
    {
        if (octaves < 1)
        {
            octaves = 1;
        }

        float sum      = 0.0f;
        float amp      = 1.0f;
        float freq     = 1.0f;
        float totalAmp = 0.0f;

        for (int i = 0; i < octaves; ++i)
        {
            // 每层换 seed（+i）避免各 octave 图案对齐；频率 *lacunarity 增细节，
            // 振幅 *gain 衰减。
            sum += amp * PerlinNoise2D(x * freq, y * freq, seed + static_cast<std::uint32_t>(i));
            // 按振幅绝对值累加：使 |sum| <= totalAmp 恒成立，任意 gain（含负）下归一化后
            // 输出都不越约 [-1,1]。gain>0（常规衰减）时 amp 恒正，fabs 无影响、行为不变。
            totalAmp += std::fabs(amp);
            freq *= lacunarity;
            amp *= gain;
        }

        // 按累计振幅归一化，使输出范围与单层 Perlin 一致（约 [-1,1]）。
        return totalAmp > 0.0f ? sum / totalAmp : 0.0f;
    }

} // namespace Orange::Engine::Noise
