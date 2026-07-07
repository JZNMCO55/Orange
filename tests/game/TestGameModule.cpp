// M7 DLL 游戏模块宿主回归门用的最小 test game.dll。
// 实现 Name + Register/UnregisterRenderPasses（往 Pipeline 插/摘一个 test pass，
// 模拟"pass 对象代码在 dll 内"——热重载注销对偶的被测场景），经
// ORANGE_EXPORT_GAME_MODULE 宏导出 OrangeCreateGameModule / OrangeDestroyGameModule。
// 刻意不引任何引擎 runtime 状态，隔离验证 load/create/destroy/unload/reload 机制。

#include <orange/engine/game/GameModuleLibrary.h>
#include <orange/engine/game/IGameModule.h>
#include <orange/engine/render/IRenderPass.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderPassContext.h>

#include <memory>

namespace
{
    // 最小 no-op pass。vtable 与代码都在 test game.dll 内——正是热重载卸载前
    // 必须 UnregisterRenderPasses 摘除的对象（否则 FreeLibrary 后 Pipeline 悬垂）。
    struct TestPass final : public Orange::Engine::Render::IRenderPass
    {
        const char* Name() const noexcept override { return "TestGameModulePass"; }
        void        Setup(Orange::Engine::Render::RenderGraphBuilder& /*b*/) override {}
        void        Execute(Orange::Engine::Render::RenderPassContext& /*c*/) override {}
    };

    class TestGameModule final : public Orange::Engine::Game::IGameModule
    {
    public:
        const char* Name() const noexcept override { return "TestGameModule"; }

        void RegisterRenderPasses(Orange::Engine::Render::Pipeline& pipeline) override
        {
            pipeline.InsertPass(Orange::Engine::Render::PipelineStage::AfterMainPass,
                                std::make_unique<TestPass>());
        }

        void UnregisterRenderPasses(Orange::Engine::Render::Pipeline& pipeline) override
        {
            // 摘掉本模块插的 pass。AfterMainPass 上只有游戏 pass（内置 post 走独立
            // 机制，不占 insertedPasses），故清整 stage 只影响本模块。
            pipeline.RemovePassesAt(Orange::Engine::Render::PipelineStage::AfterMainPass);
        }
    };
} // namespace

ORANGE_EXPORT_GAME_MODULE(TestGameModule)
