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
//   * fieldOverrides —— Inspector tweakable，authored 值列表（B1.3 已落地）。
//     实例化后、OnStart 前由 ScriptSystem 经托管反射逐条写入脚本对象的
//     public 字段（对标 Unity 序列化字段先于 Start 设好）。
// ---------------------------------------------------------------------------

#include <orange/engine/core/Serialization.h>

#include <string>
#include <vector>

namespace Orange::Engine::Script
{

// ScriptComponent 的 schema 版本。序列化语义随 component 自身字段演进，与
// scene/world 顶层 schema 解耦（顶层只负责 component 的有无与 forward-compat
// 跳过）。出厂版本冻结：只加新字段 + bump minor，已 shipped 字段语义不改。
// 1.0 → 1.1：additive 加 fieldOverrides 数组（B1.3）。
inline const SchemaVersion& ScriptComponentSchemaVersion()
{
    static const SchemaVersion kVersion{"component/Script", 1, 1};
    return kVersion;
}

// authored 值的类型标签：值统一以字符串持久化，运行期在 C# 边界按本 tag 解析
// 后用 System.Reflection 设脚本对象的 public 字段。落盘时存 int 形态（见
// ScriptFieldOverride::type），读回时校验范围越界 graceful 取 Float。
enum class ScriptFieldType
{
    Float,   // 0
    Int,     // 1
    Bool,    // 2
    String,  // 3
};

// 一条 tweakable 字段覆盖。value 始终是字符串形态：Float→如 "2.5"、
// Int→"3"、Bool→"true"/"false"、String→原文。统一字符串的理由：
//   * 最简 native↔managed ABI（一条 const char* 过界，无需多套 marshal）；
//   * 零浮点精度坑（不在 C++/C# 两侧各自 parse-print 损精度）；
//   * JSON 落盘可读、可手改。
struct ScriptFieldOverride
{
    std::string     name;                          // 目标 public 字段名
    ScriptFieldType type = ScriptFieldType::Float; // authored 值的类型标签
    std::string     value;                         // 字符串形态的 authored 值
};

struct ScriptComponent
{
    std::string assemblyPath;
    std::string typeName;

    // Inspector tweakable：authored 值列表。实例化后、OnStart 前注入脚本对象。
    std::vector<ScriptFieldOverride> fieldOverrides;
};

}  // namespace Orange::Engine::Script

#endif  // ORANGE_ENGINE_SCRIPT_SCRIPTCOMPONENT_H
