#ifndef ORANGE_ENGINE_CORE_TIME_H
#define ORANGE_ENGINE_CORE_TIME_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 04 — Core::Time
//
// Pure data types only. No clock backend lives here — Platform::Clock owns
// the high-resolution wall clock and feeds these types from the App main
// loop (see Phase 1 / Task 06 for the Window/Clock backend, Task 08 for the
// loop wiring).
//
// `DeltaSeconds` is float, not double: gameplay code overwhelmingly wants
// 32-bit math, and a frame's worth of seconds (~16.6 ms) sits well inside
// float precision. Where high-precision accumulation matters, accumulate
// in fixed-step ticks instead of in seconds.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace Orange::Engine
{

using DeltaSeconds = float;
using FrameIndex = std::uint64_t;

struct TimeStamp
{
    DeltaSeconds deltaSeconds{0.0f};
    DeltaSeconds totalSeconds{0.0f};
    FrameIndex   frameIndex{0};
};

// FixedStepAccumulator drives deterministic update loops (physics, fixed
// gameplay tick). Pattern:
//
//   accumulator.Add(frame.deltaSeconds);
//   while (accumulator.ShouldStep())
//   {
//       physicsWorld.Step(accumulator.StepSeconds());
//       accumulator.ConsumeStep();
//   }
//
// The accumulator is intentionally NOT capped here — clamping the maximum
// number of steps per frame is a policy decision that belongs to the
// caller (e.g. Physics module's "spiral of death" guard).
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

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_CORE_TIME_H
