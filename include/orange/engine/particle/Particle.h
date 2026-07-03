#ifndef ORANGE_ENGINE_PARTICLE_PARTICLE_H
#define ORANGE_ENGINE_PARTICLE_PARTICLE_H

// ---------------------------------------------------------------------------
// Particle —— 单个 2D 粒子的运行时状态 (POD)，header-only。
//
// 只描述"一个粒子此刻是什么样"：位置 / 速度 / 年龄 / 寿命 / 旋转，外加烘焙进
// 粒子的 size-over-life 与 color-over-life 端点 (begin/end)。这些端点在发射时由
// ParticleSystem 从 EmitterConfig 复制 (+ 可选抖动) 进来，之后按归一化年龄插值，
// 使粒子自描述——渲染 / 查询侧不必回头依赖发射器配置。
//
// 纯数据 + 数学，确定性、单线程、无 GPU/GLFW 依赖，故 headless 完全可测。
// size 插值复用 tween 缓动曲线；color 也可选缓动 (默认线性，alpha 常用作淡出)。
// ---------------------------------------------------------------------------

#include <orange/engine/tween/Easing.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace Orange::Engine::Particle
{

    // 单个粒子的运行时状态。存活粒子由 ParticleSystem 紧凑保存在池的前部。
    struct Particle
    {
        glm::vec2 position{0.0f, 0.0f};               // 世界位置
        glm::vec2 velocity{0.0f, 0.0f};               // 世界速度 (单位/s)
        float     age             = 0.0f;             // 已存活秒数
        float     lifetime        = 1.0f;             // 总寿命秒 (>0；<=0 视为出生即耗尽)
        float     rotation        = 0.0f;             // 朝向弧度
        float     angularVelocity = 0.0f;             // 自转角速度 (弧度/s)
        float     sizeBegin       = 1.0f;             // 出生尺寸
        float     sizeEnd         = 0.0f;             // 死亡尺寸
        glm::vec4 colorBegin{1.0f, 1.0f, 1.0f, 1.0f}; // 出生 RGBA
        glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};   // 死亡 RGBA

        // 归一化寿命进度 [0,1]。lifetime<=0 视为已耗尽 (返回 1)，避免除零。
        float NormalizedAge() const noexcept
        {
            if (lifetime <= 0.0f)
            {
                return 1.0f;
            }
            const float t = age / lifetime;
            return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        }

        // 是否寿终 (年龄追平寿命)。ParticleSystem::Update 用它做淘汰判定。
        bool IsExpired() const noexcept { return age >= lifetime; }

        // 当前尺寸：sizeBegin→sizeEnd 按缓动曲线随归一化年龄插值。
        float SizeAt(Tween::EaseType ease = Tween::EaseType::Linear) const noexcept
        {
            return Tween::EaseLerp(ease, sizeBegin, sizeEnd, NormalizedAge());
        }

        // 当前颜色：colorBegin→colorEnd 按缓动曲线插值 (逐分量，含 alpha)。
        glm::vec4 ColorAt(Tween::EaseType ease = Tween::EaseType::Linear) const noexcept
        {
            const float t = Tween::Ease(ease, NormalizedAge());
            return colorBegin + (colorEnd - colorBegin) * t;
        }
    };

} // namespace Orange::Engine::Particle

#endif // ORANGE_ENGINE_PARTICLE_PARTICLE_H
