#ifndef ORANGE_ENGINE_SCENE_PREFAB_INSTANCE_COMPONENT_H
#define ORANGE_ENGINE_SCENE_PREFAB_INSTANCE_COMPONENT_H

// ---------------------------------------------------------------------------
// PrefabInstanceComponent —— 把一个实体标记为某 prefab 资源的实例化产物。
//
// 一次 InstantiatePrefab 会克隆 prefab 模板的整棵子树；本组件挂在该实例的
// **每个**实体上，把它们绑回源 prefab 资源 + 标记它们属于同一次实例化。
//
// 字段语义：
//   * sourcePrefabPath —— 源 prefab 资源路径（.prefab.json）。跨会话稳定，
//     仿 RenderableComponent.mesh 走 AssetRegistry::PathOf 反查得到——序列化
//     落盘的是路径而非运行时 handle，重开场景时按路径重新认领源 prefab。
//   * instanceId —— 一次实例化分配一个 Guid，挂在该实例所有实体上。用来把
//     "同一次实例化出来的实体"聚成一组（未来 override / 整体删除 / 选中
//     整组的 key）。不同次实例化得到不同 instanceId。注意它与 GuidComponent
//     的 per-entity 身份 GUID 是两码事：GuidComponent 标识单个实体，本字段
//     标识"哪一次实例化"。
//   * isInstanceRoot —— 仅实例根实体（克隆出来后 HierarchyComponent.parent
//     为 Invalid 的那个）为 true，其余后代为 false。便于"对实例做整体操作时
//     先定位根"。
//
// MVP 不做：override（实例相对模板的局部修改记录）/ 嵌套 prefab / 实例 GUID
// 与模板 GUID 的映射表。这些都是后续 milestone 的事；本组件字段一次冻结，
// 后续扩字段走 scene/world schema 的 minor bump。
// ---------------------------------------------------------------------------

#include <orange/engine/core/Guid.h>

#include <string>

namespace Orange::Engine::Scene
{

struct PrefabInstanceComponent
{
    std::string sourcePrefabPath;
    Core::Guid  instanceId;
    bool        isInstanceRoot{false};
};

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_PREFAB_INSTANCE_COMPONENT_H
