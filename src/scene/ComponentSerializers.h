#ifndef ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H
#define ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H

// ---------------------------------------------------------------------------
// src 内部头（不安装到 include/）。
//
// 暴露内置组件调度表与 backend-dependent 组件的 Read 辅助函数。
// 公共类型（ComponentSerializerEntry / SaveContext / LoadContext 等）
// 已迁移到 include/orange/engine/scene/ComponentSerializerEntry.h；
// 本头仅保留内部实现所需的声明。
// ---------------------------------------------------------------------------

#include "orange/engine/scene/ComponentSerializerEntry.h"

#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine::Physics
{
struct RigidBodyComponent;
struct ColliderComponent;
}  // namespace Orange::Engine::Physics

namespace Orange::Engine::Scene
{

const std::vector<ComponentSerializerEntry>& GetBuiltinComponentSerializers();

// ---------------------------------------------------------------------------
// Backend-dependent 组件的 Read 辅助函数。
// SceneSerialization Pass 2 用这些 helper 把 desc 读到本地 var，
// 再统一调用 PhysicsWorld::AddBody / AnimatorRegistry::Create 完成 backend 绑定。
// 返回 false 表示数据格式坏（必填字段缺失 / 类型不匹配）。
// ---------------------------------------------------------------------------

bool ReadRigidBodyDesc(const JsonReader& reader,
                       std::string_view  componentPath,
                       Physics::RigidBodyComponent& out);

bool ReadColliderDesc(const JsonReader& reader,
                      std::string_view  componentPath,
                      Physics::ColliderComponent& out);

// Animator 的持久化数据仅是 backend 名字；具体初始化参数由游戏端在
// AnimatorRegistry 注册 factory 时 capture，序列化层不下钻。
bool ReadAnimatorBackendName(const JsonReader& reader,
                             std::string_view  componentPath,
                             std::string&      outBackendName);

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H
