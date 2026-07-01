#ifndef ORANGE_ENGINE_CAMERARIG_CAMERA_RIG_H
#define ORANGE_ENGINE_CAMERARIG_CAMERA_RIG_H

// ---------------------------------------------------------------------------
// CameraRig2D —— 组合 follow + shake + zoom 的 2D 相机装置 (rig)。
//
// 把 CameraFollow2D / CameraShake2D / CameraZoom2D 三原语聚合成游戏侧实际持有、每帧驱动的
// 单一对象：一次 Update(targetPos, dt) 顺序跑三者并合成相机完整状态 (含抖动的中心位置 /
// roll / zoom)，消费者拿 State 写进自己的相机 (正交中心 + 旋转 + 尺寸)。三个子控制器作为
// 公共成员直接暴露，消费者按需配置各自的 params (SetParams) 或调用便利转发。
//
// header-only 纯聚合 + 内联，不依赖 Render::Camera / World / GPU，确定性、headless 可测。
// 组合顺序：base = follow.Update() → position = base + shake.translation → zoom.Update()。
// shake 只叠加视觉偏移，不污染 follow 的 gameplay 真相位置 (与 spike 分层理念一致)。
// ---------------------------------------------------------------------------

#include <orange/engine/camerarig/CameraFollow.h>
#include <orange/engine/camerarig/CameraShake.h>
#include <orange/engine/camerarig/CameraZoom.h>

#include <glm/vec2.hpp>

namespace Orange::Engine::CameraRig
{

// 一帧合成的相机完整状态。
struct RigState
{
    glm::vec2 position{0.0f, 0.0f};  // 含 shake 偏移的相机中心 (世界坐标)
    float     roll = 0.0f;           // shake 的 roll (弧度；2.5D 用)
    float     zoom = 1.0f;           // 当前 zoom
};

// 组合相机装置。三个子控制器公共暴露，直接配置 / 查询。
class CameraRig2D
{
public:
    CameraFollow2D follow;
    CameraShake2D  shake;
    CameraZoom2D   zoom;

    // 一帧：follow 追 target → 叠加 shake 偏移 → zoom 平滑。返回相机完整状态。
    RigState Update(const glm::vec2& targetPos, float dt)
    {
        const glm::vec2   base = follow.Update(targetPos, dt);
        const ShakeOffset s    = shake.Update(dt);
        const float       z    = zoom.Update(dt);
        RigState out{};
        out.position = base + s.translation;  // shake 只叠加视觉偏移
        out.roll     = s.roll;
        out.zoom     = z;
        return out;
    }

    // —— 便利转发 (常用触发，免消费者深入子控制器) ——
    void AddTrauma(float amount) noexcept { shake.AddTrauma(amount); }     // 触发抖动 (受击/爆炸)
    void SetTargetZoom(float target) noexcept { zoom.SetTargetZoom(target); } // 目标缩放
    // 硬置相机到某点 (follow 瞬移 + 清 follow 速度)，用于初始化 / 传送。
    void SnapTo(const glm::vec2& pos) noexcept { follow.SetPosition(pos); }
    // 当前 follow 中心 (不含 shake 偏移的 gameplay 真相位置)。
    const glm::vec2& GetFollowPosition() const noexcept { return follow.GetPosition(); }
};

} // namespace Orange::Engine::CameraRig

#endif // ORANGE_ENGINE_CAMERARIG_CAMERA_RIG_H
