// CameraRig2D 的 headless 单元测试：follow + shake + zoom 组合装置。header-only 纯聚合，
// 无 World / GPU / GUI。裸 main() + <cassert>。确定性，可逐步断言。
//
// 覆盖点：
//   * 无 shake（trauma=0）→ 合成 position == follow 位置、roll=0。
//   * shake 只叠加视觉偏移不污染 follow 真相 —— follow snap 模式下 GetFollowPosition()==target
//     恒成立，而合成 position 因 shake 偏离 target，偏离量 <= maxTranslation。
//   * zoom 合成 —— State.zoom == 子 zoom 控制器当前值。
//   * 便利转发 —— AddTrauma / SetTargetZoom / SnapTo 作用到对应子控制器。

#include "orange/engine/camerarig/CameraRig.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Rig = ::Orange::Engine::CameraRig;
using Rig::CameraRig2D;
using Rig::RigState;

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
        std::fprintf(stderr, "[CameraRigTest] FAILED: %s\n", what);
        assert(cond);
    }
}

} // namespace

int main()
{
    // —— 无 shake：合成 position == follow 位置、roll=0 ——
    {
        CameraRig2D rig;
        // follow snap 模式（无死区 + 无平滑）→ 一步到 target。
        Rig::FollowParams fp{};
        fp.smoothTime = 0.0f;
        rig.follow.SetParams(fp);
        rig.SnapTo({0.0f, 0.0f});
        // shake 无 trauma → 零偏移。
        const RigState s = rig.Update({4.0f, -2.0f}, 1.0f / 60.0f);
        Check(Near(s.position.x, 4.0f) && Near(s.position.y, -2.0f), "rig: 无 shake 时 position==follow");
        Check(Near(s.roll, 0.0f), "rig: 无 shake 时 roll=0");
        Check(Near(s.position.x, rig.GetFollowPosition().x), "rig: position 与 follow 一致");
    }

    // —— shake 只叠视觉偏移，不污染 follow 真相 ——
    {
        CameraRig2D rig;
        Rig::FollowParams fp{};
        fp.smoothTime = 0.0f;   // snap → follow 恒 == target
        rig.follow.SetParams(fp);
        rig.SnapTo({0.0f, 0.0f});
        Rig::ShakeParams sp{};
        sp.maxTranslation = {0.5f, 0.5f};
        sp.traumaDecay = 0.0f;  // 不衰减，保持满 trauma
        sp.maxRoll = 0.0f;
        rig.shake.SetParams(sp);
        rig.shake.SetTrauma(1.0f);

        const glm::vec2 target{3.0f, 3.0f};
        float maxDev = 0.0f;
        for (int i = 0; i < 200; ++i)
        {
            const RigState s = rig.Update(target, 1.0f / 60.0f);
            // follow 真相恒 == target（snap），不被 shake 污染。
            Check(Near(rig.GetFollowPosition().x, target.x) && Near(rig.GetFollowPosition().y, target.y),
                  "rig: follow 真相位置不被 shake 污染");
            // 合成 position 相对 target 的偏移 = shake 偏移，量 <= maxTranslation。
            const float devX = std::fabs(s.position.x - target.x);
            const float devY = std::fabs(s.position.y - target.y);
            Check(devX <= sp.maxTranslation.x + 1e-4f && devY <= sp.maxTranslation.y + 1e-4f,
                  "rig: shake 偏移量有界 (<=maxTranslation)");
            maxDev = std::max(maxDev, devX);
        }
        Check(maxDev > 0.2f, "rig: 满 trauma 下合成 position 确有 shake 抖动");
    }

    // —— zoom 合成 + 转发 ——
    {
        CameraRig2D rig;
        Rig::ZoomParams zp{};
        zp.smoothTime = 0.0f;   // snap zoom
        zp.minZoom = 0.1f; zp.maxZoom = 10.0f;
        rig.zoom.SetParams(zp);
        rig.SetTargetZoom(3.0f);                 // 便利转发
        Check(Near(rig.zoom.GetTargetZoom(), 3.0f), "rig: SetTargetZoom 转发到 zoom 子控制器");
        const RigState s = rig.Update({0.0f, 0.0f}, 1.0f / 60.0f);
        Check(Near(s.zoom, 3.0f), "rig: State.zoom == zoom 子控制器当前值");
        Check(Near(s.zoom, rig.zoom.GetZoom()), "rig: zoom 合成一致");
    }

    // —— AddTrauma 转发 ——
    {
        CameraRig2D rig;
        rig.AddTrauma(2.0f);  // 超 1 钳制发生在子控制器
        Check(Near(rig.shake.GetTrauma(), 1.0f), "rig: AddTrauma 转发到 shake 子控制器 (钳到 1)");
    }

    std::printf("[CameraRigTest] OK (%d checks)\n", gChecks);
    return 0;
}
