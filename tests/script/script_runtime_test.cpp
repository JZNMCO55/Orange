// ScriptRuntime 端到端测试（ADR-017 B1.1）—— C# 脚本经函数指针表操作引擎实体。
//
// 证明完整闭环：
//   C++ 建 World + 实体（带 Transform 在原点）
//     → ScriptRuntime.Initialize 起 CoreCLR + 取托管 glue + Bootstrap 推绑定表
//     → SetCurrentWorld(&w)
//     → CreateInstance 实例化 OrangeFixtures.Mover 绑定到该实体
//     → InvokeStart → InvokeUpdate(1.0)
//     → 脚本在 OnUpdate 里 Entity.Position += (1,0,0) 回调进 C++
//     → 断言引擎侧该实体 TransformComponent.position.x ≈ 1
//     → InvokeDestroy → Release。
// 外加 Entity_IsValid：有效实体的 scriptId → true；伪造 id → false。
//
// **进程内单次运行时启动的约束**（同 dotnet_host_test）：CoreCLR 每进程只能
// 加载一次 runtime（且不卸载）。因此本测试只做**一次**成功的 Initialize（happy
// path），所有断言复用同一个已初始化的 ScriptRuntime。
//
// 托管 fixture（ScriptFixtures.dll / runtimeconfig + OrangeScriptSDK.dll）的磁盘
// 路径由 CMake 经 target_compile_definitions 注入。

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/script/ScriptRuntime.h>

// src 内绑定层头 —— 直接验 native 绑定函数（Entity_IsValid 等）。这些
// extern "C" 函数随 ORANGE_ENGINE_WITH_DOTNET=ON 编进 orange_engine，测试链
// 得到符号；CMake 给本测试加 src/script/dotnet include 路径取声明。header
// isolation 不破：本头不含任何 CLR / hostfxr 头（纯 C ABI 声明）。
#include "ScriptBindings.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using Orange::Engine::Entity;
using Orange::Engine::ResultCode;
using Orange::Engine::World;
using Orange::Engine::Scene::TransformComponent;
using Orange::Engine::Script::ScriptInstanceHandle;
using Orange::Engine::Script::ScriptRuntime;
namespace Script = Orange::Engine::Script;

#ifndef ORANGE_SCRIPTFIXTURES_ASSEMBLY_PATH
#error "ORANGE_SCRIPTFIXTURES_ASSEMBLY_PATH 未由 CMake 注入"
#endif
#ifndef ORANGE_SCRIPTFIXTURES_RUNTIMECONFIG_PATH
#error "ORANGE_SCRIPTFIXTURES_RUNTIMECONFIG_PATH 未由 CMake 注入"
#endif
#ifndef ORANGE_SCRIPT_SDK_ASSEMBLY_PATH
#error "ORANGE_SCRIPT_SDK_ASSEMBLY_PATH 未由 CMake 注入"
#endif

namespace
{

const std::string kFixturesAssembly = ORANGE_SCRIPTFIXTURES_ASSEMBLY_PATH;
const std::string kFixturesRuntimeConfig = ORANGE_SCRIPTFIXTURES_RUNTIMECONFIG_PATH;
const std::string kSdkAssembly = ORANGE_SCRIPT_SDK_ASSEMBLY_PATH;

bool ApproxEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[script_runtime_test] running\n");
    std::fprintf(stdout, "  fixtures assembly = %s\n", kFixturesAssembly.c_str());
    std::fprintf(stdout, "  runtimeconfig     = %s\n", kFixturesRuntimeConfig.c_str());
    std::fprintf(stdout, "  sdk assembly      = %s\n", kSdkAssembly.c_str());

    // --- 建 World + 实体（带 Transform 在原点） ---
    World world;
    Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});  // position 默认 (0,0,0)
    assert(world.IsValid(entity));
    {
        const auto* t = world.GetComponent<TransformComponent>(entity);
        assert(t != nullptr);
        assert(ApproxEqual(t->position.x, 0.0f));
    }

    // --- 未初始化时 CreateInstance → NotInitialized（不启动运行时） ---
    {
        ScriptRuntime cold;
        auto r = cold.CreateInstance(kFixturesAssembly, "OrangeFixtures.Mover, ScriptFixtures", entity);
        assert(r.IsErr());
        assert(r.Error() == ResultCode::NotInitialized);
        std::fprintf(stdout, "  [PASS] create before init -> NotInitialized\n");
    }

    // --- 唯一一次真正的 runtime 启动 ---
    ScriptRuntime rt;
    assert(!rt.IsInitialized());
    auto initResult = rt.Initialize(kFixturesRuntimeConfig, kSdkAssembly);
    if (initResult.IsErr())
    {
        std::fprintf(stderr, "  [FAIL] Initialize -> Err (%s)\n",
                     Orange::Engine::ToString(initResult.Error()));
        return 1;
    }
    assert(rt.IsInitialized());
    std::fprintf(stdout, "  [PASS] Initialize ok\n");

    // 重复 Initialize → AlreadyInitialized（不重启运行时）。
    {
        auto second = rt.Initialize(kFixturesRuntimeConfig, kSdkAssembly);
        assert(second.IsErr());
        assert(second.Error() == ResultCode::AlreadyInitialized);
        std::fprintf(stdout, "  [PASS] double initialize -> AlreadyInitialized\n");
    }

    // --- 设当前 World + 实例化 Mover 绑定到该实体 ---
    rt.SetCurrentWorld(&world);
    auto handleResult = rt.CreateInstance(
        kFixturesAssembly, "OrangeFixtures.Mover, ScriptFixtures", entity);
    if (handleResult.IsErr())
    {
        std::fprintf(stderr, "  [FAIL] CreateInstance -> Err (%s)\n",
                     Orange::Engine::ToString(handleResult.Error()));
        return 1;
    }
    ScriptInstanceHandle handle = handleResult.Value();
    assert(handle.IsValid());
    std::fprintf(stdout, "  [PASS] CreateInstance ok\n");

    // --- OnStart ---
    // Mover.OnStart 给 position.y += 100。断言被调恰一次（y == 100）。
    rt.InvokeStart(handle);
    {
        const auto* t = world.GetComponent<TransformComponent>(entity);
        assert(t != nullptr);
        if (!ApproxEqual(t->position.y, 100.0f))
        {
            std::fprintf(stderr,
                "  [FAIL] OnStart 后 position.y = %f（期望 100，证明 OnStart 调一次）\n",
                t->position.y);
            return 1;
        }
        assert(ApproxEqual(t->position.x, 0.0f));  // 还没 OnUpdate
        std::fprintf(stdout, "  [PASS] OnStart called once: position.y = %f\n", t->position.y);
    }

    // --- OnUpdate(1.0) ---
    // Mover.OnUpdate 给 position.x += 1。断言脚本经函数指针表回调进 C++、真改了
    // 引擎侧 position.x。
    rt.InvokeUpdate(handle, 1.0f);
    {
        const auto* t = world.GetComponent<TransformComponent>(entity);
        assert(t != nullptr);
        if (!ApproxEqual(t->position.x, 1.0f))
        {
            std::fprintf(stderr,
                "  [FAIL] OnUpdate 后 position.x = %f（期望 1.0）\n", t->position.x);
            return 1;
        }
        std::fprintf(stdout, "  [PASS] OnUpdate moved entity: position.x = %f\n", t->position.x);
    }

    // 再 InvokeUpdate 一次：累积到 2.0，证明每次回调都生效。
    rt.InvokeUpdate(handle, 1.0f);
    {
        const auto* t = world.GetComponent<TransformComponent>(entity);
        assert(t != nullptr);
        assert(ApproxEqual(t->position.x, 2.0f));
        std::fprintf(stdout, "  [PASS] second OnUpdate: position.x = %f\n", t->position.x);
    }

    // --- Entity_IsValid 经绑定表：有效实体 → true，伪造 id → false ---
    // 直接验 native 绑定函数（C# 侧 Entity.IsValid 转发同一函数指针）。当前
    // World 已 SetCurrentWorld(&world)，绑定函数在该 World 上 decode + 校验。
    {
        const std::uint64_t validScriptId = Script::EncodeEntityId(entity);
        const std::uint64_t fakeScriptId = 0xDEADBEEFull;       // 不存在的实体
        const std::uint64_t noneScriptId = 0;                   // id 0 = none

        assert(Script::Orange_Entity_IsValid(validScriptId) == 1);
        assert(Script::Orange_Entity_IsValid(fakeScriptId) == 0);
        assert(Script::Orange_Entity_IsValid(noneScriptId) == 0);
        std::fprintf(stdout,
            "  [PASS] Entity_IsValid: valid=1, fake=0, none=0\n");

        // Entity codec round-trip：encode → decode 回到同一实体。
        const Entity decoded = Script::DecodeEntityId(validScriptId);
        assert(decoded == entity);
        assert(Script::DecodeEntityId(0) == Entity::Invalid());
        std::fprintf(stdout, "  [PASS] Entity id codec round-trip\n");

        // 销毁实体后 IsValid 应转 false（端到端等价验证）。
        World probe;
        Entity victim = probe.CreateEntity();
        rt.SetCurrentWorld(&probe);
        const std::uint64_t victimId = Script::EncodeEntityId(victim);
        assert(Script::Orange_Entity_IsValid(victimId) == 1);
        probe.DestroyEntity(victim);
        assert(Script::Orange_Entity_IsValid(victimId) == 0);
        rt.SetCurrentWorld(&world);  // 还原
        std::fprintf(stdout, "  [PASS] Entity_IsValid false after destroy\n");
    }

    // --- OnDestroy → Release ---
    // Mover.OnDestroy 给 position.z += 7。断言被调恰一次（z == 7）。当前 World
    // 已在 IsValid 块末还原回 &world。
    rt.InvokeDestroy(handle);
    {
        const auto* t = world.GetComponent<TransformComponent>(entity);
        assert(t != nullptr);
        if (!ApproxEqual(t->position.z, 7.0f))
        {
            std::fprintf(stderr,
                "  [FAIL] OnDestroy 后 position.z = %f（期望 7，证明 OnDestroy 调一次）\n",
                t->position.z);
            return 1;
        }
        // x 仍是 2（OnDestroy 不动 x），y 仍是 100 —— 各回调互不串扰。
        assert(ApproxEqual(t->position.x, 2.0f));
        assert(ApproxEqual(t->position.y, 100.0f));
        std::fprintf(stdout, "  [PASS] OnDestroy called once: position.z = %f\n", t->position.z);
    }
    rt.Release(handle);
    std::fprintf(stdout, "  [PASS] Release ok\n");

    // Release 后再调生命周期：句柄已失效，但 GCHandle.FromIntPtr 可能仍指向
    // 已 Free 的 slot —— 托管 glue 的 Resolve 会因 !IsAllocated 返回 null，
    // 不崩。这里不强断言行为（Release 后用句柄是误用），只确认不崩进程。
    rt.InvokeUpdate(handle, 1.0f);
    std::fprintf(stdout, "  [PASS] update after release does not crash\n");

    std::fprintf(stdout, "[script_runtime_test] all tests passed.\n");
    return 0;
}
