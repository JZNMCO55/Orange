// GameModuleHost / IGameModule 生命周期契约测试（ADR-021 M2）。
//
// headless、不接 GPU（Pipeline 默认构造即可测 InsertPass 扇出，与
// InsertPassTest 同款）。覆盖：
//   * AddModule 保序 + ModuleCount；nullptr 静默忽略。
//   * RegisterRenderPasses 扇出到 Pipeline（每个模块的 pass 都插进去）。
//   * CollectSerializers flatten 所有模块的条目。
//   * AnyWantsOwnPhysicsStep 聚合。
//   * Play 生命周期扇出 + 护栏：Tick/OnEvent/ExitPlay 在未 EnterPlay 时 no-op；
//     重复 EnterPlay no-op；ExitPlay 逆序；GameModuleContext 字段透传到模块。

#include <orange/engine/game/GameModuleHost.h>
#include <orange/engine/game/IGameModule.h>
#include <orange/engine/render/IRenderPass.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderPassContext.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using Orange::Engine::World;
using Orange::Engine::Game::GameModuleContext;
using Orange::Engine::Game::GameModuleHost;
using Orange::Engine::Game::IGameModule;
using Orange::Engine::Render::IRenderPass;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PipelineStage;
using Orange::Engine::Render::RenderGraphBuilder;
using Orange::Engine::Render::RenderPassContext;
using Orange::Engine::Scene::ComponentSerializerEntry;

namespace
{

    // 插进 Pipeline 的最小 pass（验证 RegisterRenderPasses 扇出真调到 InsertPass）。
    struct DummyPass : public IRenderPass
    {
        const char* Name() const noexcept override { return "dummy"; }
        void        Setup(RenderGraphBuilder& /*b*/) override {}
        void        Execute(RenderPassContext& /*c*/) override {}
    };

    // 记录生命周期调用序列 + 透传的 ctx，供断言。
    struct RecordingModule : public IGameModule
    {
        std::string               tag;
        std::vector<std::string>* pLog{nullptr};
        bool                      ownPhysics{false};
        std::size_t               serializerCount{0};

        std::vector<ComponentSerializerEntry> serializers;
        int                                   tickCount{0};
        World*                                seenWorld{nullptr};

        RecordingModule(std::string t, std::vector<std::string>* log,
                        bool own, std::size_t serCount)
            : tag(std::move(t)), pLog(log), ownPhysics(own), serializerCount(serCount)
        {
            serializers.resize(serializerCount); // 值初始化，仅供 count 断言
        }

        const char* Name() const noexcept override { return tag.c_str(); }

        void RegisterRenderPasses(Pipeline& pipeline) override
        {
            pipeline.InsertPass(PipelineStage::AfterMainPass, std::make_unique<DummyPass>());
        }

        std::span<const ComponentSerializerEntry> ComponentSerializers() const override
        {
            return std::span<const ComponentSerializerEntry>(serializers.data(), serializers.size());
        }

        bool WantsOwnPhysicsStep() const noexcept override { return ownPhysics; }

        void OnEnterPlay(GameModuleContext& ctx) override
        {
            seenWorld = ctx.pWorld;
            if (pLog)
                pLog->push_back(tag + ":enter");
        }

        void Tick(GameModuleContext& /*ctx*/, float /*dt*/) override
        {
            ++tickCount;
            if (pLog)
                pLog->push_back(tag + ":tick");
        }

        void OnEvent(const Orange::Engine::Platform::WindowEvent& /*e*/) override
        {
            if (pLog)
                pLog->push_back(tag + ":event");
        }

        void OnExitPlay(GameModuleContext& /*ctx*/) override
        {
            if (pLog)
                pLog->push_back(tag + ":exit");
        }
    };

    void TestAddAndNullptr()
    {
        GameModuleHost host;
        assert(host.ModuleCount() == 0);
        assert(!host.IsInPlay());
        assert(host.AddModule(nullptr) == nullptr); // 静默忽略
        assert(host.ModuleCount() == 0);

        std::vector<std::string> log;
        IGameModule* raw = host.AddModule(std::make_unique<RecordingModule>("a", &log, false, 0));
        assert(raw != nullptr);
        assert(host.ModuleCount() == 1);

        std::fprintf(stdout, "  [PASS] AddModule 保序 + nullptr 忽略\n");
    }

    void TestRegisterFanOut()
    {
        GameModuleHost          host;
        std::vector<std::string> log;
        host.AddModule(std::make_unique<RecordingModule>("a", &log, false, 2));
        host.AddModule(std::make_unique<RecordingModule>("b", &log, true, 3));

        // RegisterRenderPasses 扇出：两个模块各插一个 AfterMainPass pass。
        Pipeline pipeline;
        host.RegisterRenderPasses(pipeline);
        assert(pipeline.InsertedPassCount(PipelineStage::AfterMainPass) == 2);

        // CollectSerializers flatten：2 + 3 = 5 条。
        auto sers = host.CollectSerializers();
        assert(sers.size() == 5);

        // 任一模块自管物理即 true（b=true）。
        assert(host.AnyWantsOwnPhysicsStep());

        std::fprintf(stdout, "  [PASS] RegisterRenderPasses / CollectSerializers / "
                             "AnyWantsOwnPhysicsStep 扇出\n");
    }

    void TestLifecycleGuardsAndOrder()
    {
        GameModuleHost           host;
        std::vector<std::string> log;
        auto* a = static_cast<RecordingModule*>(
            host.AddModule(std::make_unique<RecordingModule>("a", &log, false, 0)));
        host.AddModule(std::make_unique<RecordingModule>("b", &log, false, 0));

        World world;
        GameModuleContext ctx{};
        ctx.pWorld = &world;

        // 护栏：未 EnterPlay 时 Tick / OnEvent / ExitPlay 全 no-op。
        host.Tick(ctx, 0.016f);
        host.OnEvent(Orange::Engine::Platform::WindowEvent{});
        host.ExitPlay(ctx);
        assert(log.empty());
        assert(!host.IsInPlay());

        // EnterPlay 正序扇出 + ctx 透传。
        host.EnterPlay(ctx);
        assert(host.IsInPlay());
        assert(a->seenWorld == &world); // 上下文字段透传到模块

        // 重复 EnterPlay no-op（不再次扇出 enter）。
        host.EnterPlay(ctx);

        host.Tick(ctx, 0.016f);
        host.OnEvent(Orange::Engine::Platform::WindowEvent{});

        // ExitPlay 逆序扇出（b 先于 a）。
        host.ExitPlay(ctx);
        assert(!host.IsInPlay());

        // 退出后再 Tick no-op。
        host.Tick(ctx, 0.016f);

        const std::vector<std::string> expected = {
            "a:enter", "b:enter",
            "a:tick", "b:tick",
            "a:event", "b:event",
            "b:exit", "a:exit", // 逆序
        };
        assert(log == expected);
        assert(a->tickCount == 1); // 未 EnterPlay 的两次 Tick 未计

        std::fprintf(stdout, "  [PASS] 生命周期护栏 + 正序 enter / 逆序 exit + ctx 透传\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[GameModuleHostTest] running\n");
    TestAddAndNullptr();
    TestRegisterFanOut();
    TestLifecycleGuardsAndOrder();
    std::fprintf(stdout, "[GameModuleHostTest] all tests passed.\n");
    return 0;
}
