#ifndef ORANGE_ENGINE_SCENE_GUID_COMPONENT_H
#define ORANGE_ENGINE_SCENE_GUID_COMPONENT_H

// ---------------------------------------------------------------------------
// GuidComponent —— 给实体挂一个跨会话稳定身份（见 ADR-013）。
//
// 字段语义：
//   * guid —— 128-bit 稳定唯一标识。全 0 视为未分配。
//
// 设计取舍：为什么独立 component 而不是 HierarchyComponent 加字段 / World map？
//   * 身份与父子拓扑无关，塞进 HierarchyComponent 是耦合；
//   * 独立 component 可选挂载、archetype 友好，且**直接复用现有 optional
//     component 序列化机制**（与 LayerComponent / PointLight 同款）。
//
// 分配不在 CreateEntity 即时发生（World 不知道 Scene 概念）——由
// Scene::EnsureEntityGuids 在需要稳定身份时（序列化 / prefab 化前）惰性补全，
// clone 后由 Scene::ReassignEntityGuids 换新。见 EntityGuid.h。
// ---------------------------------------------------------------------------

#include <orange/engine/core/Guid.h>

namespace Orange::Engine::Scene
{

struct GuidComponent
{
    Core::Guid guid;
};

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_GUID_COMPONENT_H
