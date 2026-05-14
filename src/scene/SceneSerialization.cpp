// Scene 序列化主流程。
//
// 顶层 schema 解析 + entity 持久 ID 重映射 + 组件调度，全部在这里编排。
// 单一组件的 Read / Write 在 ComponentSerializers.cpp，通过
// GetBuiltinComponentSerializers() 暴露的表查找。
//
// 头隔离：本 .cpp 不直接 include `<nlohmann/json.hpp>`；走 Core::
// Serialization 公共面。

#include "orange/engine/scene/SceneSerialization.h"

#include "orange/engine/animation/AnimatorComponent.h"
#include "orange/engine/animation/AnimatorRegistry.h"
#include "orange/engine/animation/IAnimator.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/physics/ColliderComponent.h"
#include "orange/engine/physics/PhysicsWorld.h"
#include "orange/engine/physics/RigidBodyComponent.h"
#include "orange/engine/scene/Entity.h"
#include "orange/engine/scene/World.h"

#include "scene/ComponentSerializers.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Orange::Engine::Scene
{
namespace
{

// scene 顶层 schema 版本。一旦交付到玩家手里，major/minor 只增不改——
// 字段新增走 minor bump（向后兼容），结构性破坏走 major bump（reader 拒
// 绝读取，调用方按需路由 migrator）。
const SchemaVersion& SceneSchemaVersion()
{
    static const SchemaVersion kVersion{"scene/world", 1, 1};
    return kVersion;
}

constexpr std::string_view kSchemaVersionPath = "schemaVersion";
constexpr std::string_view kEntitiesPath      = "entities";

std::string EntityBasePath(std::size_t index)
{
    std::string p;
    p.reserve(kEntitiesPath.size() + 1 + 12);
    p.append(kEntitiesPath);
    p.push_back('/');
    p.append(std::to_string(index));
    return p;
}

std::string ComponentPath(const std::string& entityBase, std::string_view componentName)
{
    std::string p;
    p.reserve(entityBase.size() + std::string_view{"/components/"}.size() + componentName.size());
    p.append(entityBase);
    p.append("/components/");
    p.append(componentName);
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

Result<void, ResultCode> Save(const World& world,
                              std::string_view path,
                              const SaveOptions& options)
{
    // 1) 收集所有 live entity，按 view 顺序分配 0..N-1 持久 ID。
    //    EnTT view<entt::entity>() 在 3.13 上即"所有活实体"的迭代源。
    std::vector<Entity>          entityList;
    EntityToPersistentId         idMap;
    {
        auto& reg = world.Registry();
        // 先估个容量再 push——Size() 是 World 自己维护的活实体数。
        entityList.reserve(world.Size());
        idMap.reserve(world.Size());

        std::int64_t persistentId = 0;
        for (auto e : reg.view<entt::entity>())
        {
            const Entity entity = World::FromEntt(e);
            entityList.push_back(entity);
            idMap.emplace(entity, persistentId);
            ++persistentId;
        }
    }

    const SaveContext ctx{world, idMap, options.assetRegistry, options.namedMaterialInstances};

    // 2) 名字冲突检测：extra 不允许与内置 component 同名。
    const auto& serializers = GetBuiltinComponentSerializers();
    for (const auto& extra : options.extraSerializers)
    {
        for (const auto& builtin : serializers)
        {
            if (builtin.name == extra.name)
            {
                ORANGE_LOG_ERROR(
                    "Scene save: extra serializer name '{}' conflicts with builtin; "
                    "use a unique name.",
                    extra.name);
                return ResultCode::AlreadyExists;
            }
        }
    }

    // 3) 组装 JSON。
    JsonWriter writer;
    writer.WriteSchemaVersion(kSchemaVersionPath, SceneSchemaVersion());
    writer.BeginArray(kEntitiesPath, entityList.size());

    for (std::size_t i = 0; i < entityList.size(); ++i)
    {
        const std::string base   = EntityBasePath(i);
        const Entity      entity = entityList[i];

        writer.WriteInt(base + "/id", static_cast<std::int64_t>(i));

        // Save 不分 PureData / BackendDependent——所有有组件的 entity 都
        // 把字段写进 JSON。Pass 2 是 Load 才需要的特殊路径。
        for (const auto& entry : serializers)
        {
            if (entry.Has(world, entity))
            {
                const std::string componentPath = ComponentPath(base, entry.name);
                entry.Write(writer, componentPath, entity, ctx);
            }
        }
        for (const auto& entry : options.extraSerializers)
        {
            if (entry.Has != nullptr && entry.Has(world, entity))
            {
                const std::string componentPath = ComponentPath(base, entry.name);
                if (entry.Write != nullptr)
                {
                    entry.Write(writer, componentPath, entity, ctx);
                }
            }
        }
    }

    // 4) 落盘。
    auto saveResult = writer.SaveToFile(path);
    if (saveResult.IsErr())
    {
        return saveResult.Error();
    }
    return Result<void, ResultCode>{};
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

namespace
{

// 把本次 Load 已经 create 出来的 entity 从 world 里全部回收——失败回滚
// 用。仅回收本次新增的，pre-existing entity 不动。
void RollbackCreatedEntities(World& world, const std::vector<Entity>& created)
{
    // 倒序销毁——主要为可读性；EnTT 内部用稀疏集，顺序对正确性无影响。
    for (auto it = created.rbegin(); it != created.rend(); ++it)
    {
        if (it->IsValid())
        {
            world.DestroyEntity(*it);
        }
    }
}

}  // namespace

Result<void, ResultCode> Load(std::string_view path,
                              World& world,
                              const LoadOptions& options)
{
    // 1) 打开并解析 JSON。
    auto readerResult = JsonReader::FromFile(path);
    if (readerResult.IsErr())
    {
        // ParseError.code 区分 IO（文件不存在 / 不可读）与 parse（语法错）。
        // 这里直接把内层 ResultCode 透传给调用方。
        return readerResult.Error().code;
    }
    const JsonReader& reader = readerResult.Value();

    // 2) 校验 schema 版本——bit-for-bit namespace + major + minor 兼容性。
    auto schemaResult = reader.ReadSchemaVersion(kSchemaVersionPath);
    if (schemaResult.IsErr())
    {
        // 缺 schemaVersion 字段 / 字段格式坏 → 当作 schema mismatch 处理，
        // 不部分写入。
        return ResultCode::SchemaMismatch;
    }
    if (!SceneSchemaVersion().CanRead(schemaResult.Value()))
    {
        return ResultCode::SchemaMismatch;
    }

    // 3) 预创建所有实体并建立 持久 ID → Entity 双向映射。先全部建好再
    //    回填 component，让 Hierarchy 的 parent / firstChild 等字段无论
    //    引用前向或后向兄弟都能命中真 Entity。
    const std::size_t entityCount = reader.ArraySize(kEntitiesPath);
    PersistentIdToEntity idTable;
    idTable.reserve(entityCount);
    std::vector<Entity> created;
    created.reserve(entityCount);

    for (std::size_t i = 0; i < entityCount; ++i)
    {
        // 校验"entities[i].id == i"——当前 schema 下持久 ID 与数组下标
        // 一一对应。错位说明文件被人工编辑成不一致状态，拒绝加载比静默
        // 修补更安全。
        std::int64_t declaredId = -1;
        const std::string base = EntityBasePath(i);
        if (!reader.ReadInt(base + "/id", declaredId)
            || declaredId != static_cast<std::int64_t>(i))
        {
            RollbackCreatedEntities(world, created);
            return ResultCode::InvalidArgument;
        }

        Entity e = world.CreateEntity();
        created.push_back(e);
        idTable.push_back(e);
    }

    const LoadContext ctx{world, idTable,
                          options.assetRegistry,
                          options.physicsWorld,
                          options.animatorRegistry,
                          options.namedMaterialInstances};

    const auto& serializers = GetBuiltinComponentSerializers();

    // 4) 名字冲突检测：extra 不允许与内置 component 同名。
    for (const auto& extra : options.extraSerializers)
    {
        for (const auto& builtin : serializers)
        {
            if (builtin.name == extra.name)
            {
                RollbackCreatedEntities(world, created);
                return ResultCode::AlreadyExists;
            }
        }
    }

    // 5) Pass 1：PureData 组件——按 dispatch 表逐个 Read 并 attach。
    //    Hierarchy 引用 / Renderable 的 mesh path / DirectionalLight 字段
    //    都不依赖 backend，可以直接落地。
    for (std::size_t i = 0; i < entityCount; ++i)
    {
        const std::string base   = EntityBasePath(i);
        const Entity      entity = idTable[i];

        for (const auto& entry : serializers)
        {
            if (entry.kind != ComponentKind::PureData)
            {
                continue;
            }
            const std::string componentPath = ComponentPath(base, entry.name);
            if (!reader.Has(componentPath))
            {
                continue;
            }
            if (!entry.Read(reader, componentPath, entity, ctx))
            {
                // 数据格式坏 → 整体回滚本次 Load，World 回到未加载前状态。
                RollbackCreatedEntities(world, created);
                return ResultCode::InvalidArgument;
            }
        }

        // extra PureData 组件——游戏侧 / 编辑器侧自定义组件在此分派。
        for (const auto& entry : options.extraSerializers)
        {
            if (entry.kind != ComponentKind::PureData)
            {
                continue;
            }
            const std::string componentPath = ComponentPath(base, entry.name);
            if (!reader.Has(componentPath))
            {
                continue;
            }
            if (entry.Read == nullptr || !entry.Read(reader, componentPath, entity, ctx))
            {
                RollbackCreatedEntities(world, created);
                return ResultCode::InvalidArgument;
            }
        }

        // 未识别的 component key（forward-compat 路径）此处理论上要
        // warn + skip。当前 JsonReader 没有"列出对象 key"接口，所以仅
        // 做"跳过"行为，不发 warning——后续若需要逐个识别未知字段，
        // 再扩 reader API。
    }

    // 5) Pass 2：Backend-dependent 组件。
    //    * RigidBody + Collider：必须配对提交给 PhysicsWorld::AddBody 才
    //      能拿到 BodyHandle。先把两边 desc 都读到本地 var，再走 AddBody
    //      并把 handle 反写到 RigidBody.handle，最后 attach 双 component。
    //    * Animator：只持久化 backend name；通过 AnimatorRegistry::Create
    //      取得新 IAnimator 实例。具体 backend 的初始化参数由 game 端在
    //      RegisterBackend 时 capture，scene 不下钻。
    //    各 registry / world 指针为空时分别走 graceful 退化（component
    //    attach 但 backend 字段留空 / nullptr）。
    for (std::size_t i = 0; i < entityCount; ++i)
    {
        const std::string base   = EntityBasePath(i);
        const Entity      entity = idTable[i];

        const std::string rigidPath    = ComponentPath(base, "RigidBody");
        const std::string colliderPath = ComponentPath(base, "Collider");
        const std::string animPath     = ComponentPath(base, "Animator");

        const bool hasRigid    = reader.Has(rigidPath);
        const bool hasCollider = reader.Has(colliderPath);
        const bool hasAnimator = reader.Has(animPath);

        Physics::RigidBodyComponent rigid{};
        Physics::ColliderComponent  collider{};
        bool readRigid    = false;
        bool readCollider = false;

        if (hasRigid)
        {
            if (!ReadRigidBodyDesc(reader, rigidPath, rigid))
            {
                RollbackCreatedEntities(world, created);
                return ResultCode::InvalidArgument;
            }
            readRigid = true;
        }
        if (hasCollider)
        {
            if (!ReadColliderDesc(reader, colliderPath, collider))
            {
                RollbackCreatedEntities(world, created);
                return ResultCode::InvalidArgument;
            }
            readCollider = true;
        }

        if (readRigid && readCollider)
        {
            if (options.physicsWorld != nullptr)
            {
                rigid.handle = options.physicsWorld->AddBody(rigid, collider);
            }
            else
            {
                ORANGE_LOG_WARN(
                    "Scene load: entity has RigidBody+Collider but no PhysicsWorld supplied; "
                    "BodyHandle will be left invalid.");
            }
        }
        else if (readRigid != readCollider)
        {
            // 一个有一个无：当前架构下 AddBody 必须配对，单方 attach 时
            // 不绑定 backend——但 component 仍然 attach（保留 desc）。
            ORANGE_LOG_WARN(
                "Scene load: entity has only one of RigidBody / Collider; "
                "AddBody requires both, leaving body unbound.");
        }

        if (readRigid)
        {
            world.AddComponent(entity, rigid);
        }
        if (readCollider)
        {
            world.AddComponent(entity, collider);
        }

        if (hasAnimator)
        {
            std::string backendName;
            if (!ReadAnimatorBackendName(reader, animPath, backendName))
            {
                RollbackCreatedEntities(world, created);
                return ResultCode::InvalidArgument;
            }

            Animation::AnimatorComponent ac{};
            if (options.animatorRegistry != nullptr)
            {
                ac.animator = options.animatorRegistry->Create(backendName);
                if (ac.animator == nullptr)
                {
                    ORANGE_LOG_WARN(
                        "Scene load: animator backend '{}' not registered with "
                        "AnimatorRegistry; component will hold a null animator.",
                        backendName);
                }
            }
            else
            {
                ORANGE_LOG_WARN(
                    "Scene load: AnimatorComponent present but no AnimatorRegistry "
                    "supplied; component will hold a null animator.");
            }
            world.AddComponent(entity, std::move(ac));
        }

        // extra BackendDependent 组件——不走内置 RigidBody / Animator 配对逻辑，
        // 直接调用各 entry 的 Read 函数指针让实现方自行处理 backend 绑定。
        for (const auto& entry : options.extraSerializers)
        {
            if (entry.kind != ComponentKind::BackendDependent)
            {
                continue;
            }
            if (entry.Read == nullptr)
            {
                continue;
            }
            const std::string componentPath = ComponentPath(base, entry.name);
            if (!reader.Has(componentPath))
            {
                continue;
            }
            if (!entry.Read(reader, componentPath, entity, ctx))
            {
                RollbackCreatedEntities(world, created);
                return ResultCode::InvalidArgument;
            }
        }
    }

    return Result<void, ResultCode>{};
}

}  // namespace Orange::Engine::Scene
