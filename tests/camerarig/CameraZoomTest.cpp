// CameraZoom2D 的 headless 单元测试：平滑相机缩放 (临界阻尼 SmoothDamp + zoom 钳制)。
// header-only 纯数学，无 World / GPU / GUI。裸 main() + <cassert>。确定性，可逐步断言。
//
// 覆盖点：
//   * 目标 / 当前 zoom 钳到 [min,max]（含超界）。
//   * SmoothDamp 收敛 —— 多步逼到目标 epsilon 内、单调、不过冲。
//   * 瞬移模式 (smoothTime=0) —— 一步到目标。
//   * maxSpeed 钳速 —— 大跨度目标下以近似钳速逼近 (非瞬移)。
//   * min>max 退化不钳 (容错)。
//   * SetZoom —— 硬置 + 清速度 + 同步目标。
//   * dt<=0 —— 不动。
//   * 带 initialZoom 构造。

#include "orange/engine/camerarig/CameraZoom.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Rig = ::Orange::Engine::CameraRig;
using Rig::CameraZoom2D;
using Rig::ZoomParams;

namespace
{

    bool Near(float a, float b, float eps = 1e-3f)
    {
        return std::fabs(a - b) <= eps;
    }

    int  gChecks = 0;
    void Check(bool cond, const char* what)
    {
        ++gChecks;
        if (!cond)
        {
            std::fprintf(stderr, "[CameraZoomTest] FAILED: %s\n", what);
            assert(cond);
        }
    }

} // namespace

int main()
{
    // —— 钳制：目标 / SetZoom 超界钳到 [min,max] ——
    {
        ZoomParams p{};
        p.minZoom = 0.5f;
        p.maxZoom = 4.0f;
        CameraZoom2D z(p);
        z.SetTargetZoom(100.0f);
        Check(Near(z.GetTargetZoom(), 4.0f), "clamp: 目标超上限钳到 max");
        z.SetTargetZoom(-1.0f);
        Check(Near(z.GetTargetZoom(), 0.5f), "clamp: 目标超下限钳到 min");
        z.SetZoom(100.0f);
        Check(Near(z.GetZoom(), 4.0f), "clamp: SetZoom 超界钳制");
    }

    // —— SmoothDamp 收敛：单调、不过冲、逼到目标 ——
    {
        ZoomParams p{};
        p.smoothTime = 0.3f;
        p.minZoom    = 0.1f;
        p.maxZoom    = 100.0f;
        CameraZoom2D z(p, 1.0f);
        z.SetTargetZoom(5.0f);
        const float dt         = 1.0f / 60.0f;
        float       prev       = 1.0f;
        float       maxReached = 1.0f;
        for (int i = 0; i < 240; ++i)
        {
            const float v = z.Update(dt);
            Check(v >= prev - 1e-4f, "zoom smoothdamp: 单调逼近 (不回退)");
            prev = v;
            if (v > maxReached)
                maxReached = v;
        }
        Check(Near(prev, 5.0f, 0.02f), "zoom smoothdamp: 收敛到目标");
        Check(maxReached <= 5.0f + 0.02f, "zoom smoothdamp: 不显著过冲");
    }

    // —— 瞬移模式：smoothTime=0 一步到目标 ——
    {
        ZoomParams p{};
        p.smoothTime = 0.0f;
        CameraZoom2D z(p, 1.0f);
        z.SetTargetZoom(3.0f);
        Check(Near(z.Update(1.0f / 60.0f), 3.0f), "snap: smoothTime=0 一步到目标");
    }

    // —— maxSpeed 钳速：大跨度以近似钳速逼近 (非瞬移) ——
    {
        ZoomParams p{};
        p.smoothTime = 0.3f;
        p.maxSpeed   = 2.0f;
        p.maxZoom    = 1000.0f;
        CameraZoom2D z(p, 1.0f);
        z.SetTargetZoom(1000.0f);
        for (int i = 0; i < 60; ++i)
            z.Update(1.0f / 60.0f); // 1 秒
        const float v = z.GetZoom();
        // 1 秒 @ maxSpeed=2 → 走 ~2 单位量级，远非瞬移到 1000。
        Check(v > 1.5f && v < 5.0f, "zoom maxSpeed: 1 秒内近似钳速逼近 (非瞬移)");
    }

    // —— min>max 退化不钳 ——
    {
        ZoomParams p{};
        p.smoothTime = 0.0f;
        p.minZoom    = 5.0f;
        p.maxZoom    = 1.0f; // min>max
        CameraZoom2D z(p);
        z.SetTargetZoom(50.0f);
        Check(Near(z.Update(1.0f / 60.0f), 50.0f), "degenerate: min>max 退化不钳");
    }

    // —— SetZoom 清速度 + 同步目标 ——
    {
        ZoomParams p{};
        p.smoothTime = 0.3f;
        CameraZoom2D z(p, 1.0f);
        z.SetTargetZoom(10.0f);
        z.Update(1.0f / 60.0f); // 制造非零速度
        z.SetZoom(2.0f);
        Check(Near(z.GetZoom(), 2.0f), "SetZoom: 硬置当前");
        Check(Near(z.GetTargetZoom(), 2.0f), "SetZoom: 同步目标 (不再漂回旧目标)");
        Check(Near(z.GetVelocity(), 0.0f), "SetZoom: 清速度");
    }

    // —— dt<=0 不动 ——
    {
        CameraZoom2D z(ZoomParams{}, 1.0f);
        z.SetTargetZoom(9.0f);
        Check(Near(z.Update(0.0f), 1.0f), "dt<=0: 不动");
    }

    std::printf("[CameraZoomTest] OK (%d checks)\n", gChecks);
    return 0;
}
