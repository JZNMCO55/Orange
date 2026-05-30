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

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_ENTITY_GUID_H
