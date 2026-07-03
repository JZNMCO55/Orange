#ifndef ORANGE_ENGINE_RENDER_PARTICLE_EMITTER_COMPONENT_H
#define ORANGE_ENGINE_RENDER_PARTICLE_EMITTER_COMPONENT_H

// ---------------------------------------------------------------------------
// ParticleEmitterComponent —— ECS 上的"这个 entity 是粒子发射器"标记 + desc。
//
// 与 RigidBodyComponent / DirectionalLight 同节奏的 trivially-copyable POD：
// 只承载发射参数；运行时粒子池的 ownership 在 `VfxSystem` 内（按 Entity
// 索引），不放在 component 上——这样 component 行迁移成本低、scene 序
// 列化只 round-trip 数据 desc 而不触碰运行时状态。
//
// `emitting` 字段允许 game 代码暂停 / 恢复发射而不销毁粒子池——典型
// 场景：玩家暂停时仍保留已经在场上的粒子，不再产生新的。
//
// 当前设计的取舍说明：
//   * 颜色 / 大小走"start → end 线性插值"（按 age01 lerp），曲线 / spline
//     表留给将来编辑器接 ImGui curve editor 时再加；
//   * 发射位置在 entity 本地空间，VfxSystem 在 spawn 时把 entity 的
//     TransformComponent.position（取 xy 两轴）作 origin 偏移；3D 位置
//     的 z 维度暂不参与 sim（screen-aligned 2D 粒子）；
//   * 单 emitter 上限 `maxParticles`，溢出 spawn 直接 drop（不抢占老
//     粒子，更可预期）；
//   * 颜色字段是 `vec4` —— rgb 是颜色，a 用作 intensity / 强度乘子。
//     a > 1 时颜色超出 LDR 阈值，自动被 bloom pass 拾取为发光面。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>

namespace Orange::Engine::Render
{

    // 发射器的 schema-friendly desc。所有字段都是值类型 POD——用于 ECS
    // 组件内联 + scene 序列化 1:1 映射。
    struct ParticleEmitterDesc
    {
        // 每秒发射多少粒子。0 → 不发射；负数视作 0（实现端 clamp）。
        float emissionRate{20.0f};

        // 单个粒子的寿命范围（秒）。spawn 时在 [min, max] 区间均匀采样。
        float lifetimeMin{1.0f};
        float lifetimeMax{2.0f};

        // 相对 entity 本地空间的 spawn offset 范围。对每个粒子在矩形
        // [min, max] 内均匀采样位置偏移。
        glm::vec2 spawnOffsetMin{0.0f, 0.0f};
        glm::vec2 spawnOffsetMax{0.0f, 0.0f};

        // 初始速度采样范围（worldspace m/s）。
        glm::vec2 initialVelocityMin{-0.5f, 1.0f};
        glm::vec2 initialVelocityMax{0.5f, 2.0f};

        // 每帧持续作用在每个粒子上的加速度（m/s^2）。地表 fountain 典型值
        // (0, -9.8)；漂浮烟雾用 (0, +0.4)。
        glm::vec2 gravity{0.0f, -2.0f};

        // 颜色 / 大小的 start / end 端点。按粒子 age01 = age / lifetime 在
        // 两端点之间线性插值（age01=0 → start，age01=1 → end）。
        // color.a > 1 让 bloom 自动拾取发光面。
        glm::vec4 colorStart{1.0f, 0.85f, 0.35f, 1.4f};
        glm::vec4 colorEnd{1.0f, 0.20f, 0.05f, 0.0f};

        float sizeStart{0.06f};
        float sizeEnd{0.10f};

        // 单 emitter 池的硬上限。spawn 满了之后新粒子直接 drop，不抢占老
        // 的——更可预期、避免"亮粒子在视野中央被新粒子顶掉"的视觉跳变。
        std::uint32_t maxParticles{256};
    };

    struct ParticleEmitterComponent
    {
        ParticleEmitterDesc desc{};

        // 发射开关——pause / resume 不销毁粒子池：已存在的粒子继续 sim 直
        // 到自然 lifetime 结束，但不再产生新粒子。
        bool emitting{true};
    };

} // namespace Orange::Engine::Render

#endif // ORANGE_ENGINE_RENDER_PARTICLE_EMITTER_COMPONENT_H
