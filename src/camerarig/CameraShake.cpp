#include "orange/engine/camerarig/CameraShake.h"

#include <algorithm>
#include <cmath>

namespace Orange::Engine::CameraRig
{
namespace
{

// 整数 hash → [-1,1]。用几个乘法 + 异或位混合 (Wang/xxHash 风格)，确定性、无状态。
float Hash1D(std::int32_t i, std::uint32_t seed) noexcept
{
    std::uint32_t h = static_cast<std::uint32_t>(i) * 374761393u + seed * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    // [0,1) → [-1,1]。
    return (static_cast<float>(h) / 4294967295.0f) * 2.0f - 1.0f;
}

// 1D 值噪声 (value noise)：整数格点取 hash，格内 smoothstep 插值。返回平滑 [-1,1]。
// 比正弦更"随机"、又比白噪声连续 (相邻帧不跳变)，正合相机抖动。
float ValueNoise1D(float x, std::uint32_t seed) noexcept
{
    const float fi = std::floor(x);
    const std::int32_t i = static_cast<std::int32_t>(fi);
    const float f = x - fi;
    const float u = f * f * (3.0f - 2.0f * f);  // smoothstep
    const float a = Hash1D(i, seed);
    const float b = Hash1D(i + 1, seed);
    return a + (b - a) * u;
}

} // namespace

void CameraShake2D::AddTrauma(float amount) noexcept
{
    if (amount <= 0.0f)
    {
        return;  // 负 / 零：不改 (AddTrauma 只加不减)
    }
    mTrauma = std::clamp(mTrauma + amount, 0.0f, 1.0f);
}

void CameraShake2D::SetTrauma(float trauma) noexcept
{
    mTrauma = std::clamp(trauma, 0.0f, 1.0f);
}

ShakeOffset CameraShake2D::Update(float dt) noexcept
{
    if (dt > 0.0f)
    {
        mTime += dt;
        if (mParams.traumaDecay > 0.0f)
        {
            mTrauma = std::max(0.0f, mTrauma - mParams.traumaDecay * dt);
        }
    }

    ShakeOffset out{};
    if (mTrauma <= 0.0f)
    {
        return out;  // 无 trauma → 零偏移
    }

    // 强度曲线：shake = trauma^exponent (exponent<=0 退化为线性 trauma)。
    const float exponent = (mParams.traumaExponent > 0.0f) ? mParams.traumaExponent : 1.0f;
    const float shake = std::pow(mTrauma, exponent);

    // 值噪声相位：累计时间 × 频率。per-axis / roll 用不同 seed 偏移 → 三路不同步。
    const float phase = mTime * mParams.frequency;
    out.translation.x = mParams.maxTranslation.x * shake * ValueNoise1D(phase, mParams.seed + 0u);
    out.translation.y = mParams.maxTranslation.y * shake * ValueNoise1D(phase, mParams.seed + 1u);
    out.roll          = mParams.maxRoll          * shake * ValueNoise1D(phase, mParams.seed + 2u);
    return out;
}

} // namespace Orange::Engine::CameraRig
