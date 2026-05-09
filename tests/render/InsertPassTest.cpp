// Pipeline::InsertPass 公共面测试。
//
// 不接 GPU 路径——具体绘制行为靠 sample 视觉验证。这里只覆盖 4 条
// 公共 API 的不变量：
//   * 默认 Pipeline 上 InsertedPassCount == 0；
//   * 同 stage 多次 InsertPass 累加，按调用顺序保留；
//   * 不同 stage 互相隔离；
//   * RemovePassesAt / ClearInsertedPasses 后归零，pass 析构被触发。

#include <orange/engine/render/IRenderPass.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderPassContext.h>

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

using Orange::Engine::Render::IRenderPass;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PipelineStage;
using Orange::Engine::Render::RenderGraphBuilder;
using Orange::Engine::Render::RenderPassContext;

namespace
{

// 最小可观测 IRenderPass 实现：构造时 ++setupCount / 析构时 ++destroyCount /
// Setup 调用时 ++setupCount2 / Execute 调用时 ++executeCount。让测试能
// 验证 lifecycle 与 dispatch 都正确。
struct ObservedPass : public IRenderPass
{
    std::string  name;
    int*         pSetupCount{nullptr};
    int*         pExecuteCount{nullptr};
    int*         pDestroyCount{nullptr};

    ObservedPass(std::string n,
                 int* setupCount, int* executeCount, int* destroyCount)
        : name(std::move(n))
        , pSetupCount(setupCount)
        , pExecuteCount(executeCount)
        , pDestroyCount(destroyCount)
    {
    }

    ~ObservedPass() override
    {
        if (pDestroyCount) ++(*pDestroyCount);
    }

    const char* Name() const noexcept override { return name.c_str(); }

    void Setup(RenderGraphBuilder& builder) override
    {
        if (pSetupCount) ++(*pSetupCount);
        // 验证 builder 字段可被读取——值不强制；只是确认 API 可用。
        (void)builder.Kind();
        // 触发 Read / Write 占位接口；0.x Pipeline 忽略，但 API 应当能调通。
        const int sentinel = 0;
        builder.Read(&sentinel);
        builder.Write(&sentinel);
    }

    void Execute(RenderPassContext& /*ctx*/) override
    {
        if (pExecuteCount) ++(*pExecuteCount);
    }
};

void TestEmptyPipeline()
{
    Pipeline p;
    assert(p.InsertedPassCount(PipelineStage::AfterShadow)      == 0);
    assert(p.InsertedPassCount(PipelineStage::AfterMainPass)    == 0);
    assert(p.InsertedPassCount(PipelineStage::AfterPostProcess) == 0);

    std::fprintf(stdout, "  [PASS] empty pipeline -> 0 inserted passes\n");
}

void TestInsertTriggersSetup()
{
    int setupCount = 0;
    int executeCount = 0;
    int destroyCount = 0;

    {
        Pipeline p;
        p.InsertPass(PipelineStage::AfterMainPass,
                     std::make_unique<ObservedPass>(
                         "test_pass", &setupCount, &executeCount, &destroyCount));

        // InsertPass 立即触发一次 Setup。
        assert(setupCount == 1);
        assert(executeCount == 0);  // 没 Render，没 Execute
        assert(destroyCount == 0);
        assert(p.InsertedPassCount(PipelineStage::AfterMainPass) == 1);
    }

    // Pipeline 析构 → pass unique_ptr 析构 → ObservedPass dtor 触发。
    assert(destroyCount == 1);
    assert(setupCount == 1);
    assert(executeCount == 0);

    std::fprintf(stdout, "  [PASS] InsertPass -> Setup; ~Pipeline -> ~Pass\n");
}

void TestStageIsolation()
{
    int s_after_shadow = 0;
    int s_after_main   = 0;
    int s_after_post   = 0;
    int e0 = 0, e1 = 0, e2 = 0;
    int d0 = 0, d1 = 0, d2 = 0;

    Pipeline p;
    p.InsertPass(PipelineStage::AfterShadow,
                 std::make_unique<ObservedPass>("shadow_pass", &s_after_shadow, &e0, &d0));
    p.InsertPass(PipelineStage::AfterMainPass,
                 std::make_unique<ObservedPass>("main_pass",   &s_after_main,   &e1, &d1));
    p.InsertPass(PipelineStage::AfterPostProcess,
                 std::make_unique<ObservedPass>("post_pass",   &s_after_post,   &e2, &d2));

    assert(p.InsertedPassCount(PipelineStage::AfterShadow)      == 1);
    assert(p.InsertedPassCount(PipelineStage::AfterMainPass)    == 1);
    assert(p.InsertedPassCount(PipelineStage::AfterPostProcess) == 1);

    // RemovePassesAt 仅清单一 stage，不影响其它 stage。
    p.RemovePassesAt(PipelineStage::AfterMainPass);
    assert(p.InsertedPassCount(PipelineStage::AfterShadow)      == 1);
    assert(p.InsertedPassCount(PipelineStage::AfterMainPass)    == 0);
    assert(p.InsertedPassCount(PipelineStage::AfterPostProcess) == 1);
    assert(d1 == 1);  // 被 RemovePassesAt 析构
    assert(d0 == 0);
    assert(d2 == 0);

    std::fprintf(stdout, "  [PASS] stages are isolated; RemovePassesAt only clears one stage\n");
}

void TestMultiplePassesPerStage()
{
    int s = 0, e = 0, d = 0;

    Pipeline p;
    p.InsertPass(PipelineStage::AfterMainPass,
                 std::make_unique<ObservedPass>("a", &s, &e, &d));
    p.InsertPass(PipelineStage::AfterMainPass,
                 std::make_unique<ObservedPass>("b", &s, &e, &d));
    p.InsertPass(PipelineStage::AfterMainPass,
                 std::make_unique<ObservedPass>("c", &s, &e, &d));

    assert(p.InsertedPassCount(PipelineStage::AfterMainPass) == 3);
    assert(s == 3);  // 每个 pass 都被 Setup 一次
    assert(d == 0);

    p.ClearInsertedPasses();
    assert(p.InsertedPassCount(PipelineStage::AfterMainPass) == 0);
    assert(d == 3);  // 三个 pass 都析构

    std::fprintf(stdout, "  [PASS] multiple passes per stage; ClearInsertedPasses -> all destructed\n");
}

void TestNullptrPassSilentIgnore()
{
    Pipeline p;
    p.InsertPass(PipelineStage::AfterMainPass, nullptr);
    assert(p.InsertedPassCount(PipelineStage::AfterMainPass) == 0);

    std::fprintf(stdout, "  [PASS] nullptr InsertPass silent-ignored\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[InsertPassTest] running\n");
    TestEmptyPipeline();
    TestInsertTriggersSetup();
    TestStageIsolation();
    TestMultiplePassesPerStage();
    TestNullptrPassSilentIgnore();
    std::fprintf(stdout, "[InsertPassTest] all tests passed.\n");
    return 0;
}
