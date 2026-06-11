#ifndef ORANGE_EDITOR_PREFAB_OVERRIDE_UI_H
#define ORANGE_EDITOR_PREFAB_OVERRIDE_UI_H

// ---------------------------------------------------------------------------
// PrefabOverrideUI —— C1.1 prefab override 编辑器消费层的逻辑单点
// （schema-first：把 prefab override 的 UI 消费逻辑抽成独立 TU，不堆进
// SchemaInspector / EditorRenderLayer mega-class 的函数体；与 EditorPrefabActions
// 同口径）。
//
// 引擎层地基（已 commit，本 TU 只消费、不重写）：
//   * CS1 diff      —— Scene::ComputeInstanceOverrides（实例↔模板字段级 diff）
//   * CS2 refresh    —— Scene::RefreshInstanceWithRecordedOverrides
//   * 持久化 override —— Scene::{Record,Is,Clear}OverridePath + PrefabInstanceComponent
//                       .overriddenPaths
//   * C1.3 revert    —— Scene::RevertInstanceOverridePath
//   * C1.3 apply     —— Scene::ApplyInstanceToTemplate（推回模板 blob）
//
// 路径粒度的关键约束（见 .cpp 头注释「override path 粒度」节）：引擎层的
// override path 是**序列化叶子粒度**（"componentName/fieldPath"，fieldPath 是
// JSON 叶子路径，如 Vec3 的 "position/0"）；编辑器的 schema 字段是**整字段
// 粒度**（prop.name，如 "position"）。本 TU 用 ComputeInstanceOverrides 派生
// 出权威叶子集，从而：
//   * 记录（SyncRecordedOverrides）写入的 overriddenPaths 与 revert/refresh 消费
//     的叶子粒度严格一致（不靠手拼路径猜叶子名）；
//   * 蓝条查询（IsFieldOverridden）按"prop.name 作为叶子路径前缀"匹配，覆盖
//     标量字段（精确相等）与 Vec/Quat 字段（多叶子共用一个 prop 前缀）。
// ---------------------------------------------------------------------------

#include <orange/engine/scene/Entity.h>

#include <string_view>

struct EditorHost;

namespace Orange::Engine::Asset
{
class PrefabAsset;
}

namespace Orange::Editor::Prefab
{

// 该实体是否是 prefab 实例（挂 PrefabInstanceComponent 且 templateEntityGuid 有效，
// 即能与模板配对——这是 override / 蓝条 / revert / refresh 的前提）。非 prefab
// 实例的实体在 Inspector 里完全不受 C1.1 影响。
bool IsPrefabInstance(EditorHost& host, Orange::Engine::Entity entity);

// 选中实体当帧的 override 叶子集缓存刷新：用 Scene::ComputeInstanceOverrides
// 计算 instance↔template 的真实 diff，缓存到本 TU module-level（按 entity + 帧）。
// 蓝条查询（IsFieldOverridden）与记录（SyncRecordedOverrides）共享同一缓存，
// 一帧一个 prefab 实例只算一次（一次 LoadFromString 模板 blob）。
//
// 非 prefab 实例 → 清空缓存（蓝条全 false）。在 DrawEntityViaSchemas 进入字段
// 渲染前调一次即可。
void BeginFrameForEntity(EditorHost& host, Orange::Engine::Entity entity);

// 蓝条查询：当前缓存的 override 叶子集里，是否存在以 "componentName/propName"
// 为前缀的叶子（标量精确相等；Vec/Quat 的子叶子 "propName/0".. 视为命中）。
// nested prop（schema prop.name 形如 "desc.emissionRate"）的 '.' 在内部转成 '/'
// 再比对，与序列化叶子路径对齐。BeginFrameForEntity 未对本 entity 算过 → 返回 false。
bool IsFieldOverridden(std::string_view componentName, std::string_view propName);

// 记录钩子：把实例当前的真实 diff（ComputeInstanceOverrides 叶子集）同步写入
// PrefabInstanceComponent.overriddenPaths（多退少补，幂等）。用户在 Inspector
// 改了 prefab 实例字段后调一次——这样 overriddenPaths 持久化的显式 override 集
// 始终等于"实例相对模板真实改过的叶子"，喂给 refresh / 重开场景后的蓝条。
//
// 复用 BeginFrameForEntity 当帧缓存（不重复 LoadFromString）。非 prefab 实例 /
// 无缓存 → no-op。返回 true 表示 overriddenPaths 实际发生了变化（用于调用方
// 决定是否标脏；当前 SetFieldValueCommand 已自行标脏，本返回值仅供测试）。
bool SyncRecordedOverrides(EditorHost& host, Orange::Engine::Entity entity);

// 解析实例实体锚定的源 prefab → 加载并返回 const PrefabAsset*（registry 持有，
// 调用方即用即弃不可缓存跨 Unload）。非 prefab 实例 / 无 sourcePrefabPath /
// registry 缺失 / Load 失败 → nullptr。revert / 蓝条 / refresh 共用。
const Orange::Engine::Asset::PrefabAsset* ResolveTemplate(
    EditorHost& host, Orange::Engine::Entity entity);

// ---- revert / apply / refresh 动作（右键菜单消费） -----------------------

// 把单个字段回退到模板值（消费 Scene::RevertInstanceOverridePath，对 Vec/Quat
// 逐叶子 revert）。componentName = schema.typeName；propName = schema prop.name
// （内部把 '.' 转 '/'）。
//
// **不进命令栈 / 不可 Undo**：引擎层无 typed by-path 标量逆写原语，type-erased
// 的精确 Undo 做不出来（见 .cpp 头注释）；与 apply/refresh 同口径作显式动作。
// 改 Transform 时 invalidate Euler 缓存。非 prefab 实例 / 无模板 / 该字段无
// override → no-op 返回 false。成功返回 true。
bool RevertField(EditorHost& host, Orange::Engine::Entity entity,
                 std::string_view componentName, std::string_view propName);

// 把实例所有被 override 的字段全部回退到模板值（逐叶子 RevertInstanceOverridePath）。
// 同 RevertField 的口径：**不进命令栈 / 不可 Undo**。非 prefab 实例 / 无模板 /
// 无任何 override → no-op 返回 false。成功（至少回退一条）返回 true。
bool RevertAllFields(EditorHost& host, Orange::Engine::Entity entity);

// 把实例当前态推回模板（消费 Scene::ApplyInstanceToTemplate + PrefabLoader::Save
// 重写 .prefab.json + AssetRegistry reload + 失效缩略图）。**纯 IO + 资产层动作，
// 不进命令栈**（与 CommitNewPrefabFile 同口径——改磁盘上的 .prefab 文件不是
// world mutate，无法用 SetField 式 Undo 安全回滚；见 .cpp 头注释「apply 不可
// Undo」节）。成功返回 true。
bool ApplyInstanceToPrefab(EditorHost& host, Orange::Engine::Entity entity);

// 刷新实例：用持久化 overriddenPaths 当显式 override 集，从（演进后的）模板
// 重拉非 override 字段（消费 Scene::RefreshInstanceWithRecordedOverrides）。
// **不进命令栈 / 不可 Undo**（缺 typed in-place 逆写原语，与 apply/revert 同
// 口径作显式动作；见 .cpp 头注释）。非 prefab 实例 / 无模板 → no-op 返回
// false。成功返回 true。
bool RefreshInstanceFromPrefab(EditorHost& host, Orange::Engine::Entity entity);

}  // namespace Orange::Editor::Prefab

#endif  // ORANGE_EDITOR_PREFAB_OVERRIDE_UI_H
