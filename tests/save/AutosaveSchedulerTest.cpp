// AutosaveScheduler 单元测试 —— Phase 5.5 / Task 04。
//
// 覆盖：
//   1. 初始状态：未触发；SecondsSinceLastFire 起步为 0
//   2. 定时触发：累计时间到 interval 即触发；触发后清零
//   3. 节流：interval 满足但 throttle 未到时不触发
//   4. RequestAutosave：throttle 已过 → 立刻触发
//   5. RequestAutosave：throttle 未到 → 挂起；多次 request 等价于一次
//   6. Reset() 清掉累计时间 + pending
//   7. interval = 0 关闭定时；只有 RequestAutosave 能触发
//   8. deltaSeconds < 0 / 极大值 → 不会让 callback 在同一 Update 多次触发

#include <orange/engine/save/AutosaveScheduler.h>

#include <cassert>
#include <cstdio>

using Orange::Engine::Save::AutosaveScheduler;

namespace
{

    // 简单的"被调多少次"计数器 callback。
    struct FireCounter
    {
        int  count{0};
        void operator()() { ++count; }
    };

    // 1. 初始状态
    void TestInitialState()
    {
        int               fired = 0;
        AutosaveScheduler s({/*interval*/ 100.0, /*minBetween*/ 10.0},
                            [&]
                            { ++fired; });
        assert(s.SecondsSinceLastFire() == 0.0);
        assert(!s.HasPendingRequest());
        assert(fired == 0);
    }

    // 2. 定时触发：累计到 interval 即触发，触发后清零
    void TestIntervalFires()
    {
        int               fired = 0;
        AutosaveScheduler s({/*interval*/ 5.0, /*minBetween*/ 0.0},
                            [&]
                            { ++fired; });

        s.Update(2.0);
        assert(fired == 0);
        s.Update(2.0);
        assert(fired == 0);
        s.Update(2.0); // 累计 6 ≥ 5 → 触发
        assert(fired == 1);
        assert(s.SecondsSinceLastFire() == 0.0); // 触发后清零

        s.Update(4.99);
        assert(fired == 1); // 还差一点
        s.Update(0.02);
        assert(fired == 2);
    }

    // 3. 节流：interval 满足但 throttle 未到时不触发
    void TestThrottleBlocksInterval()
    {
        int               fired = 0;
        AutosaveScheduler s({/*interval*/ 1.0, /*minBetween*/ 5.0},
                            [&]
                            { ++fired; });

        // interval 1s 但 throttle 5s —— throttle 起决定作用
        s.Update(2.0);
        assert(fired == 0);
        s.Update(2.0);
        assert(fired == 0);
        s.Update(0.99);
        assert(fired == 0); // 累计 4.99 < 5
        s.Update(0.02);
        assert(fired == 1); // 累计 5.01 ≥ 5
    }

    // 4. RequestAutosave：throttle 已过 → 立刻在下一 Update 触发
    void TestRequestAutosaveImmediate()
    {
        int               fired = 0;
        AutosaveScheduler s({/*interval*/ 1000.0, /*minBetween*/ 1.0},
                            [&]
                            { ++fired; });

        s.Update(2.0); // throttle 已过，但没有 pending
        assert(fired == 0);

        s.RequestAutosave();
        assert(s.HasPendingRequest());
        s.Update(0.0); // 累计仍 ≥ throttle → pending 触发
        assert(fired == 1);
        assert(!s.HasPendingRequest());
    }

    // 5. RequestAutosave 在 throttle 内挂起；多次 request 等价于一次
    void TestRequestAutosavePending()
    {
        int               fired = 0;
        AutosaveScheduler s({/*interval*/ 1000.0, /*minBetween*/ 5.0},
                            [&]
                            { ++fired; });

        // 先用一次 RequestAutosave + Update 触发一次（throttle 起步是 0，
        // 距上次触发的累计 5 才允许触发；初始累计 0 → 需要先 Update 5s）
        s.Update(5.0);
        s.RequestAutosave();
        s.Update(0.0);
        assert(fired == 1);

        // 立刻再 request 多次 —— throttle 0 < 5，全部挂起为单一 pending
        s.RequestAutosave();
        s.RequestAutosave();
        s.RequestAutosave();
        s.Update(1.0);
        assert(fired == 1); // throttle 仍未到
        assert(s.HasPendingRequest());

        s.Update(4.0); // 累计 5 → 触发 pending（仅一次）
        assert(fired == 2);
        assert(!s.HasPendingRequest());
    }

    // 6. Reset()
    void TestReset()
    {
        int               fired = 0;
        AutosaveScheduler s({/*interval*/ 5.0, /*minBetween*/ 1.0},
                            [&]
                            { ++fired; });

        s.Update(4.0);
        s.RequestAutosave();
        s.Reset();
        assert(s.SecondsSinceLastFire() == 0.0);
        assert(!s.HasPendingRequest());

        s.Update(4.0);
        assert(fired == 0); // 之前的 pending 已被 Reset 清掉
        s.Update(2.0);      // 累计 6 ≥ 5 → interval 触发
        assert(fired == 1);
    }

    // 7. interval = 0 关闭定时
    void TestIntervalDisabled()
    {
        int               fired = 0;
        AutosaveScheduler s({/*interval*/ 0.0, /*minBetween*/ 1.0},
                            [&]
                            { ++fired; });

        s.Update(100.0);
        assert(fired == 0); // 即使时间海量流逝，interval 关 → 不触发

        s.RequestAutosave();
        s.Update(0.0); // throttle 已过 + pending → 触发
        assert(fired == 1);
    }

    // 8. 异常 deltaSeconds 处理
    void TestEdgeCaseDeltas()
    {
        int               fired = 0;
        AutosaveScheduler s({/*interval*/ 10.0, /*minBetween*/ 5.0},
                            [&]
                            { ++fired; });

        // 负数 → 视为 0
        s.Update(-100.0);
        assert(fired == 0);
        assert(s.SecondsSinceLastFire() == 0.0);

        // 极大 deltaSeconds（游戏挂起恢复）→ 同一 Update 仅触发一次
        s.Update(1e6);
        assert(fired == 1);
    }

} // namespace

int main()
{
    TestInitialState();
    TestIntervalFires();
    TestThrottleBlocksInterval();
    TestRequestAutosaveImmediate();
    TestRequestAutosavePending();
    TestReset();
    TestIntervalDisabled();
    TestEdgeCaseDeltas();

    std::printf("[autosave_scheduler_test] all 8 cases passed\n");
    return 0;
}
