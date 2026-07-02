// Timer 的 headless 单元测试：CountdownTimer（倒计时）+ Cooldown（冷却门）。
// 纯 scalar 逻辑、无依赖、确定性，故断言全为精确值。裸 main() + <cassert>。
//
// 覆盖点：
//   * CountdownTimer one-shot —— 逐 Tick 不触发直到越过 duration 触发一次 + IsFinished + 完成后 Tick 返 0。
//   * Reset —— 重置回满 + 清完成标志。
//   * Progress —— 归一化 [0,1] 精确值。
//   * looping —— 自动补周期不完成；★单次大 dt 跨多周期 → 触发次数 >1。
//   * Stop —— 立即完成 + remaining 清零。
//   * duration<=0 退化 —— Tick no-op（★looping 不无限补周期）+ Progress=1。
//   * Cooldown —— TryTrigger 门控（就绪 true 并开冷却 / 冷却中 false）+ Tick 消耗到就绪 + FractionReady + ResetReady + dt<=0 不动 + duration<=0 恒就绪。
// 仅 orange_engine + 标准库，headless 完全可验。

#include "orange/engine/timer/Timer.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace T = ::Orange::Engine::Timer;
using T::Cooldown;
using T::CountdownTimer;

namespace
{

bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

int gChecks = 0;
void Check(bool cond, const char* what)
{
    ++gChecks;
    if (!cond)
    {
        std::fprintf(stderr, "[TimerTest] FAILED: %s\n", what);
        assert(cond);
    }
}

} // namespace

int main()
{
    // —— CountdownTimer one-shot ——
    {
        CountdownTimer t(2.0f);
        Check(t.IsRunning() && !t.IsFinished(), "one-shot：初始运行中");
        Check(Near(t.Remaining(), 2.0f), "one-shot：初始 remaining=duration");
        Check(t.Tick(0.5f) == 0, "one-shot：0.5 未触发");
        Check(Near(t.Remaining(), 1.5f), "one-shot：remaining 递减");
        Check(t.Tick(0.5f) == 0 && t.Tick(0.5f) == 0, "one-shot：累计 1.5 仍未触发");
        Check(Near(t.Remaining(), 0.5f), "one-shot：remaining 0.5");
        Check(t.Tick(0.5f) == 1, "one-shot：越过 duration 触发一次");
        Check(t.IsFinished() && !t.IsRunning(), "one-shot：触发后完成");
        Check(Near(t.Remaining(), 0.0f) && Near(t.Progress(), 1.0f), "one-shot：完成 remaining0 / progress1");
        Check(t.Tick(0.5f) == 0, "one-shot：完成后 Tick 返 0");

        // Reset
        t.Reset();
        Check(t.IsRunning() && !t.IsFinished() && Near(t.Remaining(), 2.0f), "Reset：回满并重新运行");
        Check(Near(t.Progress(), 0.0f), "Reset：progress 归 0");
        t.Tick(0.5f);
        Check(Near(t.Progress(), 0.25f), "Progress：1 - 1.5/2 = 0.25");
    }

    // —— looping：补周期不完成 + 大 dt 跨多周期 ——
    {
        CountdownTimer t(1.0f, true);
        Check(t.Tick(0.5f) == 0, "looping：0.5 未触发");
        Check(t.Tick(0.5f) == 1, "looping：满 1 周期触发一次");
        Check(!t.IsFinished(), "looping：触发后不完成（自动续）");
        Check(Near(t.Remaining(), 1.0f), "looping：补回一个周期");

        CountdownTimer u(1.0f, true);
        Check(u.Tick(2.5f) == 2, "looping：单次 dt=2.5 跨 2 个周期 → 触发 2 次");
        Check(Near(u.Remaining(), 0.5f), "looping：跨多周期后 remaining 余量正确");
    }

    // —— Stop ——
    {
        CountdownTimer t(5.0f);
        t.Tick(1.0f);
        t.Stop();
        Check(t.IsFinished() && Near(t.Remaining(), 0.0f), "Stop：立即完成 + remaining 清零");
        Check(t.Tick(1.0f) == 0, "Stop：之后 Tick 返 0");
    }

    // —— duration<=0 退化：Tick no-op，looping 不无限循环 ——
    {
        CountdownTimer t(0.0f);
        Check(t.Tick(1.0f) == 0, "duration0：Tick no-op");
        Check(Near(t.Progress(), 1.0f), "duration0：progress=1");

        CountdownTimer loopZero(0.0f, true);
        Check(loopZero.Tick(1.0f) == 0, "duration0 looping：不无限补周期（返 0）");
    }

    // —— Cooldown 门控 ——
    {
        Cooldown cd(2.0f);
        Check(cd.IsReady(), "cooldown：初始就绪");
        Check(cd.TryTrigger(), "cooldown：就绪时 TryTrigger 成功");
        Check(!cd.IsReady(), "cooldown：触发后进入冷却");
        Check(Near(cd.FractionReady(), 0.0f), "cooldown：刚触发 fractionReady=0");
        Check(!cd.TryTrigger(), "cooldown：冷却中 TryTrigger 失败");

        cd.Tick(1.0f);
        Check(!cd.IsReady() && Near(cd.Remaining(), 1.0f), "cooldown：Tick 消耗一半仍未就绪");
        Check(Near(cd.FractionReady(), 0.5f), "cooldown：fractionReady=0.5");
        cd.Tick(1.5f); // 越过：remaining 钳到 0
        Check(cd.IsReady() && Near(cd.Remaining(), 0.0f), "cooldown：Tick 越过后就绪（钳 0 不负）");
        Check(Near(cd.FractionReady(), 1.0f), "cooldown：就绪 fractionReady=1");
        Check(cd.TryTrigger(), "cooldown：就绪后可再次触发");

        cd.ResetReady();
        Check(cd.IsReady(), "cooldown：ResetReady 强制就绪");

        // dt<=0 不动
        cd.TryTrigger();
        const float rem = cd.Remaining();
        cd.Tick(0.0f);
        cd.Tick(-1.0f);
        Check(Near(cd.Remaining(), rem), "cooldown：dt<=0 不消耗");

        // duration<=0 恒就绪
        Cooldown cz(0.0f);
        Check(cz.IsReady() && cz.TryTrigger() && cz.IsReady(), "cooldown：duration0 恒就绪");
        Check(Near(cz.FractionReady(), 1.0f), "cooldown：duration0 fractionReady=1");
    }

    std::printf("[TimerTest] all %d checks passed\n", gChecks);
    return 0;
}
