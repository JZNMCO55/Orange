#ifndef ORANGE_ENGINE_CAMERARIG_CAMERA_FOLLOW_H
#define ORANGE_ENGINE_CAMERARIG_CAMERA_FOLLOW_H

// ---------------------------------------------------------------------------
// CameraFollow2D —— 2D 相机跟随控制器 (deadzone + 临界阻尼平滑 SmoothDamp + 世界边界钳制)。
//
// 纯数据 + 数学：不依赖 Render::Camera / World / GPU，只吃/吐 glm::vec2 世界坐标 + params，
// 由消费者把结果写进自己的相机 (解耦——任何 2D / 2.5D 游戏可复用)。单线程、确定性 (同输入
// 同输出)，故 headless 完全可测。
//
// 每帧 Update(targetPos, dt)：
//   ① deadzone —— 目标在以相机为中心的死区矩形内移动时该轴不追；出死区则把死区边沿贴着目标
//      推 (经典 2D 死区跟随，避免小抖动导致相机不停微动)。
//   ② SmoothDamp —— 相机朝死区解出的期望点做临界阻尼平滑逼近 (Unity SmoothDamp，maxSpeed
//      可选钳速)，无弹跳、无过冲、收敛稳定。
//   ③ bounds —— 相机中心钳到世界边界矩形内 (关卡边缘不露黑边)。
//
// 前瞻 (lookahead，沿运动方向提前量) 是常见第 4 档，本版先不做，留后续按需扩展。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/vec2.hpp>

namespace Orange::Engine::CameraRig
{

// 相机跟随参数。全部有中性默认值：默认无死区 (始终追) + 0.2s 平滑 + 不限速 + 无边界。
struct FollowParams
{
    // 死区半尺寸 (以相机中心为原点的矩形半宽 / 半高)。目标在此矩形内移动相机不追该轴；
    // (0,0) = 无死区 (相机始终精确追目标)。分量取 max(0) 使用，负值当 0。
    glm::vec2 deadzoneHalfExtent{0.0f, 0.0f};
    // SmoothDamp 平滑时间 s (越大越"肉" / 滞后越明显)。<=0 → 相机瞬移到期望点 (无平滑)。
    float     smoothTime = 0.2f;
    // 相机追随最大速度 (世界单位/s)。<=0 = 不限速。
    float     maxSpeed = 0.0f;
    // 世界边界钳制 (相机中心不出此矩形)。hasBounds=false 时忽略 boundsMin/Max；
    // 某轴 min>max 时该轴退化为不钳 (容错，不崩)。
    bool      hasBounds = false;
    glm::vec2 boundsMin{0.0f, 0.0f};
    glm::vec2 boundsMax{0.0f, 0.0f};
};

// 2D 相机跟随控制器。持有当前相机中心 + SmoothDamp 内部速度状态。
class ORANGE_ENGINE_API CameraFollow2D
{
public:
    CameraFollow2D() = default;
    explicit CameraFollow2D(const FollowParams& params) : mParams(params) {}

    void                SetParams(const FollowParams& params) { mParams = params; }
    const FollowParams& GetParams() const noexcept { return mParams; }

    // 硬置相机位置 (并清 SmoothDamp 速度)。用于初始化 / 传送，不走平滑。
    void SetPosition(const glm::vec2& pos) noexcept
    {
        mPosition = pos;
        mVelocity = glm::vec2{0.0f, 0.0f};
    }
    const glm::vec2& GetPosition() const noexcept { return mPosition; }
    const glm::vec2& GetVelocity() const noexcept { return mVelocity; }

    // 推进一帧：给目标世界位置 + dt (秒)，返回并更新新的相机中心世界位置。
    // dt<=0 直接返回当前位置不动 (防除零 / 暂停帧)。
    glm::vec2 Update(const glm::vec2& targetPos, float dt);

private:
    FollowParams mParams{};
    glm::vec2    mPosition{0.0f, 0.0f};
    glm::vec2    mVelocity{0.0f, 0.0f};  // SmoothDamp per-axis 内部速度状态
};

} // namespace Orange::Engine::CameraRig

#endif // ORANGE_ENGINE_CAMERARIG_CAMERA_FOLLOW_H
