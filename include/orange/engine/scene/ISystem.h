#ifndef ORANGE_ENGINE_SCENE_I_SYSTEM_H
#define ORANGE_ENGINE_SCENE_I_SYSTEM_H

// ---------------------------------------------------------------------------
// ISystem —— "在 World 上每帧跑一段逻辑"的抽象。
//
// 与 App::Layer 的区别：
//   * Layer 是面向"主循环阶段"的钩子（事件分发、UI、debug overlay 等
//     横切关注点），生命周期与 AppHost 等长；
//   * System 是面向"World 上某种数据管线"的更新单元（物理 step、动
//     画推进、变换求 final matrix 等），生命周期通常等于 World，按依
//     赖关系排序、按帧 tick。
//
// Phase 2 / Task 03 仅定义最薄的接口；后续 Phase 4 起会引入
// SystemScheduler（或类似）来管理 system 间的依赖、阶段、并行机会。
// 当前不预设任何调度语义——OnUpdate 由 World 持有方按自己需要的顺
// 序串行调用。
//
// FrameContext 复用 App 模块中已有的定义（已在 Orange::Engine 顶层
// 命名空间）：单帧只读快照，对 system 来说足够。
// ---------------------------------------------------------------------------

#include <orange/engine/app/FrameContext.h>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Scene
{

class ISystem
{
public:
    virtual ~ISystem() = default;

    ISystem() = default;
    ISystem(const ISystem&) = delete;
    ISystem& operator=(const ISystem&) = delete;
    ISystem(ISystem&&) noexcept = delete;
    ISystem& operator=(ISystem&&) noexcept = delete;

    // 对应 World 把自己挂载 / 卸载 system 的两个生命周期点。允许子
    // 类在 OnAttach 里登记 component 索引、预分配缓存；OnDetach 释
    // 放本 system 自己持有的资源（不应反向卸载 World 上的 component）。
    virtual void OnAttach(::Orange::Engine::World& /*world*/) {}
    virtual void OnDetach(::Orange::Engine::World& /*world*/) {}

    // 每帧调用一次。`world` 是被本 system 操作的目标；`frame` 是只
    // 读快照（dt / 帧序号 / framebuffer 尺寸等）。
    virtual void OnUpdate(::Orange::Engine::World& world,
                          const FrameContext& frame) = 0;
};

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_I_SYSTEM_H
