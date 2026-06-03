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
//   * templateEntityGuid —— 该实例实体在**模板内对应实体的 GUID**（逐实体模板
//     锚定，ADR-018 §6 问题 4 裁定"按模板内 guid"）。实例化时 created 实体的
//     GuidComponent 仍是模板 blob 保真的模板 guid，在 ReassignEntityGuids 换新
//     实例 guid **之前**捕获存入本字段；之后实例实体自身换上全新 per-entity
//     guid，但本字段恒指回模板侧那个稳定 guid。用途：prefab override / re-apply
//     时按 guid 在实例↔模板间稳定匹配"哪个实例实体对应哪个模板实体"（顺序 int
//     在两套独立 id 空间间无法跨锚，见 a2-entity-guid-stable-identity-design.md
//     §2 问题 2）。空 guid（全 0）= 未知 / 旧数据（无 GuidComponent 的模板实体
//     或 schema 1.16 及更早落盘的旧实例）。子树 clone（Duplicate / Copy-Paste）
//     不动本字段——克隆体仍对应同一模板实体，模板锚定保持不变。
//
// MVP 不做：override（实例相对模板的局部修改记录）/ 嵌套 prefab。逐实体的实例
// GUID ↔ 模板 GUID 锚定已由 templateEntityGuid 落地（ADR-018 A2.2）。后续扩字段
// 走 scene/world schema 的 minor bump。
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
    Core::Guid  templateEntityGuid;  // 对应模板实体的 GUID；空 = 未知 / 旧数据
};

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_PREFAB_INSTANCE_COMPONENT_H
