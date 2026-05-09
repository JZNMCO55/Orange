// Scene 序列化主流程。
//
// 顶层 schema 解析 + entity 持久 ID 重映射 + 组件调度，全部在这里编排。
// 单一组件的 Read / Write 在 ComponentSerializers.cpp，通过
// GetBuiltinComponentSerializers() 暴露的表查找。
//
// 头隔离：本 .cpp 不直接 include `<nlohmann/json.hpp>`；走 Core::
// Serialization 公共面。

#include "orange/engine/scene/SceneSerialization.h"

#include "orange/engine/core/Log.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/scene/Entity.h"
#include "orange/engine/scene/World.h"

#include "scene/ComponentSerializers.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
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
    static const SchemaVersion kVersion{"scene/world", 1, 0};
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
                              const Asset::AssetRegistry* assetRegistry)
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

    const SaveContext ctx{world, idMap, assetRegistry};

    // 2) 组装 JSON。
    JsonWriter writer;
    writer.WriteSchemaVersion(kSchemaVersionPath, SceneSchemaVersion());
    writer.BeginArray(kEntitiesPath, entityList.size());

    const auto& serializers = GetBuiltinComponentSerializers();

    for (std::size_t i = 0; i < entityList.size(); ++i)
    {
        const std::string base   = EntityBasePath(i);
        const Entity      entity = entityList[i];

        writer.WriteInt(base + "/id", static_cast<std::int64_t>(i));

        for (const auto& entry : serializers)
        {
            if (entry.Has(world, entity))
            {
                const std::string componentPath = ComponentPath(base, entry.name);
                entry.Write(writer, componentPath, entity, ctx);
            }
        }
    }

    // 3) 落盘。
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
                              Asset::AssetRegistry* assetRegistry)
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

    const LoadContext ctx{world, idTable, assetRegistry};

    // 4) 第一遍：pure-data 组件 attach（当前注册的内置组件全部如此）。
    //    后续追加需要先建 backend 资源（Box2D body / DragonBones armature
    //    等）才能 attach 的组件时，把这里拆成两遍调度——pure-data 一遍、
    //    backend-dependent 一遍——是预留的演进路径，不在当前代码里做。
    const auto& serializers = GetBuiltinComponentSerializers();

    for (std::size_t i = 0; i < entityCount; ++i)
    {
        const std::string base   = EntityBasePath(i);
        const Entity      entity = idTable[i];

        for (const auto& entry : serializers)
        {
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

        // 未识别的 component key（forward-compat 路径）此处理论上要
        // warn + skip。当前 JsonReader 没有"列出对象 key"接口，所以仅
        // 做"跳过"行为，不发 warning——后续若需要逐个识别未知字段，
        // 再扩 reader API。
    }

    return Result<void, ResultCode>{};
}

}  // namespace Orange::Engine::Scene
