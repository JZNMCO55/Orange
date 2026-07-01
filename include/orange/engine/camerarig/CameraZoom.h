#ifndef ORANGE_ENGINE_CAMERARIG_CAMERA_ZOOM_H
#define ORANGE_ENGINE_CAMERARIG_CAMERA_ZOOM_H

// ---------------------------------------------------------------------------
// CameraZoom2D —— 平滑相机缩放控制器 (2D 正交视野 / zoom factor 的临界阻尼逼近)。
//
// 与 CameraFollow2D / CameraShake2D 组成相机工具三件套 (跟随 / 抖动 / 缩放)。给一个目标
// zoom (正交半高、或视野倍率，语义由消费者定，本类只当作被钳到 [min,max] 的标量)，每帧朝
// 它做临界阻尼平滑 (同 CameraFollow 的 SmoothDamp)，用于戏剧化拉近 / Boss 揭示拉远等。
//
// 纯数据 + 数学：header-only 全内联，不依赖 Render::Camera / World / GPU，只吃/吐 float +
// params。确定性 (同输入同输出)，headless 完全可测。消费者把 GetZoom() 结果写进自己相机的
// 正交尺寸 / FOV。故不标 ORANGE_ENGINE_API (纯内联，无跨 DLL 符号)。
// ---------------------------------------------------------------------------

#include <glm/common.hpp>  // glm::clamp

#include <algorithm>

namespace Orange::Engine::CameraRig
{

struct ZoomParams
{
    float smoothTime = 0.25f;  // 临界阻尼平滑时间 s；<=0 → 瞬移到目标 zoom
    float minZoom    = 0.1f;   // zoom 下限 (钳制目标 + 当前值)
    float maxZoom    = 100.0f; // zoom 上限
    float maxSpeed   = 0.0f;   // 缩放最大速度 (zoom 单位/s)；<=0 = 不限速
};

// 平滑缩放控制器。持有当前 zoom + SmoothDamp 内部速度。
class CameraZoom2D
{
public:
    CameraZoom2D() = default;
    explicit CameraZoom2D(const ZoomParams& params) : mParams(params) {}
    CameraZoom2D(const ZoomParams& params, float initialZoom)
        : mParams(params), mZoom(Clamp(initialZoom)), mTargetZoom(Clamp(initialZoom)) {}

    void              SetParams(const ZoomParams& params) { mParams = params; }
    const ZoomParams& GetParams() const noexcept { return mParams; }

    // 设目标 zoom (钳到 [min,max])；相机每帧平滑逼近它。
    void SetTargetZoom(float target) noexcept { mTargetZoom = Clamp(target); }
    float GetTargetZoom() const noexcept { return mTargetZoom; }

    // 硬置当前 zoom (并清速度 + 同步目标)。用于初始化 / 瞬切。
    void SetZoom(float zoom) noexcept
    {
        mZoom       = Clamp(zoom);
        mTargetZoom = mZoom;
        mVelocity   = 0.0f;
    }
    float GetZoom() const noexcept { return mZoom; }
    float GetVelocity() const noexcept { return mVelocity; }

    // 推进一帧：朝目标 zoom 平滑逼近，返回并更新当前 zoom。dt<=0 → 不动。
    float Update(float dt) noexcept
    {
        if (dt <= 0.0f)
        {
            return mZoom;
        }
        if (mParams.smoothTime <= 0.0f)
        {
            mZoom     = mTargetZoom;  // 瞬移
            mVelocity = 0.0f;
        }
        else
        {
            mZoom = SmoothDamp(mZoom, mTargetZoom, mVelocity, mParams.smoothTime,
                               mParams.maxSpeed, dt);
        }
        // 保险再钳一次 (params 变更 / 数值漂移防越界)。
        mZoom = Clamp(mZoom);
        return mZoom;
    }

private:
    float Clamp(float v) const noexcept
    {
        // min>max 容错：退化为不钳 (返回原值)。
        if (mParams.minZoom > mParams.maxZoom)
        {
            return v;
        }
        return glm::clamp(v, mParams.minZoom, mParams.maxZoom);
    }

    // Unity SmoothDamp (临界阻尼) 单标量，与 CameraFollow 同款闭式解 (故 zoom / 位置手感一致)。
    static float SmoothDamp(float current, float target, float& velocity, float smoothTime,
                            float maxSpeed, float dt) noexcept
    {
        smoothTime = std::max(smoothTime, 1e-4f);
        const float omega = 2.0f / smoothTime;
        const float x  = omega * dt;
        const float ex = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        float       change     = current - target;
        const float originalTo = target;
        if (maxSpeed > 0.0f)
        {
            const float maxChange = maxSpeed * smoothTime;
            change = std::clamp(change, -maxChange, maxChange);
        }
        const float t      = current - change;
        const float temp   = (velocity + omega * change) * dt;
        velocity           = (velocity - omega * temp) * ex;
        float       output = t + (change + temp) * ex;
        if ((originalTo - current > 0.0f) == (output > originalTo))
        {
            output   = originalTo;
            velocity = (output - originalTo) / dt;
        }
        return output;
    }

    ZoomParams mParams{};
    float      mZoom       = 1.0f;
    float      mTargetZoom = 1.0f;
    float      mVelocity   = 0.0f;
};

} // namespace Orange::Engine::CameraRig

#endif // ORANGE_ENGINE_CAMERARIG_CAMERA_ZOOM_H
