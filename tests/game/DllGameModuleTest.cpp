// M7 DLL 游戏模块宿主机制回归门：验证 GameModuleLibrary 的 load / use / unload /
// **reload**（热重载核心）与错误路径，全程 headless 不崩。
//
// test game.dll 路径经 argv[1] 传入（CMake 用 $<TARGET_FILE:test_game_module> 填）。

#include <orange/engine/game/GameModuleHost.h>
#include <orange/engine/game/GameModuleLibrary.h>
#include <orange/engine/render/IRenderPass.h>
#include <orange/engine/render/Pipeline.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>

using Orange::Engine::Game::GameModuleHost;
using Orange::Engine::Game::GameModuleLibrary;

namespace
{
    int Fail(const char* msg)
    {
        std::fprintf(stderr, "[DllGameModuleTest] FAIL: %s\n", msg);
        return EXIT_FAILURE;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        return Fail("缺 test game.dll 路径（argv[1]）");
    }
    const std::filesystem::path dll = argv[1];

    // 1) 加载 → 用 → 卸载。虚调用 Name() 跨 DLL 边界解析到 dll 内的 vtable。
    {
        auto lib = GameModuleLibrary::Load(dll);
        if (!lib) return Fail("首次 Load 返回 null");
        if (lib->Module() == nullptr) return Fail("Module() 为空");
        if (std::strcmp(lib->Module()->Name(), "TestGameModule") != 0) return Fail("Name 不匹配");
        // 卸载 = lib 出作用域析构：先 OrangeDestroyGameModule（dll 内销毁），再 FreeLibrary。
        // 顺序错会在此崩溃（悬空 vtable）。
    }

    // 2) 热重载：卸载后重新 Load（模拟 Stop → FreeLibrary → 重编 → LoadLibrary → 再用）。
    //    shadow-copy 保证原 dll 未被锁、可重编；每次 Load 用新的唯一 shadow 副本。
    for (int i = 0; i < 3; ++i)
    {
        auto lib = GameModuleLibrary::Load(dll);
        if (!lib) return Fail("重载 Load 返回 null");
        if (std::strcmp(lib->Module()->Name(), "TestGameModule") != 0) return Fail("重载 Name 不匹配");
    }

    // 3) 同时持有两个实例（宿主可同时挂多个 game module）——各自独立 shadow + module。
    {
        auto a = GameModuleLibrary::Load(dll);
        auto b = GameModuleLibrary::Load(dll);
        if (!a || !b) return Fail("并存两实例 Load 返回 null");
        if (a->Module() == b->Module()) return Fail("两实例应是独立 module 对象");
    }

    // 4) 错误路径：不存在的 dll 返回 null（不崩、不抛）。
    {
        auto lib = GameModuleLibrary::Load("this-game-module-does-not-exist-xyz.dll");
        if (lib) return Fail("缺失 dll 应返回 null");
    }

    // 5) 宿主集成：GameModuleHost 拥有 DLL 库并无差别驱动生命周期；ClearModules /
    //    host 析构安全卸载（先 dll 内销毁模块、再 FreeLibrary）。
    {
        Orange::Engine::Game::GameModuleContext ctx{}; // 全 null；TestGameModule 各回调 no-op

        GameModuleHost host;
        if (host.ModuleCount() != 0) return Fail("host 初始 ModuleCount 非 0");

        auto* raw = host.AddModuleLibrary(GameModuleLibrary::Load(dll));
        if (raw == nullptr) return Fail("AddModuleLibrary 返回 null");
        if (host.ModuleCount() != 1) return Fail("AddModuleLibrary 后 ModuleCount 应为 1");

        // 驱动 Play 生命周期（护栏 + 扇出，ctx 全 null 由 no-op 默认吞）。
        host.EnterPlay(ctx);
        if (!host.IsInPlay()) return Fail("EnterPlay 后应在 Play");
        host.Tick(ctx, 0.016f);
        host.ExitPlay(ctx);
        if (host.IsInPlay()) return Fail("ExitPlay 后不应在 Play");

        // Edit 态 ClearModules 安全卸载（销毁 GameModuleLibrary → dll 内销毁 + FreeLibrary）。
        host.ClearModules();
        if (host.ModuleCount() != 0) return Fail("ClearModules 后 ModuleCount 应为 0");

        // 混合静态 + DLL 模块并存 + host 析构自动安全卸载。
        host.AddModuleLibrary(GameModuleLibrary::Load(dll));
        host.AddModuleLibrary(GameModuleLibrary::Load(dll));
        if (host.ModuleCount() != 2) return Fail("并存两 DLL 库 ModuleCount 应为 2");
        // host 出作用域析构：mOwnedLibraries 各析构安全卸载，不崩。
    }

    // 6) 热重载 + render pass 注销对偶（M7 session 级热重载核心）：ReloadLibrary
    //    卸载前摘 pass、重 Load 新 shadow、重注册；pass 数不累积（旧 1 注销 + 新 1
    //    注册 = 1，非 2）。Pipeline 持的 TestPass 代码在 test.dll 内——声明序 host
    //    先 / pipeline 后，令析构时 pipeline 先清 pass（此时 dll 未 FreeLibrary，安全）、
    //    host 后 FreeLibrary，避免悬垂 vtable。
    {
        using Orange::Engine::Render::PipelineStage;
        GameModuleHost                   host;
        Orange::Engine::Render::Pipeline pipeline; // headless 默认构造（同 GameModuleHostTest）

        if (host.AddModuleLibrary(GameModuleLibrary::Load(dll)) == nullptr)
            return Fail("reload 前 AddModuleLibrary 返回 null");
        if (host.LibraryCount() != 1) return Fail("AddModuleLibrary 后 LibraryCount 应为 1");

        host.RegisterRenderPasses(pipeline);
        if (pipeline.InsertedPassCount(PipelineStage::AfterMainPass) != 1)
            return Fail("注册后 AfterMainPass pass 数应为 1");

        Orange::Engine::Game::IGameModule* reloaded = host.ReloadLibrary(0, pipeline);
        if (reloaded == nullptr) return Fail("ReloadLibrary 返回 null");
        if (std::strcmp(reloaded->Name(), "TestGameModule") != 0)
            return Fail("热重载后 Name 跨 DLL 虚调用不匹配");
        if (pipeline.InsertedPassCount(PipelineStage::AfterMainPass) != 1)
            return Fail("热重载后 pass 数应仍为 1（旧注销 + 新注册，不累积）");
        if (host.ModuleCount() != 1 || host.LibraryCount() != 1)
            return Fail("热重载后 Module/LibraryCount 应仍为 1");

        // Play 态护栏：EnterPlay 后 ReloadLibrary 拒绝（返 null，不动库/pass）。
        Orange::Engine::Game::GameModuleContext ctx{};
        host.EnterPlay(ctx);
        if (host.ReloadLibrary(0, pipeline) != nullptr)
            return Fail("Play 态 ReloadLibrary 应拒绝返 null");
        if (host.LibraryCount() != 1) return Fail("Play 态被拒的 reload 不应动库");
        if (pipeline.InsertedPassCount(PipelineStage::AfterMainPass) != 1)
            return Fail("Play 态被拒的 reload 不应动 pass");
        host.ExitPlay(ctx);
    }

    std::printf("[DllGameModuleTest] PASS: load/use/unload/reload/并存/错误路径/"
                "宿主集成/热重载+pass注销 全过\n");
    return EXIT_SUCCESS;
}
