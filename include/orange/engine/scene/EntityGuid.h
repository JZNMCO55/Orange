#ifndef ORANGE_ENGINE_SCENE_ENTITY_GUID_H
#define ORANGE_ENGINE_SCENE_ENTITY_GUID_H

// ---------------------------------------------------------------------------
// EntityGuid —— 稳定实体身份（GuidComponent）的分配 / 规整化工具（见 ADR-013）。
//
// 分配策略刻意是"惰性 + 规整化兜底"，不在 CreateEntity 即时分配：
//   * EnsureEntityGuids —— 给 world 内尚无 GuidComponent 的实体补全新 GUID，
//     幂等。在需要稳定身份的时刻调用（序列化 Save 前 / prefab 化前）。不依赖
//     每条建实体副路径记得同步——规整化自愈。
//   * ReassignEntityGuids —— 给指定实体强制分配全新 GUID（覆盖已有）。子树
//     clone（Duplicate / Copy-Paste / 任何 SaveSubtree→LoadFromString）后，
//     消费方**必须**对新建实体调用它，否则 clone 与源共享 GUID，破坏唯一性。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Guid.h>
#include <orange/engine/scene/Entity.h>

#include <cstddef>
#include <span>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Scene
{

// 给 world 内所有尚无 GuidComponent 的实体补一个全新稳定 GUID。已有的不动
// （幂等）。返回本次新分配的实体数。
ORANGE_ENGINE_API std::size_t EnsureEntityGuids(World& world);

// 给 entities 中每个有效实体强制分配全新 GUID（覆盖任何已有 GuidComponent）。
// clone 后调用以保证克隆体不与源共享身份。返回实际处理的实体数。
ORANGE_ENGINE_API std::size_t ReassignEntityGuids(World& world,
                                                  std::span<const Entity> entities);

// clone 后"身份分离"一站式入口：把一次子树 clone（SaveSubtreeToString →
// LoadFromString）新建出来的 created 实体与源彻底解耦。做两件事：
//
//   1) ReassignEntityGuids(world, created) —— 给每个有 GuidComponent 的克隆体
//      换全新 per-entity GUID（无 GuidComponent 的实体不受影响，幂等安全）。
//   2) 重映射 PrefabInstanceComponent.instanceId —— blob 字节保真复制了源的
//      instanceId（"哪一次实例化"的分组 key）。clone 出来的是**新的**一次实例
//      化，必须换新 instanceId，否则会被误认为与源实例属于同一次实例化（整组
//      选中 / 删除 / 未来 override 都会错连）。重映射保留**分组关系**：同一旧
//      instanceId 的克隆体共享同一个新 instanceId；不同旧 instanceId 映射到
//      不同新 instanceId（覆盖"clone 一棵含多个独立实例的子树"的情形）。
//      sourcePrefabPath / isInstanceRoot 不动（仍指向同一源 prefab、根标记不变）。
//
// 返回实际换过 instanceId 的实体数（仅统计带 PrefabInstanceComponent 的）。
// Duplicate / Copy-Paste 等消费方在 clone 完成后调用本函数即可。
ORANGE_ENGINE_API std::size_t SeparateClonedIdentities(World& world,
                                                       std::span<const Entity> created);

// 按稳定身份 GUID 反查实体——A2.2 prefab 实例↔模板锚定 / A2.3 PIE world-clone / 跨会话
// 引用解析的公共底座（见 docs/a2-entity-guid-stable-identity-design.md S2）。线性扫描所有
// 挂 GuidComponent 的实体，返回首个 guid 匹配者；无匹配 → Entity::Invalid()。guid 非法
//（全 0 = 未分配）直接判负，不会匹配到恰好未分配的实体。**只读**（const World&），不分配、
// 不改任何状态、不预判身份方案迁移（与 A2.1 主键迁移正交）。非 hot-path（load / link 时
// 调用，非每帧），线性扫描足够；将来若成热点再加缓存索引（guid→entity map）。
ORANGE_ENGINE_API Entity FindEntityByGuid(const World& world, const Core::Guid& guid);

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_ENTITY_GUID_H
