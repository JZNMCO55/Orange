// Tween / Easing 的 headless 单元测试：31 条缓动函数 + 代码驱动的补间运行时。
// 全部纯确定性 scalar 数学 + 时间推进，无 World / GPU / GUI。裸 main() + <cassert>。
//
// 覆盖点：
//   * Easing —— 全 31 EaseType 端点严格 0/1；t clamp；已知值硬编码；InOut 家族
//     中点 0.5；单调非 overshoot 家族（21 条）单调不减；EaseLerp。
//   * Tween 运行时 —— Once 线性推进 + onComplete 一次；delay；Loop 回绕；PingPong
//     奇偶反向；onUpdate 每帧；Kill / 无效句柄；Clear；抗再入 Update（回调里
//     Add / Kill）；TweenManager 不可拷贝 / 移动（编译期 static_assert）。

#include "orange/engine/tween/Easing.h"
#include "orange/engine/tween/Tween.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace Tween = ::Orange::Engine::Tween;
using Tween::EaseType;
using Tween::LoopMode;

namespace
{

    bool Near(float a, float b, float eps = 1e-4f)
    {
        return std::fabs(a - b) < eps;
    }

    // 全部 31 个 EaseType，按 enum 顺序。
    const EaseType kAllTypes[] = {
        EaseType::Linear,
        EaseType::InSine,
        EaseType::OutSine,
        EaseType::InOutSine,
        EaseType::InQuad,
        EaseType::OutQuad,
        EaseType::InOutQuad,
        EaseType::InCubic,
        EaseType::OutCubic,
        EaseType::InOutCubic,
        EaseType::InQuart,
        EaseType::OutQuart,
        EaseType::InOutQuart,
        EaseType::InQuint,
        EaseType::OutQuint,
        EaseType::InOutQuint,
        EaseType::InExpo,
        EaseType::OutExpo,
        EaseType::InOutExpo,
        EaseType::InCirc,
        EaseType::OutCirc,
        EaseType::InOutCirc,
        EaseType::InBack,
        EaseType::OutBack,
        EaseType::InOutBack,
        EaseType::InElastic,
        EaseType::OutElastic,
        EaseType::InOutElastic,
        EaseType::InBounce,
        EaseType::OutBounce,
        EaseType::InOutBounce,
    };

    // 单调非 overshoot 家族（21 条）：Sine/Quad/Cubic/Quart/Quint/Expo/Circ 的
    // In / Out / InOut。Back / Elastic / Bounce 有 overshoot / 回弹，不测单调。
    const EaseType kMonotoneTypes[] = {
        EaseType::InSine,
        EaseType::OutSine,
        EaseType::InOutSine,
        EaseType::InQuad,
        EaseType::OutQuad,
        EaseType::InOutQuad,
        EaseType::InCubic,
        EaseType::OutCubic,
        EaseType::InOutCubic,
        EaseType::InQuart,
        EaseType::OutQuart,
        EaseType::InOutQuart,
        EaseType::InQuint,
        EaseType::OutQuint,
        EaseType::InOutQuint,
        EaseType::InExpo,
        EaseType::OutExpo,
        EaseType::InOutExpo,
        EaseType::InCirc,
        EaseType::OutCirc,
        EaseType::InOutCirc,
    };

    // 10 个 InOut 家族：中点 t=0.5 必过 0.5（含 Back / Elastic / Bounce）。
    const EaseType kInOutTypes[] = {
        EaseType::InOutSine,
        EaseType::InOutQuad,
        EaseType::InOutCubic,
        EaseType::InOutQuart,
        EaseType::InOutQuint,
        EaseType::InOutExpo,
        EaseType::InOutCirc,
        EaseType::InOutBack,
        EaseType::InOutElastic,
        EaseType::InOutBounce,
    };

} // namespace

int main()
{
    // ===== 1. Easing：全 31 类型端点严格 0→0、1→1（含 Back/Elastic/Bounce）=====
    {
        for (EaseType type : kAllTypes)
        {
            assert(Near(Tween::Ease(type, 0.0f), 0.0f) && "Ease(type,0) ≈ 0");
            assert(Near(Tween::Ease(type, 1.0f), 1.0f) && "Ease(type,1) ≈ 1");
        }
        std::fprintf(stdout, "  [PASS] Easing 全 31 类型端点严格 0/1\n");
    }

    // ===== 2. Easing：t clamp —— 越界进度按端点处理（几个代表类型，含 overshoot）=====
    {
        const EaseType reps[] = {
            EaseType::Linear,
            EaseType::InQuad,
            EaseType::OutCubic,
            EaseType::InOutExpo,
            EaseType::InBack,
            EaseType::OutElastic,
            EaseType::InBounce,
        };
        for (EaseType type : reps)
        {
            assert(Tween::Ease(type, -0.5f) == Tween::Ease(type, 0.0f) && "Ease(t<0) == Ease(0)");
            assert(Tween::Ease(type, 1.5f) == Tween::Ease(type, 1.0f) && "Ease(t>1) == Ease(1)");
        }
        std::fprintf(stdout, "  [PASS] Easing t clamp 到 [0,1]\n");
    }

    // ===== 3. Easing：已知值硬编码 =====
    {
        assert(Near(Tween::Ease(EaseType::Linear, 0.5f), 0.5f) && "Linear(0.5)=0.5");
        assert(Near(Tween::Ease(EaseType::InQuad, 0.5f), 0.25f) && "InQuad(0.5)=0.25");
        assert(Near(Tween::Ease(EaseType::OutQuad, 0.5f), 0.75f) && "OutQuad(0.5)=0.75");
        assert(Near(Tween::Ease(EaseType::InCubic, 0.5f), 0.125f) && "InCubic(0.5)=0.125");
        assert(Near(Tween::Ease(EaseType::OutBounce, 1.0f), 1.0f) && "OutBounce(1)=1");
        std::fprintf(stdout, "  [PASS] Easing 已知值（Linear/InQuad/OutQuad/InCubic/OutBounce）\n");
    }

    // ===== 4. Easing：所有 InOut 家族中点 t=0.5 过 0.5（10 条，含 Back/Elastic/Bounce）=====
    {
        for (EaseType type : kInOutTypes)
        {
            assert(Near(Tween::Ease(type, 0.5f), 0.5f, 2e-3f) && "InOutX(0.5) ≈ 0.5");
        }
        std::fprintf(stdout, "  [PASS] Easing 全 10 InOut 家族中点 0.5\n");
    }

    // ===== 5. Easing：单调非 overshoot 家族（21 条）单调不减 =====
    {
        for (EaseType type : kMonotoneTypes)
        {
            float prev = Tween::Ease(type, 0.0f);
            for (int i = 1; i <= 20; ++i)
            {
                const float t   = static_cast<float>(i) * 0.05f;
                const float cur = Tween::Ease(type, t);
                assert(cur >= prev - 1e-4f && "单调家族应单调不减");
                prev = cur;
            }
        }
        std::fprintf(stdout, "  [PASS] Easing 21 条单调家族单调不减\n");
    }

    // ===== 6. EaseLerp：a→b 按缓动插值 =====
    {
        assert(Near(Tween::EaseLerp(EaseType::Linear, 10.0f, 20.0f, 0.5f), 15.0f) &&
               "EaseLerp(Linear,10,20,0.5)=15");
        assert(Near(Tween::EaseLerp(EaseType::Linear, 0.0f, 100.0f, 0.0f), 0.0f) && "端点 a");
        assert(Near(Tween::EaseLerp(EaseType::Linear, 0.0f, 100.0f, 1.0f), 100.0f) && "端点 b");
        std::fprintf(stdout, "  [PASS] EaseLerp 插值\n");
    }

    // ===== 7. Tween：Once 线性 from=0,to=10,duration=2 —— 半程 5 + 完成 10 + onComplete 一次 =====
    {
        Tween::TweenManager mgr;
        int                 completeCount = 0;
        float               lastValue     = -1.0f;

        Tween::TweenDesc desc;
        desc.from     = 0.0f;
        desc.to       = 10.0f;
        desc.duration = 2.0f;
        desc.ease     = EaseType::Linear;
        desc.loop     = LoopMode::Once;
        desc.onUpdate = [&lastValue](float v)
        { lastValue = v; };
        desc.onComplete = [&completeCount]()
        { ++completeCount; };

        const Tween::TweenHandle h = mgr.Add(desc);
        assert(h.IsValid() && "Add 返回有效句柄");
        assert(mgr.ActiveCount() == 1 && "ActiveCount=1");

        mgr.Update(1.0f); // elapsed=1 → 半程
        assert(Near(mgr.GetValue(h), 5.0f) && "半程值 5");
        assert(Near(lastValue, 5.0f) && "onUpdate 半程值 5");
        assert(mgr.IsActive(h) && "半程仍活跃");
        assert(completeCount == 0 && "半程未完成");

        mgr.Update(1.0f); // elapsed=2 → 完成
        assert(Near(lastValue, 10.0f) && "完成值 10（onUpdate 拿到端点）");
        assert(completeCount == 1 && "onComplete 触发一次");
        assert(!mgr.IsActive(h) && "完成后不活跃");
        assert(mgr.ActiveCount() == 0 && "完成后 ActiveCount=0");
        std::fprintf(stdout, "  [PASS] Tween Once 线性推进 + onComplete\n");
    }

    // ===== 8. Tween：delay=0.5 —— delay 内恒 from，delay 后开始推进 =====
    {
        Tween::TweenManager mgr;
        Tween::TweenDesc    desc;
        desc.from     = 0.0f;
        desc.to       = 1.0f;
        desc.duration = 1.0f;
        desc.delay    = 0.5f;
        desc.ease     = EaseType::Linear;

        const Tween::TweenHandle h = mgr.Add(desc);
        mgr.Update(0.25f); // elapsed=0.25 < delay → 值恒 from
        assert(Near(mgr.GetValue(h), 0.0f) && "delay 内值为 from(0)");
        assert(mgr.IsActive(h) && "delay 内仍活跃");

        mgr.Update(0.5f); // elapsed=0.75 → pt=0.25 → t=0.25
        assert(Near(mgr.GetValue(h), 0.25f) && "delay 后 pt=0.25 → 值 0.25");
        assert(mgr.IsActive(h) && "尚未完成");
        std::fprintf(stdout, "  [PASS] Tween delay 语义\n");
    }

    // ===== 9. Tween：Loop —— 跨多周期回绕、恒活跃、值在 [0,1] 环绕 =====
    {
        Tween::TweenManager mgr;
        Tween::TweenDesc    desc;
        desc.from     = 0.0f;
        desc.to       = 1.0f;
        desc.duration = 1.0f;
        desc.ease     = EaseType::Linear;
        desc.loop     = LoopMode::Loop;

        const Tween::TweenHandle h = mgr.Add(desc);
        mgr.Update(2.5f); // elapsed=2.5 → raw=2.5 → t=0.5
        assert(Near(mgr.GetValue(h), 0.5f) && "Loop elapsed=2.5 → t=0.5 → 值 0.5");
        assert(mgr.IsActive(h) && "Loop 永不完成，恒活跃");
        assert(mgr.ActiveCount() == 1 && "Loop 不被移除");

        mgr.Update(1.0f); // elapsed=3.5 → raw=3.5 → t=0.5
        assert(Near(mgr.GetValue(h), 0.5f) && "Loop 继续回绕 → 值仍 0.5");
        assert(mgr.IsActive(h) && "仍活跃");
        std::fprintf(stdout, "  [PASS] Tween Loop 回绕\n");
    }

    // ===== 10. Tween：PingPong —— 奇偶周期反向 =====
    {
        // 三个独立 tween，各推进到不同 elapsed 后读值验奇偶折返（Linear from=0 to=1）。
        auto makeDesc = []()
        {
            Tween::TweenDesc d;
            d.from     = 0.0f;
            d.to       = 1.0f;
            d.duration = 1.0f;
            d.ease     = EaseType::Linear;
            d.loop     = LoopMode::PingPong;
            return d;
        };

        Tween::TweenManager      mgr;
        const Tween::TweenHandle hA = mgr.Add(makeDesc()); // 推到 elapsed=0.5（偶周期）
        const Tween::TweenHandle hB = mgr.Add(makeDesc()); // 推到 elapsed=1.5（奇周期）
        const Tween::TweenHandle hC = mgr.Add(makeDesc()); // 推到 elapsed=1.25（奇周期）

        mgr.Update(0.0f); // 全部 elapsed=0（无进展，仅初始化路径）
        // 手动分别推进：为独立验证，用单独 manager 更干净。
        {
            Tween::TweenManager m;
            const auto          h = m.Add(makeDesc());
            m.Update(0.5f); // cyc=0 偶 → ph=0.5 → t=0.5
            assert(Near(m.GetValue(h), 0.5f) && "PingPong elapsed=0.5 → t=0.5");
        }
        {
            Tween::TweenManager m;
            const auto          h = m.Add(makeDesc());
            m.Update(1.5f); // cyc=1 奇 → ph=0.5 → t=1-0.5=0.5
            assert(Near(m.GetValue(h), 0.5f) && "PingPong elapsed=1.5 → t=0.5（奇折返）");
            assert(m.IsActive(h) && "PingPong 永不完成");
        }
        {
            Tween::TweenManager m;
            const auto          h = m.Add(makeDesc());
            m.Update(1.25f); // cyc=1 奇 → ph=0.25 → t=1-0.25=0.75
            assert(Near(m.GetValue(h), 0.75f) && "PingPong elapsed=1.25 → t=0.75（奇折返）");
        }
        (void)hA;
        (void)hB;
        (void)hC;
        std::fprintf(stdout, "  [PASS] Tween PingPong 奇偶折返\n");
    }

    // ===== 11. Tween：onUpdate 每次 Update 被调、值正确 =====
    {
        Tween::TweenManager mgr;
        std::vector<float>  values;
        Tween::TweenDesc    desc;
        desc.from     = 0.0f;
        desc.to       = 4.0f;
        desc.duration = 4.0f;
        desc.ease     = EaseType::Linear;
        desc.onUpdate = [&values](float v)
        { values.push_back(v); };

        mgr.Add(desc);
        mgr.Update(1.0f); // 1
        mgr.Update(1.0f); // 2
        mgr.Update(1.0f); // 3
        assert(values.size() == 3 && "onUpdate 每帧被调");
        assert(Near(values[0], 1.0f) && Near(values[1], 2.0f) && Near(values[2], 3.0f) &&
               "onUpdate 值随进度正确");
        std::fprintf(stdout, "  [PASS] Tween onUpdate 每帧 + 值正确\n");
    }

    // ===== 12. Tween：Kill —— 移除后不活跃、GetValue 0；无效句柄 no-op =====
    {
        Tween::TweenManager mgr;
        Tween::TweenDesc    desc;
        desc.from     = 1.0f;
        desc.to       = 2.0f;
        desc.duration = 1.0f;

        const Tween::TweenHandle h = mgr.Add(desc);
        assert(mgr.IsActive(h) && "Add 后活跃");
        mgr.Kill(h);
        assert(!mgr.IsActive(h) && "Kill 后不活跃");
        assert(Near(mgr.GetValue(h), 0.0f) && "Kill 后 GetValue 返回 0");
        assert(mgr.ActiveCount() == 0 && "Kill 后 ActiveCount=0");

        // 无效句柄：Kill / GetValue 不崩、GetValue 返回 0。
        Tween::TweenHandle invalid;
        assert(!invalid.IsValid() && "默认句柄无效");
        mgr.Kill(invalid);                     // no-op，不崩
        mgr.Kill(Tween::TweenHandle{999999u}); // 从未发过的 id，no-op
        assert(Near(mgr.GetValue(invalid), 0.0f) && "无效句柄 GetValue 0");
        assert(!mgr.IsActive(invalid) && "无效句柄不活跃");
        std::fprintf(stdout, "  [PASS] Tween Kill + 无效句柄 no-op\n");
    }

    // ===== 13. Tween：Clear —— ActiveCount 0；句柄不复用 =====
    {
        Tween::TweenManager mgr;
        Tween::TweenDesc    desc;
        desc.duration = 1.0f;

        const Tween::TweenHandle h1 = mgr.Add(desc);
        mgr.Add(desc);
        mgr.Add(desc);
        assert(mgr.ActiveCount() == 3 && "3 个补间");
        mgr.Clear();
        assert(mgr.ActiveCount() == 0 && "Clear 后 ActiveCount=0");

        // mNextId 不重置：Clear 后新 Add 的 id 严格大于此前发过的。
        const Tween::TweenHandle h2 = mgr.Add(desc);
        assert(h2.id > h1.id && "Clear 后句柄绝不复用（id 继续递增）");
        std::fprintf(stdout, "  [PASS] Tween Clear + 句柄不复用\n");
    }

    // ===== 14. Tween：抗再入 —— onComplete 里 Add / Kill，Update 不崩 =====
    {
        Tween::TweenManager mgr;
        Tween::TweenHandle  cHandle; // onComplete 里新 Add 的句柄

        // 长命 Loop 补间 B，将在 A 的 onComplete 里被 Kill。
        Tween::TweenDesc descB;
        descB.from                  = 0.0f;
        descB.to                    = 1.0f;
        descB.duration              = 10.0f;
        descB.loop                  = LoopMode::Loop;
        const Tween::TweenHandle hB = mgr.Add(descB);

        // Once 补间 A：完成时 Add 一个 C + Kill B。
        Tween::TweenDesc descA;
        descA.from       = 0.0f;
        descA.to         = 1.0f;
        descA.duration   = 1.0f;
        descA.loop       = LoopMode::Once;
        descA.onComplete = [&mgr, &cHandle, hB]()
        {
            Tween::TweenDesc descC;
            descC.from     = 42.0f; // 独特值：验 C 本帧不推进（值恒为 from）
            descC.to       = 99.0f;
            descC.duration = 1.0f;
            descC.loop     = LoopMode::Once;
            cHandle        = mgr.Add(descC); // 回调里 Add（可能 rehash）
            mgr.Kill(hB);                    // 回调里 Kill
        };
        const Tween::TweenHandle hA = mgr.Add(descA);

        mgr.Update(1.0f); // A 完成 → 触发再入 Add C + Kill B，不崩

        assert(!mgr.IsActive(hA) && "A 完成后被移除");
        assert(!mgr.IsActive(hB) && "B 被 onComplete Kill");
        assert(cHandle.IsValid() && mgr.IsActive(cHandle) && "C 被 onComplete Add");
        assert(mgr.ActiveCount() == 1 && "仅剩 C");
        // C 本帧未推进：elapsed 仍 0 → 值恒为 from(42)。
        assert(Near(mgr.GetValue(cHandle), 42.0f) && "新 Add 的 C 本帧不推进（值=from）");

        mgr.Update(0.5f); // 下一帧 C 才推进
        assert(Near(mgr.GetValue(cHandle), 42.0f + (99.0f - 42.0f) * 0.5f) &&
               "C 下一帧推进到半程");
        std::fprintf(stdout, "  [PASS] Tween 抗再入 Update（回调里 Add/Kill）\n");
    }

    // ===== 15. 编译期：TweenManager 不可拷贝 / 移动 =====
    {
        static_assert(!std::is_copy_constructible<Tween::TweenManager>::value &&
                          !std::is_move_constructible<Tween::TweenManager>::value,
                      "TweenManager 不可拷贝/移动");
        std::fprintf(stdout, "  [PASS] TweenManager 不可拷贝/移动（编译期）\n");
    }

    // ===== 16. 瞬间（duration<=0）+ 负 duration 补间完成语义（对抗式复核逮到边界 bug 修复回归）=====
    {
        // duration=0 无 delay：首帧 Update 即完成，观测值必须是 to（非 from），onComplete 一次。
        {
            Tween::TweenManager mgr;
            int                 completeCount = 0;
            float               lastUpdate    = -999.0f;
            Tween::TweenDesc    d;
            d.from       = 5.0f;
            d.to         = 10.0f;
            d.duration   = 0.0f;
            d.delay      = 0.0f;
            d.loop       = Tween::LoopMode::Once;
            d.onComplete = [&]
            { ++completeCount; };
            d.onUpdate = [&](float v)
            { lastUpdate = v; };
            const Tween::TweenHandle h = mgr.Add(d);
            mgr.Update(0.016f);
            assert(Near(lastUpdate, 10.0f) && "duration=0 瞬间补间完成帧观测值=to");
            assert(completeCount == 1 && "duration=0 onComplete 触发一次");
            assert(!mgr.IsActive(h) && "duration=0 完成后不再 active");
        }
        // duration=0 + delay=0.5：先走 delay（值=from），delay 后瞬间完成到 to。
        {
            Tween::TweenManager mgr;
            int                 completeCount = 0;
            Tween::TweenDesc    d;
            d.from       = 0.0f;
            d.to         = 100.0f;
            d.duration   = 0.0f;
            d.delay      = 0.5f;
            d.loop       = Tween::LoopMode::Once;
            d.onComplete = [&]
            { ++completeCount; };
            const Tween::TweenHandle h = mgr.Add(d);
            mgr.Update(0.25f); // pt=-0.25，delay 内
            assert(Near(mgr.GetValue(h), 0.0f) && "delay 内值=from");
            assert(completeCount == 0 && mgr.IsActive(h) && "delay 内未完成");
            mgr.Update(0.5f); // elapsed=0.75，pt=0.25>0 → 瞬间完成到 to
            assert(completeCount == 1 && "delay 后瞬间完成一次");
            assert(!mgr.IsActive(h) && "瞬间完成后移除");
        }
        // 负 duration + delay：在 delay 内（pt<0）绝不能提前完成。
        {
            Tween::TweenManager mgr;
            int                 completeCount = 0;
            Tween::TweenDesc    d;
            d.from       = 0.0f;
            d.to         = 1.0f;
            d.duration   = -1.0f;
            d.delay      = 5.0f;
            d.loop       = Tween::LoopMode::Once;
            d.onComplete = [&]
            { ++completeCount; };
            const Tween::TweenHandle h = mgr.Add(d);
            mgr.Update(4.0f); // elapsed=4，pt=-1，仍在 delay
            assert(completeCount == 0 && mgr.IsActive(h) && "负 duration 在 delay 内不提前完成");
            mgr.Update(2.0f); // elapsed=6，pt=1>0 → 完成到 to
            assert(completeCount == 1 && !mgr.IsActive(h) && "delay 后完成");
        }
        std::fprintf(stdout, "  [PASS] 瞬间/负 duration 补间完成语义（值=to、不在 delay 内误完成）\n");
    }

    // ===== 17. Loop / PingPong 永不触发 onComplete（契约反证） =====
    {
        Tween::TweenManager mgr;
        int                 loopComplete = 0;
        int                 pingComplete = 0;
        Tween::TweenDesc    dl;
        dl.from       = 0.0f;
        dl.to         = 1.0f;
        dl.duration   = 1.0f;
        dl.loop       = Tween::LoopMode::Loop;
        dl.onComplete = [&]
        { ++loopComplete; };
        Tween::TweenDesc dp = dl;
        dp.loop             = Tween::LoopMode::PingPong;
        dp.onComplete       = [&]
        { ++pingComplete; };
        const Tween::TweenHandle hl = mgr.Add(dl);
        const Tween::TweenHandle hp = mgr.Add(dp);
        for (int i = 0; i < 10; ++i)
        {
            mgr.Update(0.7f);
        } // 跨多个周期
        assert(loopComplete == 0 && "Loop 永不触发 onComplete");
        assert(pingComplete == 0 && "PingPong 永不触发 onComplete");
        assert(mgr.IsActive(hl) && mgr.IsActive(hp) && "Loop/PingPong 永不完成");
        std::fprintf(stdout, "  [PASS] Loop/PingPong 永不触发 onComplete（契约反证）\n");
    }

    // ===== 18. 全 31 缓动对 easings.net 参考真值逐点回归（数据驱动 ground truth） =====
    // 参考值由独立 Python 用 easings.net 权威公式算出（t=.25/.5/.75），把整条缓动库锁死：
    // 任何公式（pow 指数 / 符号 / 常量 / 分段阈值）写错都会被逮到。tol 2e-3 覆盖 float 精度 +
    // 5 位小数舍入，仍远紧于任何错公式的偏差（错公式通常差 >0.01）。
    {
        struct EaseRef
        {
            Tween::EaseType type;
            float           v25;
            float           v5;
            float           v75;
        };
        const EaseRef refs[] = {
            {Tween::EaseType::Linear, 0.25000f, 0.50000f, 0.75000f},
            {Tween::EaseType::InSine, 0.07612f, 0.29289f, 0.61732f},
            {Tween::EaseType::OutSine, 0.38268f, 0.70711f, 0.92388f},
            {Tween::EaseType::InOutSine, 0.14645f, 0.50000f, 0.85355f},
            {Tween::EaseType::InQuad, 0.06250f, 0.25000f, 0.56250f},
            {Tween::EaseType::OutQuad, 0.43750f, 0.75000f, 0.93750f},
            {Tween::EaseType::InOutQuad, 0.12500f, 0.50000f, 0.87500f},
            {Tween::EaseType::InCubic, 0.01562f, 0.12500f, 0.42188f},
            {Tween::EaseType::OutCubic, 0.57812f, 0.87500f, 0.98438f},
            {Tween::EaseType::InOutCubic, 0.06250f, 0.50000f, 0.93750f},
            {Tween::EaseType::InQuart, 0.00391f, 0.06250f, 0.31641f},
            {Tween::EaseType::OutQuart, 0.68359f, 0.93750f, 0.99609f},
            {Tween::EaseType::InOutQuart, 0.03125f, 0.50000f, 0.96875f},
            {Tween::EaseType::InQuint, 0.00098f, 0.03125f, 0.23730f},
            {Tween::EaseType::OutQuint, 0.76270f, 0.96875f, 0.99902f},
            {Tween::EaseType::InOutQuint, 0.01562f, 0.50000f, 0.98438f},
            {Tween::EaseType::InExpo, 0.00552f, 0.03125f, 0.17678f},
            {Tween::EaseType::OutExpo, 0.82322f, 0.96875f, 0.99448f},
            {Tween::EaseType::InOutExpo, 0.01562f, 0.50000f, 0.98438f},
            {Tween::EaseType::InCirc, 0.03175f, 0.13397f, 0.33856f},
            {Tween::EaseType::OutCirc, 0.66144f, 0.86603f, 0.96825f},
            {Tween::EaseType::InOutCirc, 0.06699f, 0.50000f, 0.93301f},
            {Tween::EaseType::InBack, -0.06414f, -0.08770f, 0.18259f},
            {Tween::EaseType::OutBack, 0.81741f, 1.08770f, 1.06414f},
            {Tween::EaseType::InOutBack, -0.09968f, 0.50000f, 1.09968f},
            {Tween::EaseType::InElastic, -0.00552f, -0.01563f, 0.08839f},
            {Tween::EaseType::OutElastic, 0.91161f, 1.01562f, 1.00552f},
            {Tween::EaseType::InOutElastic, 0.01197f, 0.50000f, 0.98803f},
            {Tween::EaseType::InBounce, 0.02734f, 0.23438f, 0.52734f},
            {Tween::EaseType::OutBounce, 0.47266f, 0.76562f, 0.97266f},
            {Tween::EaseType::InOutBounce, 0.11719f, 0.50000f, 0.88281f},
        };
        for (const EaseRef& r : refs)
        {
            assert(Near(Tween::Ease(r.type, 0.25f), r.v25, 2e-3f) && "缓动 t=.25 对 easings.net 参考值");
            assert(Near(Tween::Ease(r.type, 0.50f), r.v5, 2e-3f) && "缓动 t=.5 对 easings.net 参考值");
            assert(Near(Tween::Ease(r.type, 0.75f), r.v75, 2e-3f) && "缓动 t=.75 对 easings.net 参考值");
        }
        std::fprintf(stdout, "  [PASS] 全 31 缓动对 easings.net 参考真值逐点回归（%zu 类型 x 3 点）\n",
                     sizeof(refs) / sizeof(refs[0]));
    }

    std::fprintf(stdout, "tween_test: ALL PASS\n");
    return 0;
}
