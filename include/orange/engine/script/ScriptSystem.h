#ifndef ORANGE_ENGINE_SCRIPT_SCRIPTSYSTEM_H
#define ORANGE_ENGINE_SCRIPT_SCRIPTSYSTEM_H

// ---------------------------------------------------------------------------
// ScriptSystem —— 把 World 上所有挂 ScriptComponent 的实体批量接进
// ScriptRuntime 的生命周期（ADR-017 方案 A，B1.2）。
//
// 这是 EnterPlay S4 将要调用的运行时机制：进 Play 时为每个带脚本组件的实体
// 实例化对应 C# 对象 + OnStart，Play tick 里逐帧 OnUpdate，Stop 时 OnDestroy
// + 释放。脚本对 ECS 的改动由编辑器既有的 Play 快照回滚——脚本侧不需自做
// undo（见 ADR-017 §生命周期接入）。
//
// **门面纯净**：ScriptSystem 只经 ScriptRuntime 的公共 API 驱动脚本，**不**
// 消费任何 CLR / hostfxr 头（那些只在 src/script/dotnet/** 出现）。本头与
// .cpp 都不暴露 CLR 类型；脚本实例句柄用不透明 ScriptInstanceHandle 表达。
//
// 生命周期纪律：StartWorld → Tick* → StopWorld 配对调用。ScriptSystem 不拥有
// ScriptRuntime（editor 持一个 runtime，多个 World / 多次 Play 复用同一 CLR），
// 只引用它。析构时若 map 仍有残留 handle（漏调 StopWorld）会 Release 兜底防
// GCHandle 泄漏，但**不**调 OnDestroy（StopWorld 才是正常退出路径）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/script/ScriptRuntime.h>

#include <unordered_map>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Script
{

class ORANGE_ENGINE_API ScriptSystem
{
public:
    // 不拥有 runtime —— 引用一个已（或将）由调用方 Initialize 的 ScriptRuntime。
    // runtime 的生命周期必须覆盖本 ScriptSystem 的整个使用期。
    explicit ScriptSystem(ScriptRuntime& runtime);
    ~ScriptSystem();

    // 不可拷贝（持有 entity→handle 的所有权语义）；可移动。
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;
    ScriptSystem(ScriptSystem&&) noexcept = default;
    ScriptSystem& operator=(ScriptSystem&&) noexcept = default;

    // 进 Play：遍历 world 上所有 ScriptComponent，为每个实体实例化 C# 脚本
    // 对象（CreateInstance）+ OnStart。CreateInstance 失败的实体跳过并 warn，
    // 不中断其余实体。重复 StartWorld（未先 StopWorld）会先把上一批清掉再重建。
    void StartWorld(World& world);

    // Play tick：对所有活脚本实例调 OnUpdate(dt)。dt 单位秒。无活实例时 no-op。
    void Tick(World& world, float dt);

    // 退 Play：对所有活脚本实例调 OnDestroy + Release，清空内部 map。
    void StopWorld(World& world);

    // 当前持有的活脚本实例数（测试 / 诊断用）。
    std::size_t ActiveInstanceCount() const noexcept { return mInstances.size(); }

private:
    // 释放 map 内所有 handle（Release，不调 OnDestroy）后清空。析构兜底 +
    // StartWorld 重建前清场共用。
    void ReleaseAllInstances();

    ScriptRuntime* mpRuntime;

    // entity → 该实体的脚本实例句柄。Entity 有 std::hash 特化（Entity.h）。
    std::unordered_map<Entity, ScriptInstanceHandle> mInstances;
};

}  // namespace Orange::Engine::Script

#endif  // ORANGE_ENGINE_SCRIPT_SCRIPTSYSTEM_H
