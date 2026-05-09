// DragonBonesRuntimeSmoke —— Phase 4 / Task 02 验收：
// runtime 真被编进 orange_engine + 链接成功 + 能 ctor / advanceTime / dtor
// 不崩。
//
// 直接验证 dragonBones 内部类型违反 CLAUDE.md "Header isolation" 不变量
//   <dragonBones/...> 仅出现在 src/animation/dragonbones/**
// 这里改走 OrangeEngine 内部 `DragonBonesContext` 入口（src/animation/
// dragonbones/DragonBonesContext.h 是 src 内部头，本 test 通过 target_
// include_directories 拿到 src/ 路径来 include）——它内部已经在 cpp 里
// include 完整 <dragonBones/...>，本测试只看公共 RAII 行为：
//   * 构造一次 → ctor 链跑完不崩，EventDispatcher() / Clock() 都非空；
//   * AdvanceTime(dt > 0) 推进 WorldClock 时间不崩；
//   * 多 context 实例并存不互踩；
//   * 析构后内存归还（valgrind / leak detector 由 ctest 路径外的工具
//     兜底，本测仅断言 RAII 顺序合法）。

#include "animation/dragonbones/DragonBonesContext.h"

#include <cassert>

namespace DBB = Orange::Engine::Animation::DragonBonesBackend;

namespace
{

void TestConstructAdvanceDestroy()
{
    DBB::DragonBonesContext ctx;

    // ctor 必须把 NoopEventDispatcher 挂上 + DragonBones runtime 实例化。
    // 任一返 nullptr 都说明 runtime 没真链进来 / ctor 链断了。
    assert(ctx.EventDispatcher() != nullptr);
    assert(ctx.Clock() != nullptr);

    // 推进 1 秒 + 跨 0 步长（边界）+ 负 dt（被 context 自己拦掉）。
    ctx.AdvanceTime(1.0f / 60.0f);
    ctx.AdvanceTime(0.0f);
    ctx.AdvanceTime(-1.0f);   // 负 dt：no-op

    // 推一大段时间也不崩：runtime 内部 WorldClock 不依赖单调递增 dt
    // 真按 frame rate 走，只是累加。
    for (int i = 0; i < 1000; ++i)
    {
        ctx.AdvanceTime(1.0f / 60.0f);
    }
}

void TestMultipleContexts()
{
    // 多实例并存：每个 context 持自己的 dispatcher / runtime / clock，
    // 互不污染。
    DBB::DragonBonesContext a;
    DBB::DragonBonesContext b;

    assert(a.EventDispatcher() != b.EventDispatcher());
    assert(a.Clock() != b.Clock());

    a.AdvanceTime(1.0f / 60.0f);
    b.AdvanceTime(1.0f / 60.0f);
}

}  // namespace

int main()
{
    TestConstructAdvanceDestroy();
    TestMultipleContexts();
    return 0;
}
