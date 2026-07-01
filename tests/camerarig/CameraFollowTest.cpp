// CameraFollow2D 的 headless 单元测试：2D 相机跟随 (deadzone + SmoothDamp + 边界钳制)。
// 纯数据 + 数学，无 World / GPU / GUI。裸 main() + <cassert>。确定性，故可逐步断言。
//
// 覆盖点：
//   * 瞬移模式 (smoothTime=0, 无死区) —— 相机一步贴到目标。
//   * 死区保持 —— 目标在死区内移动，相机某轴不追。
//   * 死区跟随 —— 目标出死区，相机被推到"目标恰在死区边沿"。
//   * SmoothDamp 收敛 —— 多步后逼到目标 epsilon 内、不显著过冲、单调逼近。
//   * maxSpeed 钳速 —— 目标远跳，相机以近似 maxSpeed 前进 (不瞬移)。
//   * 边界钳制 —— 相机中心钳到世界矩形内 (含某轴 min>max 退化不钳)。
//   * SetPosition —— 硬置位并清速度。
//   * dt<=0 —— 不动 (暂停帧 / 防除零)。

#include "orange/engine/camerarig/CameraFollow.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Rig = ::Orange::Engine::CameraRig;
using Rig::CameraFollow2D;
using Rig::FollowParams;

namespace
{

bool Near(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

int gChecks = 0;
void Check(bool cond, const char* what)
{
    ++gChecks;
    if (!cond)
    {
        std::fprintf(stderr, "[CameraFollowTest] FAILED: %s\n", what);
        assert(cond);
    }
}

} // namespace

int main()
{
    // —— 瞬移模式：smoothTime=0 + 无死区 → 相机一步贴到目标 ——
    {
        FollowParams p{};
        p.smoothTime = 0.0f;
        CameraFollow2D cam(p);
        cam.SetPosition({0.0f, 0.0f});
        const glm::vec2 out = cam.Update({7.0f, -3.0f}, 1.0f / 60.0f);
        Check(Near(out.x, 7.0f) && Near(out.y, -3.0f), "snap: 相机一步贴到目标");
        Check(Near(cam.GetPosition().x, 7.0f), "snap: GetPosition 同步");
    }

    // —— 死区保持：目标在死区内移动，相机不追 ——
    {
        FollowParams p{};
        p.smoothTime = 0.0f;
        p.deadzoneHalfExtent = {2.0f, 2.0f};
        CameraFollow2D cam(p);
        cam.SetPosition({0.0f, 0.0f});
        const glm::vec2 out = cam.Update({1.5f, -1.0f}, 1.0f / 60.0f); // |delta| < 2 → 死区内
        Check(Near(out.x, 0.0f) && Near(out.y, 0.0f), "deadzone: 死区内目标不带动相机");
    }

    // —— 死区跟随：目标出死区 → 相机推到"目标恰在死区边沿" ——
    {
        FollowParams p{};
        p.smoothTime = 0.0f;
        p.deadzoneHalfExtent = {2.0f, 2.0f};
        CameraFollow2D cam(p);
        cam.SetPosition({0.0f, 0.0f});
        const glm::vec2 out = cam.Update({5.0f, 0.0f}, 1.0f / 60.0f); // delta.x=5 > 2
        // 期望 cam.x = target.x - halfExtent = 3；即 target 恰在右侧死区边沿。
        Check(Near(out.x, 3.0f), "deadzone: 出死区后 target 贴死区边沿 (cam=target-half)");
        Check(Near(out.y, 0.0f), "deadzone: 未出死区的轴不动");
    }

    // —— SmoothDamp 收敛：多步逼到目标、不显著过冲、单调逼近 ——
    {
        FollowParams p{};
        p.smoothTime = 0.3f;
        CameraFollow2D cam(p);
        cam.SetPosition({0.0f, 0.0f});
        const float target = 10.0f;
        const float dt = 1.0f / 60.0f;
        float prev = 0.0f;
        float maxReached = 0.0f;
        for (int i = 0; i < 240; ++i) // 4 秒
        {
            const float x = cam.Update({target, 0.0f}, dt).x;
            Check(x >= prev - 1e-4f, "smoothdamp: 单调逼近 (不回退)");
            prev = x;
            if (x > maxReached) maxReached = x;
        }
        Check(Near(prev, target, 0.05f), "smoothdamp: 足够时间后收敛到目标");
        Check(maxReached <= target + 0.05f, "smoothdamp: 不显著过冲 (临界阻尼)");
    }

    // —— maxSpeed 钳速：目标远跳，相机以近似 maxSpeed 前进 (不瞬移) ——
    {
        FollowParams p{};
        p.smoothTime = 0.3f;
        p.maxSpeed = 5.0f;
        CameraFollow2D cam(p);
        cam.SetPosition({0.0f, 0.0f});
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 60; ++i) // 1 秒
        {
            cam.Update({1000.0f, 0.0f}, dt);
        }
        const float x = cam.GetPosition().x;
        // 1 秒 @ maxSpeed=5 → 走 ~5 单位量级，远非瞬移到 1000；给宽松区间容 SmoothDamp 形状。
        Check(x > 2.0f && x < 8.0f, "maxSpeed: 1 秒内以近似钳速前进 (非瞬移)");
    }

    // —— 边界钳制：相机中心钳到世界矩形内 ——
    {
        FollowParams p{};
        p.smoothTime = 0.0f;
        p.hasBounds = true;
        p.boundsMin = {-10.0f, -10.0f};
        p.boundsMax = {10.0f, 10.0f};
        CameraFollow2D cam(p);
        cam.SetPosition({0.0f, 0.0f});
        const glm::vec2 out = cam.Update({100.0f, -100.0f}, 1.0f / 60.0f);
        Check(Near(out.x, 10.0f) && Near(out.y, -10.0f), "bounds: 相机钳到边界矩形");
    }

    // —— 边界某轴 min>max 退化不钳 (容错不崩) ——
    {
        FollowParams p{};
        p.smoothTime = 0.0f;
        p.hasBounds = true;
        p.boundsMin = {5.0f, -10.0f};
        p.boundsMax = {-5.0f, 10.0f}; // x 轴 min>max → 退化不钳；y 正常
        CameraFollow2D cam(p);
        cam.SetPosition({0.0f, 0.0f});
        const glm::vec2 out = cam.Update({100.0f, 100.0f}, 1.0f / 60.0f);
        Check(Near(out.x, 100.0f), "bounds: x 轴 min>max 退化不钳 (不崩)");
        Check(Near(out.y, 10.0f), "bounds: y 轴正常钳制");
    }

    // —— SetPosition：硬置位 + 清速度 ——
    {
        FollowParams p{};
        p.smoothTime = 0.3f;
        CameraFollow2D cam(p);
        cam.Update({50.0f, 0.0f}, 1.0f / 60.0f); // 制造非零速度
        cam.SetPosition({1.0f, 2.0f});
        Check(Near(cam.GetPosition().x, 1.0f) && Near(cam.GetPosition().y, 2.0f), "SetPosition: 硬置位");
        Check(Near(cam.GetVelocity().x, 0.0f) && Near(cam.GetVelocity().y, 0.0f), "SetPosition: 清速度");
    }

    // —— dt<=0：不动 ——
    {
        FollowParams p{};
        CameraFollow2D cam(p);
        cam.SetPosition({3.0f, 4.0f});
        const glm::vec2 out = cam.Update({99.0f, 99.0f}, 0.0f);
        Check(Near(out.x, 3.0f) && Near(out.y, 4.0f), "dt<=0: 不动");
    }

    std::printf("[CameraFollowTest] OK (%d checks)\n", gChecks);
    return 0;
}
