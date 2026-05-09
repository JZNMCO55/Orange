#ifndef ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H
#define ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H

// ---------------------------------------------------------------------------
// 组件序列化调度表（src 内部头，不进公共 install）。
//
// 每个内置组件以一条 `ComponentSerializerEntry` 注册到这里：
//   * `name`  —— scene JSON 里 "components/<Name>" 这一级的 key；同时也
//     是 forward-compat 时遇到未识别 key 的判定依据。
//   * `Has`   —— entity 是否拥有该组件（Save 阶段是否要写这条目）。
//   * `Write` —— 在 `componentPath` 这个对象路径下落地组件全部字段。
//   * `Read`  —— 从 `componentPath` 读取并 attach 到 entity；返回 false
//     表示数据存在但格式坏（fatal），调用方应整体回滚。组件不存在
//     视为正常无操作，由调用方先用 reader.Has(componentPath) 过滤。
//
// 当前仅注册 Transform / Hierarchy / Name；后续要新增内置组件时往
// GetBuiltinComponentSerializers() 返回的表里追加，主流程不动。
// ---------------------------------------------------------------------------

#include "orange/engine/core/Serialization.h"
#include "orange/engine/scene/Entity.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Orange::Engine
{
class World;
}  // namespace Orange::Engine

namespace Orange::Engine::Asset
{
class AssetRegistry;
}  // namespace Orange::Engine::Asset

namespace Orange::Engine::Physics
{
class PhysicsWorld;
struct RigidBodyComponent;
struct ColliderComponent;
}  // namespace Orange::Engine::Physics

namespace Orange::Engine::Animation
{
class AnimatorRegistry;
}  // namespace Orange::Engine::Animation

namespace Orange::Engine::Scene
{

using EntityToPersistentId = std::unordered_map<Entity, std::int64_t>;
using PersistentIdToEntity = std::vector<Entity>;

// Save / Load 路径透传给每个组件的上下文。各 registry / world 指针都
// 可空——序列化层针对每个 nullptr 各自走 graceful 退化（写空路径 /
// 装空 backend），不视为 fatal。
struct SaveContext
{
    const World&                          world;
    const EntityToPersistentId&           entityToId;
    const Asset::AssetRegistry*           assetRegistry;
};

struct LoadContext
{
    World&                                world;
    const PersistentIdToEntity&           idToEntity;
    Asset::AssetRegistry*                 assetRegistry;
    Physics::PhysicsWorld*                physicsWorld;
    const Animation::AnimatorRegistry*    animatorRegistry;
};

// 把 backend-dependent 组件（需要先建 PhysicsWorld body / 通过
// AnimatorRegistry::Create 拿 IAnimator 等）与 pure-data 组件分开调度
// 的判别标志。Save 路径忽略 kind，Load 路径分两遍：
//   * Pass 1：PureData，按 kEntries 顺序逐个 Read。
//   * Pass 2：BackendDependent，由 SceneSerialization 主流程按"配对 /
//     先建 backend"的特殊路径处理——不走通用 dispatch。
//
// kind 字段同时让 ComponentSerializers 把 RigidBody / Collider / Animator
// 的 Write 也保留在统一表中（Save 不需要分类），调用方序列化遍历仍是
// 对称的"every entry once per entity"模型。
enum class ComponentKind : std::uint8_t
{
    PureData,
    BackendDependent,
};

struct ComponentSerializerEntry
{
    std::string_view name;
    ComponentKind    kind;

    bool (*Has)(const World& world, Entity entity);

    void (*Write)(JsonWriter& writer,
                  std::string_view componentPath,
                  Entity entity,
                  const SaveContext& ctx);

    // 返回 false 表示 component 数据格式坏（缺必填字段、类型错）。
    // BackendDependent 的 Read 永远返回 true—— Pass 2 不走通用 dispatch，
    // 这里的 Read 字段对它们不调用（保持 nullptr）。
    bool (*Read)(const JsonReader& reader,
                 std::string_view componentPath,
                 Entity entity,
                 const LoadContext& ctx);
};

const std::vector<ComponentSerializerEntry>& GetBuiltinComponentSerializers();

// ---------------------------------------------------------------------------
// Backend-dependent 组件的 Read 辅助函数。SceneSerialization 主流程
// 在 Pass 2 用这些 helper 把 desc 读到本地 var，再统一调用 PhysicsWorld
// ::AddBody / AnimatorRegistry::Create 完成 backend 绑定。
//
// 每个返回 false 表示数据格式坏（必填字段缺失 / 类型不匹配）；调用方
// 视为 fatal 并整体回滚。
// ---------------------------------------------------------------------------

bool ReadRigidBodyDesc(const JsonReader& reader,
                       std::string_view  componentPath,
                       Physics::RigidBodyComponent& out);

bool ReadColliderDesc(const JsonReader& reader,
                      std::string_view  componentPath,
                      Physics::ColliderComponent& out);

// Animator 的"重建数据"目前仅是 backend 名字——具体 backend 需要的
// skeleton 路径 / channel 闭包等参数由调用方在向 AnimatorRegistry 注
// 册 factory 时 capture，序列化层不下钻。
bool ReadAnimatorBackendName(const JsonReader& reader,
                             std::string_view  componentPath,
                             std::string&      outBackendName);

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H
