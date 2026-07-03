#ifndef ORANGE_ENGINE_TWEEN_EASING_H
#define ORANGE_ENGINE_TWEEN_EASING_H

// ---------------------------------------------------------------------------
// Easing —— 缓动函数库（easing functions）。把归一化进度 t∈[0,1] 映射成缓动
// 后的进度，供 Tween 补间 / 动画 / UI 过渡按感知曲线插值（对标 DOTween /
// iTween / easings.net 的标准 31 条曲线）。
//
// 全部 header-only inline 纯函数：无状态、确定性（同输入同输出）、无外部依赖。
// 公式逐条照抄 easings.net 标准定义；常量 c1/c2/c3/c4/c5/n1/d1 同源。
//
// 约定：
//   * 多数缓动端点严格 0→0、1→1；Back / Elastic 中途 overshoot 到 [0,1] 之外
//     （回弹 / 过冲），但端点仍严格 0 和 1。
//   * Ease 内部先把 t clamp 到 [0,1]（越界的进度按端点处理）。
//
// 纯 scalar float —— 只依赖标准库（<cmath> / <cstdint>），不引 glm。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cmath>

namespace Orange::Engine::Tween
{

    // 缓动类型（31 条标准曲线）：Linear + Sine/Quad/Cubic/Quart/Quint/Expo/Circ/
    // Back/Elastic/Bounce 各 In / Out / InOut 三态。顺序与 easings.net 一致。
    enum class EaseType : std::uint8_t
    {
        Linear,
        InSine,
        OutSine,
        InOutSine,
        InQuad,
        OutQuad,
        InOutQuad,
        InCubic,
        OutCubic,
        InOutCubic,
        InQuart,
        OutQuart,
        InOutQuart,
        InQuint,
        OutQuint,
        InOutQuint,
        InExpo,
        OutExpo,
        InOutExpo,
        InCirc,
        OutCirc,
        InOutCirc,
        InBack,
        OutBack,
        InOutBack,
        InElastic,
        OutElastic,
        InOutElastic,
        InBounce,
        OutBounce,
        InOutBounce,
    };

    namespace Detail
    {

        // 圆周率常量（float 精度足够；缓动无需 double）。
        constexpr float kPi = 3.14159265358979323846f;

        // easings.net 标准常量。
        constexpr float kC1 = 1.70158f;            // Back 过冲系数
        constexpr float kC2 = kC1 * 1.525f;        // InOutBack 用
        constexpr float kC3 = kC1 + 1.0f;          // In/OutBack 用
        constexpr float kC4 = (2.0f * kPi) / 3.0f; // In/OutElastic 频率
        constexpr float kC5 = (2.0f * kPi) / 4.5f; // InOutElastic 频率
        constexpr float kN1 = 7.5625f;             // Bounce 抛物段系数
        constexpr float kD1 = 2.75f;               // Bounce 分段阈值

        // OutBounce —— Bounce 家族的基元（In / InOut 均由它复用）。四段抛物线拼接。
        inline float OutBounce(float t)
        {
            if (t < 1.0f / kD1)
            {
                return kN1 * t * t;
            }
            else if (t < 2.0f / kD1)
            {
                t -= 1.5f / kD1;
                return kN1 * t * t + 0.75f;
            }
            else if (t < 2.5f / kD1)
            {
                t -= 2.25f / kD1;
                return kN1 * t * t + 0.9375f;
            }
            else
            {
                t -= 2.625f / kD1;
                return kN1 * t * t + 0.984375f;
            }
        }

    } // namespace Detail

    // 对归一化进度 t∈[0,1] 求缓动值（t 先 clamp 到 [0,1]）。多数缓动 0→0、1→1；
    // Back / Elastic 中途 overshoot 到 [0,1] 之外但端点仍严格 0 和 1。
    inline float Ease(EaseType type, float t)
    {
        // 进度先 clamp：越界的 t 按端点处理，保证端点语义稳定。
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

        using namespace Detail;

        switch (type)
        {
            case EaseType::Linear:
                return t;

            // ----- Sine -----
            case EaseType::InSine:
                return 1.0f - std::cos((t * kPi) / 2.0f);
            case EaseType::OutSine:
                return std::sin((t * kPi) / 2.0f);
            case EaseType::InOutSine:
                return -(std::cos(kPi * t) - 1.0f) / 2.0f;

            // ----- Quad -----
            case EaseType::InQuad:
                return t * t;
            case EaseType::OutQuad:
                return 1.0f - (1.0f - t) * (1.0f - t);
            case EaseType::InOutQuad:
                return t < 0.5f ? 2.0f * t * t
                                : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;

            // ----- Cubic -----
            case EaseType::InCubic:
                return t * t * t;
            case EaseType::OutCubic:
                return 1.0f - std::pow(1.0f - t, 3.0f);
            case EaseType::InOutCubic:
                return t < 0.5f ? 4.0f * t * t * t
                                : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;

            // ----- Quart -----
            case EaseType::InQuart:
                return t * t * t * t;
            case EaseType::OutQuart:
                return 1.0f - std::pow(1.0f - t, 4.0f);
            case EaseType::InOutQuart:
                return t < 0.5f ? 8.0f * t * t * t * t
                                : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;

            // ----- Quint -----
            case EaseType::InQuint:
                return t * t * t * t * t;
            case EaseType::OutQuint:
                return 1.0f - std::pow(1.0f - t, 5.0f);
            case EaseType::InOutQuint:
                return t < 0.5f ? 16.0f * t * t * t * t * t
                                : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;

            // ----- Expo -----
            case EaseType::InExpo:
                return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
            case EaseType::OutExpo:
                return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
            case EaseType::InOutExpo:
                return t == 0.0f   ? 0.0f
                       : t == 1.0f ? 1.0f
                       : t < 0.5f  ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
                                   : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;

            // ----- Circ -----
            case EaseType::InCirc:
                return 1.0f - std::sqrt(1.0f - t * t);
            case EaseType::OutCirc:
                return std::sqrt(1.0f - std::pow(t - 1.0f, 2.0f));
            case EaseType::InOutCirc:
                return t < 0.5f
                           ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f
                           : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;

            // ----- Back（过冲 overshoot）-----
            case EaseType::InBack:
                return kC3 * t * t * t - kC1 * t * t;
            case EaseType::OutBack:
                return 1.0f + kC3 * std::pow(t - 1.0f, 3.0f) + kC1 * std::pow(t - 1.0f, 2.0f);
            case EaseType::InOutBack:
                return t < 0.5f
                           ? (std::pow(2.0f * t, 2.0f) * ((kC2 + 1.0f) * 2.0f * t - kC2)) / 2.0f
                           : (std::pow(2.0f * t - 2.0f, 2.0f) * ((kC2 + 1.0f) * (2.0f * t - 2.0f) + kC2) + 2.0f) / 2.0f;

            // ----- Elastic（回弹 overshoot）-----
            case EaseType::InElastic:
                return t == 0.0f   ? 0.0f
                       : t == 1.0f ? 1.0f
                                   : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((10.0f * t - 10.75f) * kC4);
            case EaseType::OutElastic:
                return t == 0.0f   ? 0.0f
                       : t == 1.0f ? 1.0f
                                   : std::pow(2.0f, -10.0f * t) * std::sin((10.0f * t - 0.75f) * kC4) + 1.0f;
            case EaseType::InOutElastic:
                return t == 0.0f   ? 0.0f
                       : t == 1.0f ? 1.0f
                       : t < 0.5f
                           ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * kC5)) / 2.0f
                           : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * kC5)) / 2.0f + 1.0f;

            // ----- Bounce（弹跳，复用 OutBounce）-----
            case EaseType::InBounce:
                return 1.0f - Detail::OutBounce(1.0f - t);
            case EaseType::OutBounce:
                return Detail::OutBounce(t);
            case EaseType::InOutBounce:
                return t < 0.5f ? (1.0f - Detail::OutBounce(1.0f - 2.0f * t)) / 2.0f
                                : (1.0f + Detail::OutBounce(2.0f * t - 1.0f)) / 2.0f;
        }

        // 不可达（enum 全覆盖）；给编译器一个确定返回值。
        return t;
    }

    // 便捷：a→b 按缓动插值 = a + (b-a)*Ease(type, t)。
    inline float EaseLerp(EaseType type, float a, float b, float t)
    {
        return a + (b - a) * Ease(type, t);
    }

} // namespace Orange::Engine::Tween

#endif // ORANGE_ENGINE_TWEEN_EASING_H
