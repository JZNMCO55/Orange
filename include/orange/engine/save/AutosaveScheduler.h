#ifndef ORANGE_ENGINE_SAVE_AUTOSAVE_SCHEDULER_H
#define ORANGE_ENGINE_SAVE_AUTOSAVE_SCHEDULER_H

// ---------------------------------------------------------------------------
// AutosaveScheduler —— 帧驱动的"按时间间隔触发自动存档" + 节流 + 手动
// 请求队列。
//
// 引擎刻意不绑定到任何具体的存档实现：
//   * 触发时只调用 game 提供的 callback，game 自己决定怎么落盘（典型
//     场景是调 `SlotManager::Save` 写 "autosave" 槽）；
//   * 调度本身是纯时间逻辑，无 IO，可在主线程的 update tick 里同步调用；
//   * Game 自己负责每帧调 `Update(deltaSeconds)`；调度不读系统时钟，避
//     免暂停游戏 / 时间缩放等场景下行为诡异。
//
// 设计要点：
//   * **interval**：定时触发周期，单位秒；0 = 关闭定时触发（仅响应
//     RequestAutosave）。
//   * **throttle**：两次触发之间的最小间隔；防止 RequestAutosave 在短
//     时间内连发把磁盘写爆，也防止"定时触发" 与"手动请求"在同一帧叠加
//     成两次写入。
//   * **PendingRequest**：RequestAutosave 在 throttle 窗口内被调用时不
//     丢弃——挂起，等下一次 Update 时若 throttle 解除则立刻触发；多次
//     RequestAutosave 等价于一次（latest wins，不累计）。
//   * **Reset()**：手动存档完成时由 game 调，把"距上次触发的累计时间"
//     清零，避免手动存盘后立刻又被 autosave 一次。
//
// 线程模型：单线程拥有，Update / RequestAutosave / Reset / 查询都假设
// 在同一线程串行调用。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <functional>

namespace Orange::Engine::Save
{

    class ORANGE_ENGINE_API AutosaveScheduler
    {
    public:
        // 触发回调签名 —— 无参无返回。Game 自己捕获所需的 SlotManager /
        // World / 当前 slot 名等 context。回调内抛异常**不**被 scheduler
        // 捕获——交给上层处理器决定。
        using TriggerCallback = std::function<void()>;

        struct Config
        {
            // 定时触发周期（秒）。0 = 关闭定时触发，仅靠 RequestAutosave
            // 显式驱动。
            double intervalSeconds{300.0};

            // 两次触发之间的最小间隔（秒）。无论是定时还是手动 request 触
            // 发，距上一次成功触发不到这个时间都会被推迟。0 = 无节流。
            double minSecondsBetween{30.0};
        };

        AutosaveScheduler(Config config, TriggerCallback callback) noexcept;

        AutosaveScheduler(const AutosaveScheduler&)            = delete;
        AutosaveScheduler& operator=(const AutosaveScheduler&) = delete;

        // 推进调度器；触发条件满足时同步调用 callback。
        //
        // 触发顺序（同 Update 内多源同时满足时仅触发一次）：
        //   1) 若距上一次触发 ≥ minSecondsBetween：
        //      a) 定时：intervalSeconds > 0 且累计时间 ≥ intervalSeconds → 触发
        //      b) 手动 pending request → 触发
        // 触发后清零累计时间 + 清 pending flag。
        //
        // deltaSeconds < 0 视为 0；deltaSeconds 极大值（如游戏挂起后恢复）
        // 不会让 callback 连发——同一 Update 最多触发一次。
        void Update(double deltaSeconds);

        // 请求一次自动存档。throttle 未解除时挂起；下次 Update 时若 throttle
        // 已过则立刻触发。多次调用等价于一次（不累计）。
        void RequestAutosave() noexcept;

        // 清零"距上次触发的累计时间" + 清 pending flag。典型场景：玩家
        // 手动存档完成后调一下，避免手动存档后立刻再 autosave。
        void Reset() noexcept;

        // 距离上次触发已经累计了多少秒。Game UI 可以据此显示"距离下次自动
        // 存档还剩 X 秒"。从未触发过则为最近一次 Reset 以来的累计；构造后
        // 从 0 开始。
        double SecondsSinceLastFire() const noexcept { return mElapsedSinceLastFire; }

        // 是否有挂起的手动 RequestAutosave 等待 throttle 解除。
        bool HasPendingRequest() const noexcept { return mPendingRequest; }

        const Config& GetConfig() const noexcept { return mConfig; }

    private:
        Config          mConfig;
        TriggerCallback mCallback;
        double          mElapsedSinceLastFire{0.0};
        bool            mPendingRequest{false};
    };

} // namespace Orange::Engine::Save

#endif // ORANGE_ENGINE_SAVE_AUTOSAVE_SCHEDULER_H
