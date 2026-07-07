#ifndef ORANGE_ENGINE_GAME_I_GAME_MODULE_H
#define ORANGE_ENGINE_GAME_I_GAME_MODULE_H

// ---------------------------------------------------------------------------
// IGameModule —— PIE 双语言玩法宿主的语言无关生命周期接口（ADR-021）。
//
// 定位：游戏（C++ 或经 C# ScriptSystem 包装）把自己实现成一个 IGameModule，
// 由「宿主」驱动。宿主有两种，共用本接口：
//   * 编辑器宿主（orange_editor）—— 挂进 Play 状态机（ApplyPendingPlayOp），
//     支持 edit-while-play + Stop 还原。
//   * 发布 runtime 宿主（orange_runtime，M10）—— 瘦 main，加载场景后直接跑。
//
// 接口两段式：
//   * 注册期（宿主启动即调，Edit 态就生效）：RegisterRenderPasses 把自定义
//     IRenderPass 插进 Pipeline；ComponentSerializers 交出游戏组件的 scene
//     序列化器（宿主收集后填进 Scene::Save/LoadOptions.extraSerializers，令
//     含游戏组件的场景可 round-trip）。
//   * Play 生命周期（宿主的 EnterPlay / Tick / Stop 序列各调一次）：
//     OnEnterPlay 建运行期状态、Tick 推进模拟、OnEvent 收视口聚焦时的输入、
//     OnExitPlay 拆卸。
//
// 刻意**不含** RegisterSchemas —— schema 是编辑器 Inspector 侧概念（引擎
// runtime 无 schema registry），游戏组件的 Inspector 注册在编辑器层单独接
// （见 pie-gamemodule-roadmap.md M3/M5），不污染引擎 runtime 接口。
//
// 头文件隔离：本头只 include 无第三方依赖的公共头（WindowEvent /
// ComponentSerializerEntry）+ 前向声明重量级类型（World / PhysicsWorld /
// Pipeline / AssetRegistry），不引入任何 RHI / 第三方头。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/scene/ComponentSerializerEntry.h>

#include <span>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Asset
{
    class AssetRegistry;
}

namespace Orange::Engine::Physics
{
    class PhysicsWorld;
}

namespace Orange::Engine::Render
{
    class Pipeline;
}

namespace Orange::Engine::Game
{

    // Play 生命周期回调透传的宿主资源。各指针宿主装配，可空——模块应对
    // nullptr graceful 退化（headless / 无相机 / 无物理场景）。
    struct GameModuleContext
    {
        World*                 pWorld{nullptr};
        Physics::PhysicsWorld* pPhysics{nullptr};   // Play 期宿主装配；模块可在 OnEnterPlay 加自己的 body
        Asset::AssetRegistry*  pAssets{nullptr};
        Render::Pipeline*      pPipeline{nullptr};
    };

    // 游戏玩法模块。宿主经 unique_ptr 持有（不可拷贝 / 移动）。所有虚函数
    // 除 Name 外都有 no-op 默认实现——模块只覆写自己需要的段。
    class ORANGE_ENGINE_API IGameModule
    {
    public:
        virtual ~IGameModule() = default;

        IGameModule()                              = default;
        IGameModule(const IGameModule&)            = delete;
        IGameModule& operator=(const IGameModule&) = delete;
        IGameModule(IGameModule&&)                 = delete;
        IGameModule& operator=(IGameModule&&)      = delete;

        // 调试 / 日志用名称。宿主不做去重。
        virtual const char* Name() const noexcept = 0;

        // ---- 注册期（宿主启动即调，Edit 态生效）--------------------------

        // 把自定义 IRenderPass 经 pipeline.InsertPass 接进帧路径（典型：
        // SlimeMetaballPass 走 AfterMainPass）。宿主保证 Edit 态就调，故 pass
        // 常驻；pass 无数据时自己在 Execute 里早退。
        virtual void RegisterRenderPasses(Render::Pipeline& pipeline)
        {
            (void)pipeline;
        }

        // RegisterRenderPasses 的对偶：把注册的 pass 从 pipeline 摘除。DLL 宿主
        // 的 session 级热重载 / 项目切换时，宿主在**卸载 game.dll 前**调——pass
        // 对象的 vtable 与代码都在 dll 内，若 FreeLibrary 后 Pipeline 仍持有该
        // pass，之后每帧 Execute 或 Pipeline 析构会跳进已卸载的 dll 代码 → 崩溃。
        // 默认 no-op（无 pass 的模块 / 静态链模块无需实现）。
        virtual void UnregisterRenderPasses(Render::Pipeline& pipeline)
        {
            (void)pipeline;
        }

        // 交出游戏组件的 scene 序列化器。返回的 span 指向模块自己持有的静态 /
        // 成员存储，须活到宿主不再 Save/Load 为止。宿主收集所有模块的条目，
        // 填进 Scene::Save/LoadOptions.extraSerializers。默认无。
        virtual std::span<const Scene::ComponentSerializerEntry> ComponentSerializers() const
        {
            return {};
        }

        // ---- Play 生命周期（宿主 EnterPlay / Tick / Stop 序列各调一次）----

        // 进 Play：建运行期状态（Blob 复位、control point 建 body 等）。
        virtual void OnEnterPlay(GameModuleContext& ctx)
        {
            (void)ctx;
        }

        // 模块是否自管物理 step（spike-01 在自管固定步长 accumulator 内自己
        // Step PhysicsWorld）。返回 true 时宿主让位、不再自动 step（ADR-021
        // 开放问题①）。默认 false = 宿主负责 step。
        virtual bool WantsOwnPhysicsStep() const noexcept
        {
            return false;
        }

        // 每帧推进模拟。dt 为宿主帧 dt；模块可在内部再做固定步长细分。
        virtual void Tick(GameModuleContext& ctx, float dt)
        {
            (void)ctx;
            (void)dt;
        }

        // 视口聚焦时宿主把窗口事件路由进来（键鼠喂 InputContext）。失焦时
        // 宿主不转发（走编辑器 fly-cam / gizmo）。
        virtual void OnEvent(const Platform::WindowEvent& event)
        {
            (void)event;
        }

        // 退 Play：拆卸运行期状态。宿主随后从快照还原 World。
        virtual void OnExitPlay(GameModuleContext& ctx)
        {
            (void)ctx;
        }
    };

} // namespace Orange::Engine::Game

#endif // ORANGE_ENGINE_GAME_I_GAME_MODULE_H
