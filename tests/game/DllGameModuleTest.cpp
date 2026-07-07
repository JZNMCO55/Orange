// M7 DLL 游戏模块宿主机制回归门：验证 GameModuleLibrary 的 load / use / unload /
// **reload**（热重载核心）与错误路径，全程 headless 不崩。
//
// test game.dll 路径经 argv[1] 传入（CMake 用 $<TARGET_FILE:test_game_module> 填）。

#include <orange/engine/game/GameModuleLibrary.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>

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

    std::printf("[DllGameModuleTest] PASS: load/use/unload/reload/并存/错误路径 全过\n");
    return EXIT_SUCCESS;
}
