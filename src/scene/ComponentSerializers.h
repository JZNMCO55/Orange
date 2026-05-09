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

namespace Orange::Engine::Scene
{

using EntityToPersistentId = std::unordered_map<Entity, std::int64_t>;
using PersistentIdToEntity = std::vector<Entity>;

// Save / Load 路径透传给每个组件的上下文。registry 可以是 nullptr——
// 仅 pure-data 组件（不需要资源路径反查）的 scene 文件可以无 registry
// 完成 round-trip；持有 AssetHandle 的组件遇到 nullptr 时退化为"写空
// 路径 / 读不到资源"的 graceful 路径，不视为 fatal。
struct SaveContext
{
    const World&                       world;
    const EntityToPersistentId&        entityToId;
    const Asset::AssetRegistry*        assetRegistry;
};

struct LoadContext
{
    World&                             world;
    const PersistentIdToEntity&        idToEntity;
    Asset::AssetRegistry*              assetRegistry;
};

struct ComponentSerializerEntry
{
    std::string_view name;

    bool (*Has)(const World& world, Entity entity);

    void (*Write)(JsonWriter& writer,
                  std::string_view componentPath,
                  Entity entity,
                  const SaveContext& ctx);

    // 返回 false 表示 component 数据格式坏（缺必填字段、类型错）。
    bool (*Read)(const JsonReader& reader,
                 std::string_view componentPath,
                 Entity entity,
                 const LoadContext& ctx);
};

const std::vector<ComponentSerializerEntry>& GetBuiltinComponentSerializers();

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SRC_SCENE_COMPONENT_SERIALIZERS_H
