// CameraShake2D 的 headless 单元测试：基于 trauma 的 2D 相机抖动。纯数据 + 数学 (自包含
// hash 值噪声)，无 World / GPU / GUI。裸 main() + <cassert>。确定性，故可逐步断言。
//
// 覆盖点：
//   * AddTrauma / SetTrauma 钳制到 [0,1]（含负值 / 超 1）。
//   * trauma 衰减 —— 随时间线性减、到 0 停；traumaDecay=0 不衰减。
//   * trauma=0 → 零偏移；SetTrauma(0) 立即停抖。
//   * 偏移有界 —— |translation| <= maxTranslation（shake<=1 × noise∈[-1,1]）。
//   * 偏移随时间变化 —— 满 trauma 下多帧采样非全零、有实际抖动量。
//   * 强度随 trauma 单调 —— 同 seed 同相位下高 trauma 的偏移量 >= 低 trauma。
//   * 确定性 —— 同 seed + 同 dt 序列 → 逐帧偏移完全相同。
//   * roll —— maxRoll=0 时 roll 恒 0。

#include "orange/engine/camerarig/CameraShake.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace Rig = ::Orange::Engine::CameraRig;
using Rig::CameraShake2D;
using Rig::ShakeOffset;
using Rig::ShakeParams;

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
        std::fprintf(stderr, "[CameraShakeTest] FAILED: %s\n", what);
        assert(cond);
    }
}

} // namespace

int main()
{
    // —— AddTrauma / SetTrauma 钳制 ——
    {
        CameraShake2D s;
        s.AddTrauma(2.0f);
        Check(Near(s.GetTrauma(), 1.0f), "AddTrauma: 超 1 钳到 1");
        s.AddTrauma(-5.0f);
        Check(Near(s.GetTrauma(), 1.0f), "AddTrauma: 负值不减 trauma");
        s.SetTrauma(-1.0f);
        Check(Near(s.GetTrauma(), 0.0f), "SetTrauma: 负值钳到 0");
        s.SetTrauma(0.4f);
        Check(Near(s.GetTrauma(), 0.4f), "SetTrauma: 正常置值");
    }

    // —— trauma 衰减：随时间线性减、到 0 停 ——
    {
        ShakeParams p{};
        p.traumaDecay = 1.0f;  // 1 秒衰减完
        CameraShake2D s(p);
        s.SetTrauma(1.0f);
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f); // 0.5 秒
        Check(std::fabs(s.GetTrauma() - 0.5f) < 0.02f, "decay: 0.5 秒后 trauma≈0.5");
        for (int i = 0; i < 60; ++i) s.Update(1.0f / 60.0f); // 再 1 秒 → 早已到 0
        Check(Near(s.GetTrauma(), 0.0f), "decay: 足够时间后 trauma 到 0 并停");
    }

    // —— trauma=0 → 零偏移 ——
    {
        CameraShake2D s;
        const ShakeOffset o = s.Update(1.0f / 60.0f);
        Check(Near(o.translation.x, 0.0f) && Near(o.translation.y, 0.0f) && Near(o.roll, 0.0f),
              "zero-trauma: 无 trauma 时零偏移");
    }

    // —— 偏移有界 + 满 trauma 下有实际抖动量 ——
    {
        ShakeParams p{};
        p.maxTranslation = {0.5f, 0.5f};
        p.traumaDecay = 0.0f;   // 不衰减，保持满 trauma 采样
        p.maxRoll = 0.0f;
        CameraShake2D s(p);
        s.SetTrauma(1.0f);
        float maxAbsX = 0.0f;
        bool  bounded = true;
        for (int i = 0; i < 300; ++i)
        {
            const ShakeOffset o = s.Update(1.0f / 60.0f);
            maxAbsX = std::max(maxAbsX, std::fabs(o.translation.x));
            if (std::fabs(o.translation.x) > p.maxTranslation.x + 1e-4f) bounded = false;
            if (std::fabs(o.translation.y) > p.maxTranslation.y + 1e-4f) bounded = false;
            Check(Near(o.roll, 0.0f), "roll: maxRoll=0 时 roll 恒 0");
        }
        Check(bounded, "bounded: |translation| 不超 maxTranslation");
        Check(maxAbsX > 0.2f, "shake: 满 trauma 下多帧采样有实际抖动量 (非全零)");
    }

    // —— 强度随 trauma 单调：同 seed 同相位，高 trauma 偏移量 >= 低 trauma ——
    {
        ShakeParams p{};
        p.traumaDecay = 0.0f;
        p.seed = 7u;
        CameraShake2D hi(p), lo(p);
        hi.SetTrauma(1.0f);
        lo.SetTrauma(0.5f);
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 120; ++i)
        {
            const ShakeOffset a = hi.Update(dt);
            const ShakeOffset b = lo.Update(dt);
            // 同 mTime 同 seed → 同 noise 相位；shake_hi=1 >= shake_lo=0.5^exp。
            if (std::fabs(a.translation.x) > 0.01f)
            {
                Check(std::fabs(a.translation.x) >= std::fabs(b.translation.x) - 1e-4f,
                      "monotonic: 高 trauma 偏移量 >= 低 trauma");
            }
        }
    }

    // —— 确定性：同 seed + 同 dt 序列 → 逐帧偏移相同 ——
    {
        ShakeParams p{};
        p.traumaDecay = 0.0f;
        p.seed = 42u;
        CameraShake2D a(p), b(p);
        a.SetTrauma(0.8f);
        b.SetTrauma(0.8f);
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 120; ++i)
        {
            const ShakeOffset oa = a.Update(dt);
            const ShakeOffset ob = b.Update(dt);
            Check(Near(oa.translation.x, ob.translation.x) && Near(oa.translation.y, ob.translation.y),
                  "deterministic: 同 seed+dt 序列逐帧偏移相同");
        }
    }

    std::printf("[CameraShakeTest] OK (%d checks)\n", gChecks);
    return 0;
}
