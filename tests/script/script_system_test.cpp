// ScriptSystem 端到端测试（ADR-017 B1.2）—— World 上带 ScriptComponent 的实体
// 被批量实例化 + 按生命周期 tick。
//
// 证明完整闭环：
//   C++ 建 World + 实体（带 Transform 在原点 + ScriptComponent 指向 Mover）
//     → ScriptRuntime.Initialize 起 CoreCLR + 取托管 glue + Bootstrap 推绑定表
//     → ScriptSystem.StartWorld：遍历 ScriptComponent → CreateInstance + OnStart
//     → Tick(1.0) ×2：对所有活实例 OnUpdate
//     → StopWorld：OnDestroy + Release + 清 map
//     → 断言引擎侧该实体 TransformComponent：
//         y==100（OnStart 一次）、x==2（两次 Tick）、z==7（OnDestroy 一次）。
//   外加：CreateInstance 失败的实体被跳过、不中断其他（坏 typeName 实体）；
//         ScriptSystem 析构对漏 StopWorld 的残留 handle 做 Release 兜底。
//
// **进程内单次运行时启动的约束**（同 script_runtime_test / dotnet_host_test）：
// CoreCLR 每进程只能加载一次 runtime（且不卸载）。本测试只做**一次**成功的
// ScriptRuntime.Initialize，所有断言复用同一个已初始化的 ScriptRuntime。
//
// 托管 fixture（ScriptFixtures.dll / runtimeconfig + OrangeScriptSDK.dll）的磁盘
// 路径由 CMake 经 target_compile_definitions 注入（复用 script_runtime_test 同款）。

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/script/ScriptComponent.h>
#include <orange/engine/script/ScriptRuntime.h>
#include <orange/engine/script/ScriptSystem.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Scene::TransformComponent;
using Orange::Engine::Script::ScriptComponent;
using Orange::Engine::Script::ScriptRuntime;
using Orange::Engine::Script::ScriptSystem;

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

// Mover 的 assembly-qualified 类型全名（与 script_runtime_test 一致）。
const std::string kMoverType = "OrangeFixtures.Mover, ScriptFixtures";

bool ApproxEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[script_system_test] running\n");
    std::fprintf(stdout, "  fixtures assembly = %s\n", kFixturesAssembly.c_str());

    // --- 唯一一次真正的 runtime 启动 ---
    ScriptRuntime rt;
    auto initResult = rt.Initialize(kFixturesRuntimeConfig, kSdkAssembly);
    if (initResult.IsErr())
    {
        std::fprintf(stderr, "  [FAIL] Initialize -> Err (%s)\n",
                     Orange::Engine::ToString(initResult.Error()));
        return 1;
    }
    assert(rt.IsInitialized());
    std::fprintf(stdout, "  [PASS] Initialize ok\n");

    // --- 建 World + 实体（带 Transform 在原点 + ScriptComponent 指向 Mover） ---
    World world;
    Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});  // position 默认 (0,0,0)
    {
        ScriptComponent sc;
        sc.assemblyPath = kFixturesAssembly;
        sc.typeName     = kMoverType;
        world.AddComponent(entity, sc);
    }
    {
        const auto* t = world.GetComponent<TransformComponent>(entity);
        assert(t != nullptr);
        assert(ApproxEqual(t->position.x, 0.0f));
        assert(ApproxEqual(t->position.y, 0.0f));
        assert(ApproxEqual(t->position.z, 0.0f));
    }

    // 再加一个 ScriptComponent 指向不存在类型的实体：StartWorld 应跳过它
    // （CreateInstance 失败 + warn），不影响 Mover 实体。
    Entity badEntity = world.CreateEntity();
    world.AddComponent(badEntity, TransformComponent{});
    {
        ScriptComponent sc;
        sc.assemblyPath = kFixturesAssembly;
        sc.typeName     = "OrangeFixtures.DoesNotExist, ScriptFixtures";
        world.AddComponent(badEntity, sc);
    }

    // ScriptSystem 作用域 —— 退出时若漏 StopWorld 会析构兜底 Release。
    {
        ScriptSystem scripts(rt);

        // --- StartWorld：批量实例化 + OnStart ---
        scripts.StartWorld(world);
        // 仅 Mover 实体成功实例化；坏类型实体被跳过 → 活实例数 == 1。
        if (scripts.ActiveInstanceCount() != 1)
        {
            std::fprintf(stderr,
                "  [FAIL] StartWorld 后活实例数 = %zu（期望 1：坏类型实体应被跳过）\n",
                scripts.ActiveInstanceCount());
            return 1;
        }
        std::fprintf(stdout, "  [PASS] StartWorld instantiated 1 (bad-type entity skipped)\n");

        // Mover.OnStart 给 position.y += 100。断言被调恰一次（y == 100）。
        {
            const auto* t = world.GetComponent<TransformComponent>(entity);
            assert(t != nullptr);
            if (!ApproxEqual(t->position.y, 100.0f))
            {
                std::fprintf(stderr,
                    "  [FAIL] OnStart 后 position.y = %f（期望 100）\n", t->position.y);
                return 1;
            }
            // 还没 Tick：x / z 仍 0。
            assert(ApproxEqual(t->position.x, 0.0f));
            assert(ApproxEqual(t->position.z, 0.0f));
            std::fprintf(stdout, "  [PASS] OnStart called once: position.y = %f\n", t->position.y);
        }
        // 坏类型实体的 Transform 完全没动（脚本没实例化）。
        assert(ApproxEqual(world.GetComponent<TransformComponent>(badEntity)->position.y, 0.0f));

        // --- Tick(1.0) ×2：每次 Mover.OnUpdate 给 position.x += 1 ---
        scripts.Tick(world, 1.0f);
        scripts.Tick(world, 1.0f);
        {
            const auto* t = world.GetComponent<TransformComponent>(entity);
            assert(t != nullptr);
            if (!ApproxEqual(t->position.x, 2.0f))
            {
                std::fprintf(stderr,
                    "  [FAIL] 两次 Tick 后 position.x = %f（期望 2.0）\n", t->position.x);
                return 1;
            }
            // OnStart 的 y 不被 Tick 干扰。
            assert(ApproxEqual(t->position.y, 100.0f));
            std::fprintf(stdout, "  [PASS] two Tick(1.0): position.x = %f\n", t->position.x);
        }

        // --- StopWorld：OnDestroy + Release + 清 map ---
        scripts.StopWorld(world);
        assert(scripts.ActiveInstanceCount() == 0);
        {
            const auto* t = world.GetComponent<TransformComponent>(entity);
            assert(t != nullptr);
            if (!ApproxEqual(t->position.z, 7.0f))
            {
                std::fprintf(stderr,
                    "  [FAIL] OnDestroy 后 position.z = %f（期望 7）\n", t->position.z);
                return 1;
            }
            // x / y 不被 OnDestroy 干扰 —— 三个回调互不串扰。
            assert(ApproxEqual(t->position.x, 2.0f));
            assert(ApproxEqual(t->position.y, 100.0f));
            std::fprintf(stdout, "  [PASS] StopWorld OnDestroy once: position.z = %f\n", t->position.z);
        }
    }
    std::fprintf(stdout, "  [PASS] ScriptSystem destructed cleanly\n");

    // --- 析构兜底验证：StartWorld 后故意不 StopWorld，让析构 Release 残留 handle ---
    // 不崩进程即视为兜底生效（GCHandle 不泄漏由 runtime 侧 Release 保证；这里
    // 只验证 ScriptSystem 析构路径会触达 Release）。新建 World 复用同一 rt。
    {
        World world2;
        Entity e2 = world2.CreateEntity();
        world2.AddComponent(e2, TransformComponent{});
        {
            ScriptComponent sc;
            sc.assemblyPath = kFixturesAssembly;
            sc.typeName     = kMoverType;
            world2.AddComponent(e2, sc);
        }

        ScriptSystem leaky(rt);
        leaky.StartWorld(world2);
        assert(leaky.ActiveInstanceCount() == 1);
        // 故意不 StopWorld —— leaky 析构时 ReleaseAllInstances 兜底。
    }
    std::fprintf(stdout, "  [PASS] destructor releases leftover handles (no StopWorld)\n");

    // --- B1.3 fieldOverrides：authored 值在 OnStart 前经托管反射注入 public 字段 ---
    // Tweakable.Speed 默认 1.0，OnUpdate 每帧 position.x += Speed。
    //   * 带 override Speed="2.5"：4 帧后 x == 10.0（2.5×4）；若 override 没生效
    //     会落到默认 1.0×4 == 4.0；
    //   * 不带 override（对照）：4 帧后 x == 4.0（证明默认值 + 测试非空）。
    using Orange::Engine::Script::ScriptFieldType;
    const std::string kTweakableType = "OrangeFixtures.Tweakable, ScriptFixtures";

    // (a) 带 override 的实体 —— 单独 World 隔离活实例计数。
    {
        World world3;
        Entity e3 = world3.CreateEntity();
        world3.AddComponent(e3, TransformComponent{});  // 原点
        {
            ScriptComponent sc;
            sc.assemblyPath = kFixturesAssembly;
            sc.typeName     = kTweakableType;
            sc.fieldOverrides.push_back({"Speed", ScriptFieldType::Float, "2.5"});
            world3.AddComponent(e3, sc);
        }

        ScriptSystem scripts3(rt);
        scripts3.StartWorld(world3);
        assert(scripts3.ActiveInstanceCount() == 1);
        for (int i = 0; i < 4; ++i)
        {
            scripts3.Tick(world3, 1.0f);
        }
        {
            const auto* t = world3.GetComponent<TransformComponent>(e3);
            assert(t != nullptr);
            if (!ApproxEqual(t->position.x, 10.0f))
            {
                std::fprintf(stderr,
                    "  [FAIL] override Speed=2.5 后 4 帧 position.x = %f（期望 10.0；"
                    "若得 4.0 说明 override 未生效）\n", t->position.x);
                return 1;
            }
        }
        scripts3.StopWorld(world3);
        std::fprintf(stdout,
            "  [PASS] fieldOverrides applied: Speed=2.5 -> position.x = 10.0\n");
    }

    // (b) 对照：不带 override，默认 Speed=1.0 —— 4 帧后 x == 4.0。
    {
        World world4;
        Entity e4 = world4.CreateEntity();
        world4.AddComponent(e4, TransformComponent{});
        {
            ScriptComponent sc;
            sc.assemblyPath = kFixturesAssembly;
            sc.typeName     = kTweakableType;
            // 故意不加 fieldOverrides —— 验证默认值路径。
            world4.AddComponent(e4, sc);
        }

        ScriptSystem scripts4(rt);
        scripts4.StartWorld(world4);
        assert(scripts4.ActiveInstanceCount() == 1);
        for (int i = 0; i < 4; ++i)
        {
            scripts4.Tick(world4, 1.0f);
        }
        {
            const auto* t = world4.GetComponent<TransformComponent>(e4);
            assert(t != nullptr);
            if (!ApproxEqual(t->position.x, 4.0f))
            {
                std::fprintf(stderr,
                    "  [FAIL] 无 override 默认 Speed=1.0 后 4 帧 position.x = %f（期望 4.0）\n",
                    t->position.x);
                return 1;
            }
        }
        scripts4.StopWorld(world4);
        std::fprintf(stdout,
            "  [PASS] no override default: Speed=1.0 -> position.x = 4.0\n");
    }

    std::fprintf(stdout, "[script_system_test] all tests passed.\n");
    return 0;
}
