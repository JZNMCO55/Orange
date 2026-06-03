#ifndef ORANGE_ENGINE_SCRIPT_SCRIPTCOMPONENT_H
#define ORANGE_ENGINE_SCRIPT_SCRIPTCOMPONENT_H

// ---------------------------------------------------------------------------
// ScriptComponent —— 把一个托管 OrangeScript 子类挂到实体上的纯数据组件
// （ADR-017 方案 A）。
//
// 对标 Unity 的 MonoBehaviour 引用：实体在 Play 时由 ScriptSystem 按本组件
// 的 (assemblyPath, typeName) 实例化对应 C# 脚本对象，并按生命周期回调
// （OnStart / OnUpdate / OnDestroy）驱动。
//
// **零 CLR 依赖、始终编译**：本头是 POD-ish 纯数据，**不**受
// ORANGE_ENGINE_WITH_DOTNET 门控——这样含 ScriptComponent 的场景在没开
// dotnet 的构建里也能 Save / Load round-trip（只是不会被实际运行）。运行脚本
// 的能力（ScriptSystem + ScriptRuntime）才是 dotnet-gated。
//
// 字段语义：
//   * assemblyPath —— game assembly（含脚本类型）的路径。运行期由
//     ScriptRuntime::CreateInstance 解析；序列化时按字符串原样落盘。
//   * typeName     —— assembly-qualified 类型全名，如
//                     "OrangeFixtures.Mover, ScriptFixtures"。
//
// fieldOverrides（Inspector tweakable，ADR-017 列入 B1.3）暂不在本组件内
// 落地——留作后续 schema bump 的 additive 扩展。
// ---------------------------------------------------------------------------

#include <orange/engine/core/Serialization.h>

#include <string>

namespace Orange::Engine::Script
{

// ScriptComponent 的 schema 版本。序列化语义随 component 自身字段演进，与
// scene/world 顶层 schema 解耦（顶层只负责 component 的有无与 forward-compat
// 跳过）。出厂版本冻结：只加新字段 + bump minor，已 shipped 字段语义不改。
inline const SchemaVersion& ScriptComponentSchemaVersion()
{
    static const SchemaVersion kVersion{"component/Script", 1, 0};
    return kVersion;
}

struct ScriptComponent
{
    std::string assemblyPath;
    std::string typeName;
};

}  // namespace Orange::Engine::Script

#endif  // ORANGE_ENGINE_SCRIPT_SCRIPTCOMPONENT_H
