// ScriptSystem 的实现 —— World 上所有 ScriptComponent 的批量实例化 + 生命周期
// 驱动（ADR-017 方案 A，B1.2）。
//
// **本文件在 src/script/ 而非 src/script/dotnet/**：它只用 ScriptRuntime 的
// 公共门面（CreateInstance / InvokeStart / InvokeUpdate / InvokeDestroy /
// Release / SetCurrentWorld），不消费任何 CLR / hostfxr 头。门控在
// ORANGE_ENGINE_WITH_DOTNET（依赖 ScriptRuntime），但本 TU 本身无 CLR 依赖。

#include <orange/engine/script/ScriptSystem.h>

#include <orange/engine/core/Log.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/script/ScriptComponent.h>

namespace Orange::Engine::Script
{

    ScriptSystem::ScriptSystem(ScriptRuntime& runtime) : mpRuntime(&runtime)
    {
    }

    ScriptSystem::~ScriptSystem()
    {
        // RAII 兜底：正常路径走 StopWorld（OnDestroy + Release）；若调用方漏调，
        // 析构时至少 Release 残留 handle 防 GCHandle 泄漏（不调 OnDestroy——那是
        // 正常退出语义，非清理语义）。
        ReleaseAllInstances();
    }

    void ScriptSystem::ReleaseAllInstances()
    {
        if (mpRuntime != nullptr)
        {
            for (auto& [entity, handle] : mInstances)
            {
                mpRuntime->Release(handle);
            }
        }
        mInstances.clear();
    }

    void ScriptSystem::StartWorld(World& world)
    {
        // 重复 StartWorld（未先 StopWorld）→ 先清掉上一批残留实例，避免句柄泄漏 +
        // 重复实例化。注意这里只 Release 不 OnDestroy（上一批未走正常退出）。
        if (!mInstances.empty())
        {
            ORANGE_LOG_WARN(
                "ScriptSystem::StartWorld called with {} live instance(s) still tracked; "
                "releasing them before re-instantiating.",
                mInstances.size());
            ReleaseAllInstances();
        }

        // 绑定函数 decode scriptId 后在该 World 上取 / 写组件 —— 实例化前先设好。
        mpRuntime->SetCurrentWorld(&world);

        // 遍历所有挂 ScriptComponent 的实体，逐个实例化 + OnStart。CreateInstance
        // 失败的实体跳过 + warn，不中断其余（一个坏脚本不该拖垮整个 Play）。
        auto& registry = world.Registry();
        auto  view     = registry.view<ScriptComponent>();
        for (auto enttEntity : view)
        {
            const Entity           entity = World::FromEntt(enttEntity);
            const ScriptComponent& sc     = view.get<ScriptComponent>(enttEntity);

            auto instanceResult = mpRuntime->CreateInstance(sc.assemblyPath, sc.typeName, entity);
            if (instanceResult.IsErr())
            {
                ORANGE_LOG_WARN(
                    "ScriptSystem::StartWorld: failed to instantiate script type '{}' from "
                    "assembly '{}' for an entity; skipping it.",
                    sc.typeName, sc.assemblyPath);
                continue;
            }

            const ScriptInstanceHandle handle = instanceResult.Value();
            mInstances.emplace(entity, handle);

            // authored 值在 OnStart 前注入（对标 Unity 序列化字段先于 Start 设好）：
            // 逐条经托管反射写脚本对象的 public 字段。单条失败（字段不存在 / 解析
            // 失败）只 warn 不中断——一个坏 override 不该拖垮整个实体。
            for (const ScriptFieldOverride& ov : sc.fieldOverrides)
            {
                auto setResult = mpRuntime->SetInstanceField(
                    handle, ov.name, static_cast<int>(ov.type), ov.value);
                if (setResult.IsErr())
                {
                    ORANGE_LOG_WARN(
                        "ScriptSystem::StartWorld: failed to apply field override '{}' = '{}' "
                        "on script type '{}'; skipping that override.",
                        ov.name, ov.value, sc.typeName);
                }
            }

            mpRuntime->InvokeStart(handle);
        }
    }

    void ScriptSystem::Tick(World& world, float dt)
    {
        // 每次 tick 重设当前 World：脚本回调期间绑定函数读的就是这个 World
        // （editor 多 World / 重载场景时上下文可能已变）。
        mpRuntime->SetCurrentWorld(&world);

        for (auto& [entity, handle] : mInstances)
        {
            mpRuntime->InvokeUpdate(handle, dt);
        }
    }

    void ScriptSystem::StopWorld(World& world)
    {
        mpRuntime->SetCurrentWorld(&world);

        // 正常退出：每个实例 OnDestroy（让脚本做收尾）再 Release（解 GCHandle 保活）。
        for (auto& [entity, handle] : mInstances)
        {
            mpRuntime->InvokeDestroy(handle);
            mpRuntime->Release(handle);
        }
        mInstances.clear();
    }

} // namespace Orange::Engine::Script
