#include "orange/engine/camerarig/CameraFollow.h"

#include <algorithm>

namespace Orange::Engine::CameraRig
{
    namespace
    {

        // Unity SmoothDamp (临界阻尼平滑) 单轴实现：current 朝 target 收敛，velocity 为 in/out
        // 内部速度状态。近似临界阻尼弹簧的闭式解，无弹跳无过冲、收敛时间由 smoothTime 决定。
        float SmoothDampAxis(float current, float target, float& velocity, float smoothTime,
                             float maxSpeed, float dt)
        {
            // smoothTime 下限护栏 (避免 omega 爆炸 / 除零)。
            smoothTime        = std::max(smoothTime, 1e-4f);
            const float omega = 2.0f / smoothTime;
            const float x     = omega * dt;
            const float ex    = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

            float       change     = current - target;
            const float originalTo = target;

            // maxSpeed 钳制 (maxSpeed<=0 视为不限速)：限制单次可移动的最大 change。
            if (maxSpeed > 0.0f)
            {
                const float maxChange = maxSpeed * smoothTime;
                change                = std::clamp(change, -maxChange, maxChange);
            }

            const float t    = current - change; // 钳后等效 target
            const float temp = (velocity + omega * change) * dt;
            velocity         = (velocity - omega * temp) * ex;
            float output     = t + (change + temp) * ex;

            // 防过冲：若越过 originalTo 就贴住 (并把速度设成"恰好停在目标"的斜率)。
            if ((originalTo - current > 0.0f) == (output > originalTo))
            {
                output   = originalTo;
                velocity = (output - originalTo) / dt;
            }
            return output;
        }

        // 单轴死区：目标相对相机的偏移超过死区半径就把相机推到"目标恰在死区边沿"；死区内不动。
        float DeadzoneDesiredAxis(float camPos, float targetPos, float halfExtent)
        {
            const float delta = targetPos - camPos;
            if (delta > halfExtent)
            {
                return targetPos - halfExtent;
            }
            if (delta < -halfExtent)
            {
                return targetPos + halfExtent;
            }
            return camPos; // 死区内 → 该轴不追
        }

    } // namespace

    glm::vec2 CameraFollow2D::Update(const glm::vec2& targetPos, float dt)
    {
        if (dt <= 0.0f)
        {
            return mPosition; // 暂停帧 / 非法 dt：不动
        }

        // ① 死区：算出相机"想去"的期望中心 (per-axis，负 halfExtent 当 0)。
        const glm::vec2 desired{
            DeadzoneDesiredAxis(mPosition.x, targetPos.x, std::max(0.0f, mParams.deadzoneHalfExtent.x)),
            DeadzoneDesiredAxis(mPosition.y, targetPos.y, std::max(0.0f, mParams.deadzoneHalfExtent.y)),
        };

        // ② SmoothDamp 平滑逼近；smoothTime<=0 → 瞬移到期望点 (清速度)。
        if (mParams.smoothTime <= 0.0f)
        {
            mPosition = desired;
            mVelocity = glm::vec2{0.0f, 0.0f};
        }
        else
        {
            mPosition.x = SmoothDampAxis(mPosition.x, desired.x, mVelocity.x,
                                         mParams.smoothTime, mParams.maxSpeed, dt);
            mPosition.y = SmoothDampAxis(mPosition.y, desired.y, mVelocity.y,
                                         mParams.smoothTime, mParams.maxSpeed, dt);
        }

        // ③ 世界边界钳制 (相机中心不出矩形；某轴 min>max 时该轴退化不钳)。
        if (mParams.hasBounds)
        {
            if (mParams.boundsMin.x <= mParams.boundsMax.x)
            {
                mPosition.x = std::clamp(mPosition.x, mParams.boundsMin.x, mParams.boundsMax.x);
            }
            if (mParams.boundsMin.y <= mParams.boundsMax.y)
            {
                mPosition.y = std::clamp(mPosition.y, mParams.boundsMin.y, mParams.boundsMax.y);
            }
        }
        return mPosition;
    }

} // namespace Orange::Engine::CameraRig
