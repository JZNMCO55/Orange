// AutosaveScheduler 实现 —— 纯时间逻辑，无 IO，由 game 在主循环里
// 每帧调 Update 驱动。

#include "orange/engine/save/AutosaveScheduler.h"

#include <utility>

namespace Orange::Engine::Save
{

    AutosaveScheduler::AutosaveScheduler(Config config, TriggerCallback callback) noexcept
        : mConfig(config), mCallback(std::move(callback))
    {
    }

    void AutosaveScheduler::Update(double deltaSeconds)
    {
        if (deltaSeconds < 0.0)
        {
            deltaSeconds = 0.0;
        }
        mElapsedSinceLastFire += deltaSeconds;

        // 检查是否在 throttle 窗口内。0 节流意味着任何时候都允许触发。
        const bool throttleClear = (mConfig.minSecondsBetween <= 0.0) || (mElapsedSinceLastFire >= mConfig.minSecondsBetween);
        if (!throttleClear)
        {
            return;
        }

        // 触发条件：定时（interval > 0 且累计时间够）或挂起的手动请求。
        const bool intervalDue = (mConfig.intervalSeconds > 0.0) && (mElapsedSinceLastFire >= mConfig.intervalSeconds);
        const bool requestDue  = mPendingRequest;

        if (!intervalDue && !requestDue)
        {
            return;
        }

        // 触发 —— 先清状态再调 callback，避免 callback 内部触发 RequestAutosave
        // 导致 pending flag 被错误清掉。
        mElapsedSinceLastFire = 0.0;
        mPendingRequest       = false;

        if (mCallback)
        {
            mCallback();
        }
    }

    void AutosaveScheduler::RequestAutosave() noexcept
    {
        mPendingRequest = true;
    }

    void AutosaveScheduler::Reset() noexcept
    {
        mElapsedSinceLastFire = 0.0;
        mPendingRequest       = false;
    }

} // namespace Orange::Engine::Save
