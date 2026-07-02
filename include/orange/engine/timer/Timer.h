#ifndef ORANGE_ENGINE_TIMER_TIMER_H
#define ORANGE_ENGINE_TIMER_TIMER_H

// ---------------------------------------------------------------------------
// Timer —— gameplay 倒计时 / 冷却原语 (CountdownTimer + Cooldown)，header-only。
//
// CountdownTimer：从 duration 倒数，Tick(dt) 返回本次触发次数。one-shot 触发一次即
// 完成；looping 自动补周期 (单次大 dt 可跨多个周期 → 触发次数 >1)。用于计时事件 /
// 周期生成 / buff 到期。Cooldown：技能冷却门，TryTrigger() 就绪则开始冷却并返 true、
// 否则返 false；用于技能 CD / 无敌帧 / 攻击间隔。
//
// 纯 scalar 逻辑，确定性、无依赖 (不引 glm / 标准库)，headless 完全可测。duration<=0
// 视为退化：CountdownTimer::Tick no-op (避免 looping 无限补周期)；Cooldown 恒就绪。
// ---------------------------------------------------------------------------

namespace Orange::Engine::Timer
{

namespace Detail
{
inline float Clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
} // namespace Detail

// 倒计时器。从 duration 秒倒数到 0；one-shot 完成即止，looping 自动补一个周期继续。
class CountdownTimer
{
public:
    CountdownTimer() = default;
    explicit CountdownTimer(float duration, bool looping = false) noexcept
        : mDuration(duration), mRemaining(duration), mLooping(looping)
    {
    }

    // 重置到满 duration 并清完成标志 (重新开始)。
    void Reset() noexcept
    {
        mRemaining = mDuration;
        mFinished = false;
    }

    // 改周期时长。不动当前 remaining (下个周期 / Reset 后生效)。
    void SetDuration(float duration) noexcept { mDuration = duration; }
    void SetLooping(bool looping) noexcept { mLooping = looping; }

    // 立即停 (标记完成、remaining 清零)。one-shot 语义的强制结束。
    void Stop() noexcept
    {
        mRemaining = 0.0f;
        mFinished = true;
    }

    float Duration() const noexcept { return mDuration; }
    float Remaining() const noexcept { return mRemaining > 0.0f ? mRemaining : 0.0f; }
    bool  IsLooping() const noexcept { return mLooping; }
    bool  IsFinished() const noexcept { return mFinished; }
    bool  IsRunning() const noexcept { return !mFinished && mRemaining > 0.0f; }

    // 归一化进度 [0,1]：0 = 刚开始，1 = 本周期完成。duration<=0 → 1。
    float Progress() const noexcept
    {
        if (mDuration <= 0.0f)
        {
            return 1.0f;
        }
        return Detail::Clamp01(1.0f - mRemaining / mDuration);
    }

    // 推进 dt 秒，返回本次触发次数。one-shot：0 或 1 (触发后 IsFinished)；
    // looping：0..N (dt 跨多个周期则 >1)。dt<=0 / 已完成 / duration<=0 → 0。
    int Tick(float dt) noexcept
    {
        if (dt <= 0.0f || mFinished || mDuration <= 0.0f)
        {
            return 0;
        }
        mRemaining -= dt;
        if (!mLooping)
        {
            if (mRemaining <= 0.0f)
            {
                mRemaining = 0.0f;
                mFinished = true;
                return 1;
            }
            return 0;
        }
        // looping：补周期直到 remaining 回正。duration>0 保证有限次。
        int fires = 0;
        while (mRemaining <= 0.0f)
        {
            ++fires;
            mRemaining += mDuration;
        }
        return fires;
    }

private:
    float mDuration = 1.0f;
    float mRemaining = 1.0f;
    bool  mLooping = false;
    bool  mFinished = false;
};

// 冷却门。TryTrigger() 就绪时开始冷却；Tick(dt) 消耗冷却。用于技能 CD / i-frame / 攻击间隔。
class Cooldown
{
public:
    Cooldown() = default;
    explicit Cooldown(float duration) noexcept : mDuration(duration) {}

    void SetDuration(float duration) noexcept { mDuration = duration; }
    float Duration() const noexcept { return mDuration; }

    // 剩余冷却秒 (0 = 就绪)。
    float Remaining() const noexcept { return mRemaining > 0.0f ? mRemaining : 0.0f; }
    bool  IsReady() const noexcept { return mRemaining <= 0.0f; }

    // 就绪比例 [0,1]：0 = 刚触发，1 = 就绪。duration<=0 → 1。
    float FractionReady() const noexcept
    {
        if (mDuration <= 0.0f)
        {
            return 1.0f;
        }
        return Detail::Clamp01(1.0f - mRemaining / mDuration);
    }

    // 尝试触发：就绪则开始冷却并返 true，否则返 false (仍在冷却)。duration<=0 恒就绪。
    bool TryTrigger() noexcept
    {
        if (mRemaining > 0.0f)
        {
            return false;
        }
        mRemaining = mDuration > 0.0f ? mDuration : 0.0f;
        return true;
    }

    // 推进 dt 秒消耗冷却 (钳到 0，不会负)。dt<=0 不动。
    void Tick(float dt) noexcept
    {
        if (dt <= 0.0f)
        {
            return;
        }
        mRemaining -= dt;
        if (mRemaining < 0.0f)
        {
            mRemaining = 0.0f;
        }
    }

    // 强制立即就绪 (清冷却)。
    void ResetReady() noexcept { mRemaining = 0.0f; }

private:
    float mDuration = 1.0f;
    float mRemaining = 0.0f; // 0 = 就绪
};

} // namespace Orange::Engine::Timer

#endif // ORANGE_ENGINE_TIMER_TIMER_H
