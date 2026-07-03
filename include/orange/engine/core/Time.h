#ifndef ORANGE_ENGINE_CORE_TIME_H
#define ORANGE_ENGINE_CORE_TIME_H

// ---------------------------------------------------------------------------
// Core::Time —— 仅有数据类型，不实现时钟后端。
//
// 真正的高精度 wall clock 由 Platform::Clock 拥有，再由 App 主循环把测量
// 值喂进这里的 TimeStamp / FixedStepAccumulator。
//
// `DeltaSeconds` 选 float 而非 double：gameplay 代码绝大多数情况下使用
// 32 位浮点数学，而一帧时长（约 16.6 ms）远在 float 精度的安全范围内。
// 若某处确实需要高精度积分，应改在 fixed-step ticks 上累加，而不是在
// seconds 上累加。
// ---------------------------------------------------------------------------

#include <cstdint>

namespace Orange::Engine
{

    using DeltaSeconds = float;
    using FrameIndex   = std::uint64_t;

    struct TimeStamp
    {
        DeltaSeconds deltaSeconds{0.0f};
        DeltaSeconds totalSeconds{0.0f};
        FrameIndex   frameIndex{0};
    };

    // FixedStepAccumulator 用于推动确定性的更新循环（如 physics、固定 tick
    // gameplay）。典型用法：
    //
    //   accumulator.Add(frame.deltaSeconds);
    //   while (accumulator.ShouldStep())
    //   {
    //       physicsWorld.Step(accumulator.StepSeconds());
    //       accumulator.ConsumeStep();
    //   }
    //
    // 这里刻意不对单帧最大步数设上限——所谓 "spiral of death" 的限制是策略
    // 决定，应由调用方（如 Physics 模块）持有。
    class FixedStepAccumulator
    {
    public:
        explicit constexpr FixedStepAccumulator(DeltaSeconds stepSeconds) noexcept
            : mStep(stepSeconds)
        {
        }

        constexpr void Add(DeltaSeconds delta) noexcept { mAccum += delta; }

        constexpr bool ShouldStep() const noexcept { return mAccum >= mStep; }

        constexpr void ConsumeStep() noexcept { mAccum -= mStep; }

        constexpr void Reset() noexcept { mAccum = 0.0f; }

        constexpr DeltaSeconds StepSeconds() const noexcept { return mStep; }
        constexpr DeltaSeconds Accumulated() const noexcept { return mAccum; }

        constexpr float InterpolationAlpha() const noexcept
        {
            return (mStep > 0.0f) ? (mAccum / mStep) : 0.0f;
        }

    private:
        DeltaSeconds mStep;
        DeltaSeconds mAccum{0.0f};
    };

} // namespace Orange::Engine

#endif // ORANGE_ENGINE_CORE_TIME_H
