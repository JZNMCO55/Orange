#ifndef ORANGE_ENGINE_STEERING_STEERING_H
#define ORANGE_ENGINE_STEERING_STEERING_H

// ---------------------------------------------------------------------------
// Steering —— 2D 转向行为库 (Reynolds steering behaviors)，header-only。
//
// 局部自主运动原语：每个行为吐一个"转向力" (steering force = 期望速度 - 当前速度，
// 钳到 maxForce)，消费者把力交给 Agent::Integrate 做半隐式欧拉积分 + maxSpeed 钳速。
// Seek / Flee 全速趋避、Arrive 到点平滑刹停 (无过冲)、Pursue / Evade 预测拦截、
// Wander 随机游走。与 nav 分工：nav 出全局路径 (A*)，steering 做逐帧局部跟随 / 避让。
//
// 纯 glm + 数学，确定性 (Wander 的随机复用 Noise::Rng，同 seed+state → 同结果)，
// 不依赖 World / GPU / GLFW，故 headless 完全可测。所有 Normalize 对零向量做守卫
// (返回零、不产生 NaN)，退化输入 (target==pos / vel==0 / maxForce<=0) 均有定义行为。
// ---------------------------------------------------------------------------

#include <orange/engine/noise/Rng.h>

#include <glm/vec2.hpp>

#include <cmath>

namespace Orange::Engine::Steering
{

namespace Detail
{

inline float Length(const glm::vec2& v) noexcept
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

// 单位化：零向量 (或近零) 返回零，避免 NaN。
inline glm::vec2 Normalized(const glm::vec2& v) noexcept
{
    const float len = Length(v);
    return len > 1e-8f ? v * (1.0f / len) : glm::vec2{0.0f, 0.0f};
}

} // namespace Detail

// 转向参数。maxSpeed 钳制积分后的速度；maxForce 钳制每帧转向力 (越大越敏捷 / 转向越急)。
struct SteeringParams
{
    float maxSpeed = 10.0f;
    float maxForce = 40.0f;
};

// 把向量长度钳到 maxLen (保方向)。maxLen<=0 → 零向量；零向量原样返回。
inline glm::vec2 Truncate(const glm::vec2& v, float maxLen) noexcept
{
    if (maxLen <= 0.0f)
    {
        return glm::vec2{0.0f, 0.0f};
    }
    const float len = Detail::Length(v);
    return (len > maxLen && len > 1e-8f) ? v * (maxLen / len) : v;
}

// 期望速度 → 转向力：force = truncate(desiredVel - currentVel, maxForce)。
inline glm::vec2 DesiredForce(const glm::vec2& desiredVel, const glm::vec2& currentVel, float maxForce) noexcept
{
    return Truncate(desiredVel - currentVel, maxForce);
}

// Seek：朝 target 全速趋近。target==pos 时期望速度为零 → 相当于刹车 (力 = -vel 钳制)。
inline glm::vec2 Seek(const glm::vec2& pos, const glm::vec2& vel, const glm::vec2& target,
                      const SteeringParams& p) noexcept
{
    const glm::vec2 desired = Detail::Normalized(target - pos) * p.maxSpeed;
    return DesiredForce(desired, vel, p.maxForce);
}

// Flee：背离 target 全速逃离。
inline glm::vec2 Flee(const glm::vec2& pos, const glm::vec2& vel, const glm::vec2& target,
                      const SteeringParams& p) noexcept
{
    const glm::vec2 desired = Detail::Normalized(pos - target) * p.maxSpeed;
    return DesiredForce(desired, vel, p.maxForce);
}

// Arrive：趋近 target 且在 slowRadius 内线性减速，到点平滑停下 (无过冲)。
// slowRadius<=0 → 退化为 Seek (无减速带)。dist≈0 → 刹车。
inline glm::vec2 Arrive(const glm::vec2& pos, const glm::vec2& vel, const glm::vec2& target,
                        const SteeringParams& p, float slowRadius) noexcept
{
    const glm::vec2 toTarget = target - pos;
    const float     dist = Detail::Length(toTarget);
    if (dist < 1e-6f)
    {
        return DesiredForce(glm::vec2{0.0f, 0.0f}, vel, p.maxForce); // 到点刹车
    }
    float speed = p.maxSpeed;
    if (slowRadius > 0.0f && dist < slowRadius)
    {
        speed = p.maxSpeed * (dist / slowRadius); // 减速带内线性收速
    }
    const glm::vec2 desired = (toTarget / dist) * speed;
    return DesiredForce(desired, vel, p.maxForce);
}

// Pursue：预测移动目标 lookahead 秒后的位置再 Seek (拦截而非追尾)。
inline glm::vec2 Pursue(const glm::vec2& pos, const glm::vec2& vel, const glm::vec2& targetPos,
                        const glm::vec2& targetVel, const SteeringParams& p, float lookahead) noexcept
{
    return Seek(pos, vel, targetPos + targetVel * lookahead, p);
}

// Evade：预测移动威胁 lookahead 秒后的位置再 Flee。
inline glm::vec2 Evade(const glm::vec2& pos, const glm::vec2& vel, const glm::vec2& threatPos,
                       const glm::vec2& threatVel, const SteeringParams& p, float lookahead) noexcept
{
    return Flee(pos, vel, threatPos + threatVel * lookahead, p);
}

// Wander 的持久状态：投影圆上的游走角 (弧度)。跨帧保留使朝向连续、不抖。
struct WanderState
{
    float angle = 0.0f;
};

// Wander：沿速度前方投影一个圆，在圆上取一点 (角度每帧被 jitter 随机扰动) 作为 Seek 目标。
// vel≈0 时默认朝 +X 投影。确定性来自传入的 Noise::Rng (同 seed+state → 同结果)。
inline glm::vec2 Wander(const glm::vec2& pos, const glm::vec2& vel, WanderState& state,
                        Noise::Rng& rng, float circleDist, float circleRadius, float jitter,
                        const SteeringParams& p) noexcept
{
    state.angle += rng.NextFloatRange(-jitter, jitter);
    glm::vec2 heading = Detail::Normalized(vel);
    if (heading.x == 0.0f && heading.y == 0.0f)
    {
        heading = glm::vec2{1.0f, 0.0f};
    }
    const glm::vec2 circleCenter = pos + heading * circleDist;
    const glm::vec2 offset{std::cos(state.angle) * circleRadius, std::sin(state.angle) * circleRadius};
    return Seek(pos, vel, circleCenter + offset, p);
}

// 受转向力驱动的可移动体。转向力当加速度 (质量 1)，半隐式欧拉积分 + maxSpeed 钳速。
struct Agent
{
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 velocity{0.0f, 0.0f};

    // 施加一帧转向力。dt<=0 不动 (暂停帧 / 防除零)。
    void Integrate(const glm::vec2& force, const SteeringParams& p, float dt) noexcept
    {
        if (dt <= 0.0f)
        {
            return;
        }
        velocity += force * dt;
        velocity = Truncate(velocity, p.maxSpeed);
        position += velocity * dt;
    }
};

} // namespace Orange::Engine::Steering

#endif // ORANGE_ENGINE_STEERING_STEERING_H
