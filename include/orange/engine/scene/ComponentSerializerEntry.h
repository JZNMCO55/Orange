#ifndef ORANGE_ENGINE_SCENE_COMPONENT_SERIALIZER_ENTRY_H
#define ORANGE_ENGINE_SCENE_COMPONENT_SERIALIZER_ENTRY_H

// ---------------------------------------------------------------------------
// 游戏侧 / 编辑器侧注册自定义组件序列化器所需的公共接口。
//
// 内置组件的注册表在 src/scene/ComponentSerializers.h（私有头，不安装）；
// 外部扩展通过把若干 ComponentSerializerEntry 填入
//   Scene::SaveOptions::extraSerializers
//   Scene::LoadOptions::extraSerializers
// 让 Scene::Save / Scene::Load 识别并调度自定义组件。
//
// 使用示例（游戏侧 / 编辑器侧 main 启动期）：
//
//   static const Orange::Engine::Scene::ComponentSerializerEntry kHealthEntry{
//       .name  = "Health",
//       .kind  = ComponentKind::PureData,
//       .Has   = HealthHas,
//       .Write = HealthWrite,
//       .Read  = HealthRead,
//   };
//
//   Orange::Engine::Scene::LoadOptions opt{
//       .assetRegistry    = &reg,
//       .extraSerializers = std::span{&kHealthEntry, 1},
//   };
//   Scene::Load("assets/scenes/demo.scene.json", world, opt);
// ---------------------------------------------------------------------------

#include <orange/engine/core/Serialization.h>
#include <orange/engine/scene/Entity.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Asset
{
    class AssetRegistry;
}

namespace Orange::Engine::Physics
{
    class PhysicsWorld;
}

namespace Orange::Engine::Animation
{
    class AnimatorRegistry;
}

namespace Orange::Engine::Render
{
    class MaterialInstance;
}

namespace Orange::Engine::Scene
{

    // entity → 持久 ID（0..N-1，按 view 顺序分配）的映射；Save 路径用。
    // 游戏侧 Write 函数若需要把 Entity 引用写成稳定整数，可查这张表。
    using EntityToPersistentId = std::unordered_map<Entity, std::int64_t>;

    // 持久 ID → 新建 entity；Load 路径用。
    // 游戏侧 Read 函数按持久 ID 反查对应 entity。
    using PersistentIdToEntity = std::vector<Entity>;

    // Save 路径透传给每个组件 Write 函数的上下文。
    // 各指针均可空——实现方应对 nullptr 做 graceful 退化。
    struct SaveContext
    {
        const World&                world;
        const EntityToPersistentId& entityToId;
        const Asset::AssetRegistry* assetRegistry;

        // 按名字注册的 MaterialInstance 表（name → non-owning pointer）。
        // 供 RenderableComponent::Write 把 materialInstance* 反查为 id 字符串。
        // 空 → 持有 materialInstance 的组件写出空 id + warn。
        const std::unordered_map<std::string, Render::MaterialInstance*>* namedMaterialInstances{nullptr};
    };

    // Load 路径透传给每个组件 Read 函数的上下文。
    struct LoadContext
    {
        World&                             world;
        const PersistentIdToEntity&        idToEntity;
        Asset::AssetRegistry*              assetRegistry;
        Physics::PhysicsWorld*             physicsWorld;
        const Animation::AnimatorRegistry* animatorRegistry;

        // 与 SaveContext::namedMaterialInstances 相同表；Load 路径按 id 正向
        // 查找 MaterialInstance*，赋给 RenderableComponent::materialInstance。
        const std::unordered_map<std::string, Render::MaterialInstance*>* namedMaterialInstances{nullptr};

        // namedMaterialInstances 查不到某个 materialInstanceId 时的兜底解析器。
        // 设计动机：namedMaterialInstances 在编辑器启动期是 one-shot snapshot，
        // 只含内置 / showcase + 本 session 已 lazy 过的 user material；DCC 导入
        // 产生的 assets/<Type>/*.material 在新 session 里不在表内，导致重启后
        // 加载场景时 materialInstance 解析失败留 null（Inspector 显示 None）。
        // 而 mesh 走 AssetRegistry::Load 从磁盘按路径加载，不受此影响——二者
        // 不对称。编辑器把本 resolver 接到 EnsureMaterialInstance（按 .material
        // 路径从磁盘 lazy-create + 注册），令 material 解析与 mesh 对称。
        // 空 → 维持旧行为（查表失败即留 null）。
        std::function<Render::MaterialInstance*(const std::string& materialId)>
            materialResolver{};

        // guid 字符串（GuidComponent::guid 的 ToString 形态，32 hex）→ 本次 Load
        // 预创建出的 Entity 的反查表（A2 主键迁移 / ADR-018）。Load 主流程在创建完
        // 所有实体后、回填 component 前一次性建好，供 ReadHierarchy 等"互引用解析优
        // 先 guid、回退顺序 int"。键用 guid 字符串而非 Core::Guid，避免给 Core::Guid
        // 加全局 std::hash 特化（侵入公共面）；空 / 非法 guid 的实体不入表。
        // 空 → 读端无 guid 索引可用，互引用全回退顺序 int（读旧文件 / guid 缺失）。
        const std::unordered_map<std::string, Entity>* guidToEntity{nullptr};
    };

    // 区分 Pass 1（纯数据，无 backend 依赖）与 Pass 2（需先建 backend 再 attach）。
    // 游戏侧自定义组件几乎都应填 PureData；BackendDependent 仅在组件初始化
    // 必须先有物理 body / 动画 backend 等外部资源时才用。
    enum class ComponentKind : std::uint8_t
    {
        PureData,
        BackendDependent,
    };

    // 一条组件序列化器描述符。
    //
    // 函数指针签名约定：
    //   Has   —— entity 是否持有该组件（Save 路径过滤用）。
    //   Write —— 在 componentPath 下写出组件全部字段。
    //   Read  —— 从 componentPath 读取并 attach 到 entity；返回 false 表示
    //            数据格式坏（缺必填字段 / 类型不匹配），调用方整体回滚 Load。
    //            组件 JSON 段不存在视为"此实体不挂该组件"，由 Load 在调用
    //            Read 前以 JsonReader::Has 过滤，不会无谓触发 Read。
    //
    // SchemaVersion 由实现方自行在 Write / Read 内部处理（参 CLAUDE.md 序列化
    // 规约）；ComponentSerializerEntry 本身不承载 schema version。
    struct ComponentSerializerEntry
    {
        // scene JSON 里 "components/<name>" 这一级的 key。
        // 不得与内置组件名重复（Save / Load 检测到冲突时返回 AlreadyExists）。
        std::string_view name;

        ComponentKind kind;

        bool (*Has)(const World& world, Entity entity);

        void (*Write)(JsonWriter&        writer,
                      std::string_view   componentPath,
                      Entity             entity,
                      const SaveContext& ctx);

        // PureData：在 Pass 1 被调用。
        // BackendDependent（extra）：在 Pass 2 被调用，直接通过本指针分派。
        bool (*Read)(const JsonReader&  reader,
                     std::string_view   componentPath,
                     Entity             entity,
                     const LoadContext& ctx);
    };

} // namespace Orange::Engine::Scene

#endif // ORANGE_ENGINE_SCENE_COMPONENT_SERIALIZER_ENTRY_H
