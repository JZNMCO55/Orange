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
#include "orange/engine/scene/LayerComponent.h"
#include "orange/engine/scene/World.h"
#include "orange/engine/scene/WorldPartition.h"

#include "scene/ComponentSerializers.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
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
//
// 1.1 → 1.2：新增 LayerComponent（可选 component，旧 1.1 文件读出后该
// 字段缺失视为"归属于 default layer"，与新增任何 optional component 的
// forward-compat 路径一致；不需要 major bump）。
// 1.2 → 1.3：新增 EnvironmentComponent（同款 optional component 路径，
// 旧 1.2 文件无该字段时 Pipeline 退化到 dummy IBL，PBR + IBL baseline）。
// 1.3 → 1.4：新增 AudioSourceComponent（同款 optional component 路径，
// 旧 1.3 文件无该字段时 entity 不参与 audio 播放）。
// 1.4 → 1.5：新增 PointLight（GAP-2026-05-11 G1；同款 optional component
// 路径，旧 1.4 文件无该字段时 Pipeline pointLightCount=0 退化为纯
// DirectionalLight 路径）。
// 1.5 → 1.6：新增 SpotLight（GAP-2026-05-26 G1；同款 optional component
// 路径，旧 1.5 文件无该字段时 Pipeline spotLightCount=0，无锥光贡献）。
// 1.6 → 1.7：新增 PostProcessComponent（屏幕空间 post + PCSS 配置；同款
// optional component 路径，旧 1.6 文件无该字段时 Pipeline 退化到引擎默认
// post；组件内字段全 optional，缺省保留默认值）。
const SchemaVersion& SceneSchemaVersion()
{
    static const SchemaVersion kVersion{"scene/world", 1, 7};
    return kVersion;
}

// manifest 文件独立 schema namespace；与 scene/world 不复用版本号，避免
// "manifest 改 schema 把 scene/world 拖一起 bump"。
const SchemaVersion& SceneManifestSchemaVersion()
{
    static const SchemaVersion kVersion{"scene/manifest", 1, 0};
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

// SaveImpl 是 Save / SaveSplit 共用的核心。entityFilter 为空时把所有活
// 实体写进 JSON；非空时只写过滤通过的子集——SaveSplit 按 layerId 过滤
// 时用这条路径。
//
// 注意：持久 ID 按"过滤后剩下的 entity 在 view 内的相对顺序"重新分配，
// 不与"完整 world 内的全局位置"耦合——这样每个 per-layer 文件内部都从
// 0 开始编号，文件可读性 + diff 友好性最大。HierarchyComponent 跨 layer
// 引用因此**不会被正确序列化**——是已知设计选择：跨 layer 的 hierarchy
// 关系本来就违反 "per-layer 独立编辑" 的工程意图；遇到时 layer 编辑器
// 应在 attach-time 拒绝建立跨 layer parent-child。
Result<void, ResultCode> SaveImpl(const World& world,
                                  std::string_view path,
                                  const SaveOptions& options,
                                  const std::function<bool(Entity)>& entityFilter)
{
    // 1) 收集所有 live entity（可选过滤），按 entity index 升序分配 0..N-1
    //    持久 ID。
    //
    //    为什么要排序：EnTT view<entt::entity>() 在 packed array 上是 LIFO
    //    方向迭代（后创建的先返回），所以 Save 直接按 view 写出的 entity 数
    //    组顺序在 Save → Load → Save 往返中会反转：Load 按 JSON 顺序逐个
    //    CreateEntity，新 World 内 EnTT ID 按 JSON 顺序单调递增，view 再
    //    LIFO 给出的就是原始顺序的反转，二次 Save 写盘后整段 entity 数组
    //    完整翻转，制造无业务变动的污染 diff。
    //
    //    按 entity index 升序排序后，写盘顺序等价于"按创建顺序"——Source
    //    与 Loaded World 在 Save 时都会输出相同字节序列，跨机器 / 跨 session
    //    .scene.json 字节稳定，编辑器 Play → Stop 也不再翻转 Entity Tree
    //    显示顺序（Entity Tree 仍消费 view，但反转再反转回到原序）。
    std::vector<Entity>          entityList;
    EntityToPersistentId         idMap;
    {
        auto& reg = world.Registry();
        // 先估个容量再 push——Size() 是 World 自己维护的活实体数。
        entityList.reserve(world.Size());

        for (auto e : reg.view<entt::entity>())
        {
            const Entity entity = World::FromEntt(e);
            if (entityFilter && !entityFilter(entity))
            {
                continue;
            }
            entityList.push_back(entity);
        }

        std::sort(entityList.begin(), entityList.end(),
                  [](Entity a, Entity b)
                  {
                      // 剥掉 version bits，只比较 index 部分；EnTT 在 entity
                      // destroy + recycle 时 version 会递增，但 index 仍能
                      // 反映 EnTT 内部 storage 位置，足以作为稳定 key。
                      using Traits = entt::entt_traits<entt::entity>;
                      constexpr auto kEntityMask = Traits::entity_mask;
                      const auto ai = static_cast<std::uint32_t>(
                          entt::to_integral(World::ToEntt(a))) & kEntityMask;
                      const auto bi = static_cast<std::uint32_t>(
                          entt::to_integral(World::ToEntt(b))) & kEntityMask;
                      return ai < bi;
                  });

        idMap.reserve(entityList.size());
        for (std::size_t i = 0; i < entityList.size(); ++i)
        {
            idMap.emplace(entityList[i], static_cast<std::int64_t>(i));
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

}  // namespace

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

Result<void, ResultCode> Save(const World& world,
                              std::string_view path,
                              const SaveOptions& options)
{
    // 直接走 SaveImpl 不带 filter——单文件路径写出整 world。
    return SaveImpl(world, path, options, {});
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
                          options.namedMaterialInstances,
                          options.materialResolver};

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

    // 6) assignLayerId 兜底：LoadSplit 路径把每个 per-layer 文件的 entity
    //    自动归属到对应 layer。本次新建且 component map 里没明确写出
    //    LayerComponent 的 entity，统一挂上 assignLayerId。
    //    单文件 Load 默认 options.assignLayerId 为空，跳过此 pass。
    if (!options.assignLayerId.empty())
    {
        for (Entity e : created)
        {
            if (!world.HasComponent<LayerComponent>(e))
            {
                LayerComponent lc;
                lc.layerId = options.assignLayerId;
                world.AddComponent(e, std::move(lc));
            }
        }
    }

    return Result<void, ResultCode>{};
}

// ---------------------------------------------------------------------------
// SaveSplit / LoadSplit —— 多文件 + manifest 序列化。
// ---------------------------------------------------------------------------

namespace
{

constexpr std::string_view kManifestSchemaVersionPath = "schemaVersion";
constexpr std::string_view kManifestLayersPath        = "layers";

std::string LayerArrayPath(std::size_t index)
{
    std::string p;
    p.reserve(kManifestLayersPath.size() + 1 + 12);
    p.append(kManifestLayersPath);
    p.push_back('/');
    p.append(std::to_string(index));
    return p;
}

// 把 source 字段（manifest 内的相对路径）解析为绝对 / 工作目录可用的
// 完整路径。base 是 manifest 文件所在目录；source 为空时直接返回空 path
// （SaveSplit / LoadSplit 调用前会先校验 source 非空，本函数只做拼接）。
std::filesystem::path ResolveSource(const std::filesystem::path& base,
                                    std::string_view source)
{
    std::filesystem::path src{source};
    if (src.is_absolute())
    {
        return src;
    }
    return base / src;
}

}  // namespace

Result<void, ResultCode> SaveSplit(const World& world,
                                   const WorldPartition& partition,
                                   std::string_view manifestPath,
                                   const SaveOptions& options)
{
    const auto& layers = partition.GetLayers();
    if (layers.empty())
    {
        // 至少要有 default layer——WorldPartition 构造时自动补；空状态
        // 说明调用方拿到一个被 ResetLayers({}) 清空后又没补 default 的
        // partition，视为编程错误而不是数据错误。
        ORANGE_LOG_ERROR("Scene SaveSplit: WorldPartition has no layers; expected at least 'default'.");
        return ResultCode::InvalidArgument;
    }

    // 每条 layer 必须配 source 文件路径——manifest 没法引用空 source。
    for (const auto& layer : layers)
    {
        if (layer.source.empty())
        {
            ORANGE_LOG_ERROR(
                "Scene SaveSplit: layer '{}' has no source filename; "
                "set LayerInfo.source before calling SaveSplit.",
                layer.id);
            return ResultCode::InvalidArgument;
        }
    }

    const std::filesystem::path manifestFsPath{std::string{manifestPath}};
    const std::filesystem::path baseDir = manifestFsPath.parent_path();

    // 先写出每个 layer 的 .scene.json——按 partition 排列序，文件顺序
    // 与 manifest 内 layers 数组一致。
    for (const auto& layer : layers)
    {
        const std::filesystem::path layerPath = ResolveSource(baseDir, layer.source);
        // SaveImpl 内部覆盖写；中间失败已写盘的层不会自动回收（与单文件
        // Save 同款约束——调用方应在临时目录写完再 rename / commit）。
        const std::string capturedId = layer.id;
        auto filter = [&world, capturedId](Entity e) -> bool
        {
            // GetLayerOf 返回 default 给"无 LayerComponent"的 entity；
            // 与 partition 内 default layer 的 id 比较即可正确归属。
            const auto* lc = world.GetComponent<LayerComponent>(e);
            const std::string_view layerId =
                (lc != nullptr && !lc->layerId.empty())
                    ? std::string_view{lc->layerId}
                    : WorldPartition::DefaultLayerId();
            return layerId == capturedId;
        };

        auto layerResult = SaveImpl(world, layerPath.string(), options, filter);
        if (layerResult.IsErr())
        {
            ORANGE_LOG_ERROR(
                "Scene SaveSplit: failed to write layer '{}' to '{}' (code={}); aborting.",
                layer.id, layerPath.string(),
                static_cast<int>(layerResult.Error()));
            return layerResult.Error();
        }
    }

    // 写 manifest：schemaVersion + layers 数组（id / displayName / visible / source）。
    JsonWriter manifestWriter;
    manifestWriter.WriteSchemaVersion(kManifestSchemaVersionPath, SceneManifestSchemaVersion());
    manifestWriter.BeginArray(kManifestLayersPath, layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i)
    {
        const auto& layer = layers[i];
        const std::string base = LayerArrayPath(i);
        manifestWriter.WriteString(base + "/id",          layer.id);
        manifestWriter.WriteString(base + "/displayName", layer.displayName);
        manifestWriter.WriteBool  (base + "/visible",     layer.visible);
        manifestWriter.WriteString(base + "/source",      layer.source);
    }

    auto manifestSave = manifestWriter.SaveToFile(manifestPath);
    if (manifestSave.IsErr())
    {
        return manifestSave.Error();
    }
    return Result<void, ResultCode>{};
}

Result<void, ResultCode> LoadSplit(std::string_view manifestPath,
                                   World& world,
                                   WorldPartition& partition,
                                   const LoadOptions& options)
{
    // 1) 解析 manifest 文件。
    auto manifestReaderResult = JsonReader::FromFile(manifestPath);
    if (manifestReaderResult.IsErr())
    {
        return manifestReaderResult.Error().code;
    }
    const JsonReader& manifestReader = manifestReaderResult.Value();

    auto schemaResult = manifestReader.ReadSchemaVersion(kManifestSchemaVersionPath);
    if (schemaResult.IsErr())
    {
        return ResultCode::SchemaMismatch;
    }
    if (!SceneManifestSchemaVersion().CanRead(schemaResult.Value()))
    {
        return ResultCode::SchemaMismatch;
    }

    const std::size_t layerCount = manifestReader.ArraySize(kManifestLayersPath);
    std::vector<LayerInfo> manifestLayers;
    manifestLayers.reserve(layerCount);
    for (std::size_t i = 0; i < layerCount; ++i)
    {
        const std::string base = LayerArrayPath(i);
        LayerInfo li;
        if (!manifestReader.ReadString(base + "/id", li.id))
        {
            return ResultCode::InvalidArgument;
        }
        // displayName / visible / source 缺失时用宽容默认值，便于未来 minor
        // bump 不破坏读路径。
        li.displayName = manifestReader.GetString(base + "/displayName", li.id);
        li.visible     = manifestReader.GetBool  (base + "/visible",     true);
        li.source      = manifestReader.GetString(base + "/source",      std::string{});
        manifestLayers.push_back(std::move(li));
    }

    // 2) 灌入 manifest——ResetLayers 内部会自动补 default layer 兜底。
    partition.ResetLayers(std::move(manifestLayers));

    // 3) 对每个 layer 逐个 Load 对应 source 文件。
    //    每条 source 内的 entity 都强制归属到本 layer——已存在 LayerComponent
    //    的 entity 优先于 assignLayerId（手工编辑允许某 entity 跨 layer 引用
    //    时不被覆盖）。
    const std::filesystem::path manifestFsPath{std::string{manifestPath}};
    const std::filesystem::path baseDir = manifestFsPath.parent_path();

    for (const auto& layer : partition.GetLayers())
    {
        if (layer.source.empty())
        {
            // default layer 自动补的条目通常没 source——跳过即可，对应 layer
            // 上的 entity 由调用方 SeedDemoWorld 之类的代码自己挂。
            continue;
        }

        LoadOptions perLayerOptions{
            .assetRegistry          = options.assetRegistry,
            .physicsWorld           = options.physicsWorld,
            .animatorRegistry       = options.animatorRegistry,
            .namedMaterialInstances = options.namedMaterialInstances,
            .materialResolver       = options.materialResolver,
            .extraSerializers       = options.extraSerializers,
            .assignLayerId          = layer.id,
        };

        const std::filesystem::path layerPath = ResolveSource(baseDir, layer.source);
        auto layerResult = Load(layerPath.string(), world, perLayerOptions);
        if (layerResult.IsErr())
        {
            ORANGE_LOG_ERROR(
                "Scene LoadSplit: failed to load layer '{}' from '{}' (code={}); "
                "World may be in partial state.",
                layer.id, layerPath.string(),
                static_cast<int>(layerResult.Error()));
            return layerResult.Error();
        }
    }

    return Result<void, ResultCode>{};
}

}  // namespace Orange::Engine::Scene
