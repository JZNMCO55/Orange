#ifndef ORANGE_ENGINE_SCENE_PREFAB_OVERRIDE_H
#define ORANGE_ENGINE_SCENE_PREFAB_OVERRIDE_H

// ---------------------------------------------------------------------------
// PrefabOverride —— prefab 实例↔模板的字段级 override diff（只读、决策中立）。
//
// 给定一个 prefab 实例实体（带 PrefabInstanceComponent.templateEntityGuid）+
// 它的模板，diff 出该实例相对模板被改了哪些 component / 字段。返回的是
// **被 override 的字段路径集**（field 级粒度），供蓝条标记 / revert / refresh
// 等上层消费。
//
// 设计立场（c1-prefab-override-design.md §2 选项 A / §3 CS1）：
//   * **只读、不改任何存储、不改 scene schema**。override 在选项 A 下是派生
//     计算，不预判 C1.0 ADR 最终选存储模型 A 还是 B——两者都消费本 diff
//     （A 用它做蓝条/revert；B 也要它生成 delta）。故本模块是 C1 的决策中立
//     安全先行件。
//   * diff 复用现有序列化：把实例实体与模板实体的 components 各经已注册的
//     ComponentSerializer 写成 JSON，再结构化逐叶子路径比较。**不重写任何
//     component 的序列化逻辑**。
//
// 过滤"身份/链接"component（见 .cpp 的 IsIdentityComponent）：Guid /
// PrefabInstance / Hierarchy 本就该在实例与模板间不同（实例换了新 per-entity
// guid、新 instanceId、互引用指向各自 world 的实体），那些不是业务 override，
// 不报。本期只做"同 entity 字段值 override"，结构性 override（加/删 entity）
// 留后续（design §5 问题 6）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/scene/Entity.h>

#include <string>
#include <vector>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Asset
{
class PrefabAsset;
}

namespace Orange::Engine::Scene
{

// 一条被 override 的字段。
//   * componentName —— scene JSON 里 "components/<name>" 这一级的 component 名
//     （如 "Transform" / "Renderable"）。
//   * fieldPath —— 该 component 内部相对 component 根的叶子路径（如 "position" /
//     "position/0" / "materialInstanceId"）。按 JSON 结构枚举叶子，'/' 分隔，
//     数组元素用数字下标段（与 Core::Serialization 的路径语法一致）。
//
// "被 override" = 叶子值在实例与模板间不同，**或**该叶子只在一侧存在（实例
// 加了模板没有的字段 / 实例缺了模板有的字段，都算 override）。
struct OverrideField
{
    std::string componentName;
    std::string fieldPath;
};

// 底层入口：diff 两个 world 里的两个实体的 component 字段。
//
// instWorld/instEntity = 实例侧；tmplWorld/tmplEntity = 模板侧。返回 instEntity
// 相对 tmplEntity 被 override 的字段集（过滤身份/链接 component 后）。两实体
// 任一无效 → 返回空（不崩）。字段集顺序：按 component 名、再按叶子路径稳定排序，
// 便于测试与上层稳定消费。
ORANGE_ENGINE_API std::vector<OverrideField> ComputeEntityOverrides(
    const World& instWorld, Entity instEntity,
    const World& tmplWorld, Entity tmplEntity);

// 便利入口：给定实例实体 + 它的模板 PrefabAsset，自动配对模板实体并 diff。
//
// 内部：LoadFromString(tmpl.TemplateBlob()) 到一个 scratch world → 取 instEntity
// 的 PrefabInstanceComponent.templateEntityGuid → FindEntityByGuid(scratch, guid)
// 找模板实体 → ComputeEntityOverrides。
//
// 失败 graceful（均返回空，不崩）：instEntity 无效 / 无 PrefabInstanceComponent /
// templateEntityGuid 为空（旧数据） / 模板 blob 载入失败 / 模板里 guid 未命中。
ORANGE_ENGINE_API std::vector<OverrideField> ComputeInstanceOverrides(
    const World& instWorld, Entity instEntity, const Asset::PrefabAsset& tmpl);

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_PREFAB_OVERRIDE_H
