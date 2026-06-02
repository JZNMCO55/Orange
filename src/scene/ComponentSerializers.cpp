// 内置组件的序列化实现。
//
// 一条规约：所有 component 在 JSON 里都写成"对象"，即使当前只有一个字段。
// 这是为了让"未来加可选字段"成为 minor schema bump，而不需要重写读路径。
// 比如 Name 当前只有 `name` 一个字段，仍然落到 `{"name": "..."}` 而非裸字
// 符串——后续要加 tag / category 时不破老存档。
//
// 头隔离：本文件**不**直接 include `<nlohmann/json.hpp>`；走 Core::
// Serialization 的 JsonReader / JsonWriter 公共接口，与 CLAUDE.md 的
// "Serialization and reflection" 一节保持一致。

#include "scene/ComponentSerializers.h"

#include "orange/engine/animation/AnimationClipSerialization.h"
#include "orange/engine/animation/AnimatorComponent.h"
#include "orange/engine/animation/ClipAnimator.h"
#include "orange/engine/animation/IAnimator.h"
#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/MeshAsset.h"
#include "orange/engine/asset/SoundAsset.h"
#include "orange/engine/audio/AudioSourceComponent.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/physics/ColliderComponent.h"
#include "orange/engine/physics/RigidBodyComponent.h"
#include "orange/engine/asset/TextureAsset.h"
#include "orange/engine/render/Camera.h"
#include "orange/engine/render/EnvironmentComponent.h"
#include "orange/engine/render/PostProcessComponent.h"
#include "orange/engine/render/LightComponent.h"
#include "orange/engine/render/ParticleEmitterComponent.h"
#include "orange/engine/render/RenderableComponent.h"
#include "orange/engine/render/SubMeshMaterialsComponent.h"
#include "orange/engine/scene/GuidComponent.h"
#include "orange/engine/scene/HierarchyComponent.h"
#include "orange/engine/scene/LayerComponent.h"
#include "orange/engine/scene/NameComponent.h"
#include "orange/engine/scene/PrefabInstanceComponent.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Orange::Engine::Scene
{
namespace
{

// ---------------------------------------------------------------------------
// 路径拼接小工具：调用频次很高，避免每个站点重复 std::string + "/" + 后缀
// 这种写法。
std::string Join(std::string_view base, std::string_view leaf)
{
    std::string out;
    out.reserve(base.size() + 1 + leaf.size());
    out.append(base);
    out.push_back('/');
    out.append(leaf);
    return out;
}

// ---------------------------------------------------------------------------
// Entity reference 的写 / 读：用持久 ID（int64_t）落 JSON，-1 表示 invalid。
// 这样 Hierarchy 字段在文件里始终是稳定可读的整数，不依赖 EnTT 内部位模式。
constexpr std::int64_t kInvalidPersistentId = -1;

std::int64_t PersistentIdOf(Entity entity, const EntityToPersistentId& map)
{
    if (!entity.IsValid())
    {
        return kInvalidPersistentId;
    }
    auto it = map.find(entity);
    return (it != map.end()) ? it->second : kInvalidPersistentId;
}

Entity EntityForPersistentId(std::int64_t id, const PersistentIdToEntity& table)
{
    if (id < 0 || static_cast<std::size_t>(id) >= table.size())
    {
        return Entity::Invalid();
    }
    return table[static_cast<std::size_t>(id)];
}

// ---------------------------------------------------------------------------
// TransformComponent
// ---------------------------------------------------------------------------

bool HasTransform(const World& world, Entity entity)
{
    return world.HasComponent<TransformComponent>(entity);
}

void WriteTransform(JsonWriter& writer,
                    std::string_view componentPath,
                    Entity entity,
                    const SaveContext& ctx)
{
    const auto* tx = ctx.world.GetComponent<TransformComponent>(entity);
    if (tx == nullptr)
    {
        return;
    }

    const float position[3] = {tx->position.x, tx->position.y, tx->position.z};
    // glm::quat 内存布局是 (w, x, y, z)，但写到 JSON 时按业内更常见的
    // (x, y, z, w) 落字符串——读时再翻回来。坐标轴语义不变，只是字段顺序。
    const float rotation[4] = {tx->rotation.x, tx->rotation.y, tx->rotation.z, tx->rotation.w};
    const float scale[3] = {tx->scale.x, tx->scale.y, tx->scale.z};

    writer.WriteFloatArray(Join(componentPath, "position"), position, 3);
    writer.WriteFloatArray(Join(componentPath, "rotation"), rotation, 4);
    writer.WriteFloatArray(Join(componentPath, "scale"), scale, 3);
}

bool ReadTransform(const JsonReader& reader,
                   std::string_view componentPath,
                   Entity entity,
                   const LoadContext& ctx)
{
    TransformComponent tx;

    float position[3] = {0.0f, 0.0f, 0.0f};
    if (!reader.ReadFloatArray(Join(componentPath, "position"), position, 3))
    {
        return false;
    }
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!reader.ReadFloatArray(Join(componentPath, "rotation"), rotation, 4))
    {
        return false;
    }
    float scale[3] = {1.0f, 1.0f, 1.0f};
    if (!reader.ReadFloatArray(Join(componentPath, "scale"), scale, 3))
    {
        return false;
    }

    tx.position = {position[0], position[1], position[2]};
    // glm::quat 构造序是 (w, x, y, z)，与文件里的 (x, y, z, w) 颠倒。
    tx.rotation = glm::quat(rotation[3], rotation[0], rotation[1], rotation[2]);
    tx.scale    = {scale[0], scale[1], scale[2]};

    ctx.world.AddComponent(entity, tx);
    return true;
}

// ---------------------------------------------------------------------------
// HierarchyComponent
// ---------------------------------------------------------------------------

bool HasHierarchy(const World& world, Entity entity)
{
    return world.HasComponent<HierarchyComponent>(entity);
}

void WriteHierarchy(JsonWriter& writer,
                    std::string_view componentPath,
                    Entity entity,
                    const SaveContext& ctx)
{
    const auto* h = ctx.world.GetComponent<HierarchyComponent>(entity);
    if (h == nullptr)
    {
        return;
    }

    writer.WriteInt(Join(componentPath, "parent"),      PersistentIdOf(h->parent,      ctx.entityToId));
    writer.WriteInt(Join(componentPath, "firstChild"),  PersistentIdOf(h->firstChild,  ctx.entityToId));
    writer.WriteInt(Join(componentPath, "nextSibling"), PersistentIdOf(h->nextSibling, ctx.entityToId));
    writer.WriteInt(Join(componentPath, "prevSibling"), PersistentIdOf(h->prevSibling, ctx.entityToId));
    // 根序（ADR-014）——仅根节点有意义；非根写出来读回也被忽略，无害。
    writer.WriteInt(Join(componentPath, "sortIndex"),   h->sortIndex);
}

bool ReadHierarchy(const JsonReader& reader,
                   std::string_view componentPath,
                   Entity entity,
                   const LoadContext& ctx)
{
    HierarchyComponent h;

    std::int64_t parent      = kInvalidPersistentId;
    std::int64_t firstChild  = kInvalidPersistentId;
    std::int64_t nextSibling = kInvalidPersistentId;
    std::int64_t prevSibling = kInvalidPersistentId;
    std::int64_t sortIndex   = 0;  // 根序（ADR-014）；缺字段默认 0 = id 序退化

    // 缺字段视为 -1（无引用）；类型不匹配则抛 false。这种宽容度让"只
    // 想表达 parent 关系的旧版 scene"在 schema 升级后仍可被读出来。
    if (reader.Has(Join(componentPath, "parent")))
    {
        if (!reader.ReadInt(Join(componentPath, "parent"), parent))
        {
            return false;
        }
    }
    if (reader.Has(Join(componentPath, "firstChild")))
    {
        if (!reader.ReadInt(Join(componentPath, "firstChild"), firstChild))
        {
            return false;
        }
    }
    if (reader.Has(Join(componentPath, "nextSibling")))
    {
        if (!reader.ReadInt(Join(componentPath, "nextSibling"), nextSibling))
        {
            return false;
        }
    }
    if (reader.Has(Join(componentPath, "prevSibling")))
    {
        if (!reader.ReadInt(Join(componentPath, "prevSibling"), prevSibling))
        {
            return false;
        }
    }
    if (reader.Has(Join(componentPath, "sortIndex")))
    {
        if (!reader.ReadInt(Join(componentPath, "sortIndex"), sortIndex))
        {
            return false;
        }
    }

    h.parent      = EntityForPersistentId(parent,      ctx.idToEntity);
    h.firstChild  = EntityForPersistentId(firstChild,  ctx.idToEntity);
    h.nextSibling = EntityForPersistentId(nextSibling, ctx.idToEntity);
    h.prevSibling = EntityForPersistentId(prevSibling, ctx.idToEntity);
    h.sortIndex   = static_cast<int>(sortIndex);

    ctx.world.AddComponent(entity, h);
    return true;
}

// ---------------------------------------------------------------------------
// NameComponent
// ---------------------------------------------------------------------------

bool HasName(const World& world, Entity entity)
{
    return world.HasComponent<NameComponent>(entity);
}

void WriteName(JsonWriter& writer,
               std::string_view componentPath,
               Entity entity,
               const SaveContext& ctx)
{
    const auto* n = ctx.world.GetComponent<NameComponent>(entity);
    if (n == nullptr)
    {
        return;
    }
    // 始终走对象形态 `{"name": "..."}`，便于将来扩 tag / category 字段
    // 时不破 schema。
    writer.WriteString(Join(componentPath, "name"), n->name);
}

bool ReadName(const JsonReader& reader,
              std::string_view componentPath,
              Entity entity,
              const LoadContext& ctx)
{
    NameComponent n;
    // name 是必填——空字符串显式存为 ""（语义"未命名"），缺字段视为格式坏。
    if (!reader.ReadString(Join(componentPath, "name"), n.name))
    {
        return false;
    }
    ctx.world.AddComponent(entity, n);
    return true;
}

// ---------------------------------------------------------------------------
// GuidComponent
//
// 稳定实体身份（ADR-013）。序列化为单个 32-hex 字符串字段 "value"，走对象
// 形态便于将来扩字段。FromString 失败（坏格式）视为数据坏 → Read 返回 false。
// ---------------------------------------------------------------------------

bool HasGuid(const World& world, Entity entity)
{
    return world.HasComponent<GuidComponent>(entity);
}

void WriteGuid(JsonWriter& writer,
               std::string_view componentPath,
               Entity entity,
               const SaveContext& ctx)
{
    const auto* g = ctx.world.GetComponent<GuidComponent>(entity);
    if (g == nullptr)
    {
        return;
    }
    writer.WriteString(Join(componentPath, "value"), g->guid.ToString());
}

bool ReadGuid(const JsonReader& reader,
              std::string_view componentPath,
              Entity entity,
              const LoadContext& ctx)
{
    std::string text;
    if (!reader.ReadString(Join(componentPath, "value"), text))
    {
        return false;
    }
    GuidComponent g;
    if (!Core::Guid::FromString(text, g.guid))
    {
        return false;
    }
    ctx.world.AddComponent(entity, g);
    return true;
}

// ---------------------------------------------------------------------------
// PrefabInstanceComponent
//
// prefab 实例链接组件（实例化产物绑回源 prefab 资源）。序列化为对象形态，
// 三个字段：
//   * sourcePrefabPath —— 字符串，源 prefab 资源路径（跨会话稳定 key）。
//   * instanceId       —— 32-hex 字符串，标识"哪一次实例化"。坏格式 → Read
//     返回 false（数据坏，整体回滚 Load），与 Guid 同款严格。
//   * isInstanceRoot   —— bool，仅实例根 true。
// 三字段全为必填——本组件只由 InstantiatePrefab 程序化挂载，落盘时必然写齐，
// 不需要 optional 兜底。
// ---------------------------------------------------------------------------

bool HasPrefabInstance(const World& world, Entity entity)
{
    return world.HasComponent<PrefabInstanceComponent>(entity);
}

void WritePrefabInstance(JsonWriter& writer,
                         std::string_view componentPath,
                         Entity entity,
                         const SaveContext& ctx)
{
    const auto* p = ctx.world.GetComponent<PrefabInstanceComponent>(entity);
    if (p == nullptr)
    {
        return;
    }
    writer.WriteString(Join(componentPath, "sourcePrefabPath"), p->sourcePrefabPath);
    writer.WriteString(Join(componentPath, "instanceId"), p->instanceId.ToString());
    writer.WriteBool(Join(componentPath, "isInstanceRoot"), p->isInstanceRoot);
}

bool ReadPrefabInstance(const JsonReader& reader,
                        std::string_view componentPath,
                        Entity entity,
                        const LoadContext& ctx)
{
    PrefabInstanceComponent p;
    if (!reader.ReadString(Join(componentPath, "sourcePrefabPath"), p.sourcePrefabPath))
    {
        return false;
    }
    std::string idText;
    if (!reader.ReadString(Join(componentPath, "instanceId"), idText))
    {
        return false;
    }
    if (!Core::Guid::FromString(idText, p.instanceId))
    {
        return false;
    }
    if (!reader.ReadBool(Join(componentPath, "isInstanceRoot"), p.isInstanceRoot))
    {
        return false;
    }
    ctx.world.AddComponent(entity, std::move(p));
    return true;
}

// ---------------------------------------------------------------------------
// LayerComponent
//
// 序列化策略：仅一个 string 字段，但走对象形态 `{"id": "..."}`，给后续
// 加 lockInEditor / color 等 layer-side meta 留 schema bump 空间。空字符
// 串显式落盘——读时 WorldPartition::GetLayerOf 把空 id 解释为 default。
// ---------------------------------------------------------------------------

bool HasLayer(const World& world, Entity entity)
{
    return world.HasComponent<LayerComponent>(entity);
}

void WriteLayer(JsonWriter& writer,
                std::string_view componentPath,
                Entity entity,
                const SaveContext& ctx)
{
    const auto* l = ctx.world.GetComponent<LayerComponent>(entity);
    if (l == nullptr)
    {
        return;
    }
    writer.WriteString(Join(componentPath, "id"), l->layerId);
}

bool ReadLayer(const JsonReader& reader,
               std::string_view componentPath,
               Entity entity,
               const LoadContext& ctx)
{
    LayerComponent l;
    if (!reader.ReadString(Join(componentPath, "id"), l.layerId))
    {
        return false;
    }
    ctx.world.AddComponent(entity, std::move(l));
    return true;
}

// ---------------------------------------------------------------------------
// RenderableComponent
//
// 序列化策略：mesh 走"路径化"——AssetRegistry 反查 PathOf 写出文件相对
// 路径；读时通过 Load<MeshAsset>(path) 重新拿 handle。
//
// materialInstance 是游戏侧持有的 raw 指针（不是 AssetRegistry 资源），
// 不参与序列化——读回来的 entity 必须由调用方 / sample / 编辑器在 Load
// 之后重新挂 instance。这与 RenderableComponent.h 的注释保持一致：
// "MaterialInstance 由 sample / 游戏代码侧持有"。
//
// 没有 AssetRegistry 时（`ctx.assetRegistry == nullptr`）走 graceful
// 退化：写出空 mesh path、读时把 handle 留空。整个 scene 仍能 round-trip，
// 只是少了 mesh 解析的一步——典型用于"只想做 entity / hierarchy 编辑"
// 的轻量工具。
// ---------------------------------------------------------------------------

bool HasRenderable(const World& world, Entity entity)
{
    return world.HasComponent<Render::RenderableComponent>(entity);
}

void WriteRenderable(JsonWriter& writer,
                     std::string_view componentPath,
                     Entity entity,
                     const SaveContext& ctx)
{
    const auto* r = ctx.world.GetComponent<Render::RenderableComponent>(entity);
    if (r == nullptr)
    {
        return;
    }

    std::string_view meshPath;
    if (ctx.assetRegistry != nullptr && r->mesh.IsValid())
    {
        meshPath = ctx.assetRegistry->PathOf<Asset::MeshAsset>(r->mesh);
        if (meshPath.empty())
        {
            // handle 在但 registry 反查不到 path——通常说明该 mesh 是
            // 通过 Insert() 临时挂的（无源文件）。落空字符串，并 warn
            // 一声让调用方有线索调查；不阻断保存。
            ORANGE_LOG_WARN(
                "Scene save: RenderableComponent's mesh handle has no path in AssetRegistry; "
                "writing empty path.");
        }
    }
    else if (r->mesh.IsValid() && ctx.assetRegistry == nullptr)
    {
        ORANGE_LOG_WARN(
            "Scene save: RenderableComponent has a valid mesh handle but no AssetRegistry "
            "was supplied to Save(); writing empty path.");
    }

    // materialInstance 按 id 字符串持久化。调用方通过 SaveContext::
    // namedMaterialInstances 提供 name→pointer 表；此处 O(N) 反查。
    std::string_view materialId;
    if (r->materialInstance != nullptr)
    {
        if (ctx.namedMaterialInstances != nullptr)
        {
            for (const auto& [name, ptr] : *ctx.namedMaterialInstances)
            {
                if (ptr == r->materialInstance)
                {
                    materialId = name;
                    break;
                }
            }
            if (materialId.empty())
            {
                ORANGE_LOG_WARN(
                    "Scene save: RenderableComponent.materialInstance not found in "
                    "namedMaterialInstances; writing empty materialInstanceId.");
            }
        }
        else
        {
            ORANGE_LOG_WARN(
                "Scene save: RenderableComponent has a materialInstance but no "
                "namedMaterialInstances was supplied to Save(); writing empty id.");
        }
    }

    writer.WriteString(Join(componentPath, "mesh"),               meshPath);
    writer.WriteString(Join(componentPath, "materialInstanceId"), materialId);
    writer.WriteBool(  Join(componentPath, "visible"),            r->visible);
    writer.WriteBool(  Join(componentPath, "castsShadow"),        r->castsShadow);
}

bool ReadRenderable(const JsonReader& reader,
                    std::string_view componentPath,
                    Entity entity,
                    const LoadContext& ctx)
{
    std::string meshPath;
    // mesh 字段必填（即使是空字符串也得有），缺失视为格式坏。
    if (!reader.ReadString(Join(componentPath, "mesh"), meshPath))
    {
        return false;
    }

    // GAP-2026-05-16 G4：旧版 demo / save_load_demo 等 .scene.json 内
    // RenderableComponent.mesh 用过命名 ID "editor/cube" / "editor/plane"
    // 风格（G1 之前内置 mesh 是内存 named handle）。G1 落地后内置 mesh
    // 改为磁盘 "assets/meshes/X.mesh"，老 .scene.json 字段需要透明 mapping
    // 让旧文件仍可加载。已知映射表见下；命中即重写到磁盘路径，未命中
    // 按原值走 AssetRegistry::Load（caller 自己保证路径正确）。
    if (meshPath == "editor/cube")  { meshPath = "assets/meshes/cube.mesh"; }
    else if (meshPath == "editor/plane") { meshPath = "assets/meshes/plane.mesh"; }

    Render::RenderableComponent r;
    if (!meshPath.empty())
    {
        if (ctx.assetRegistry != nullptr)
        {
            auto loadResult = ctx.assetRegistry->Load<Asset::MeshAsset>(meshPath);
            if (loadResult.IsOk())
            {
                r.mesh = loadResult.Value();
            }
            else
            {
                // mesh 资源加载失败 → 留空 handle，warn 后继续。让编辑器
                // 能开"资源缺失但其他完好"的 scene 修复，而不是整盘拒绝。
                ORANGE_LOG_WARN("Scene load: failed to load mesh '{}'; leaving handle empty.",
                                meshPath);
            }
        }
        else
        {
            ORANGE_LOG_WARN(
                "Scene load: RenderableComponent references mesh '{}' but no AssetRegistry "
                "was supplied to Load(); leaving handle empty.",
                meshPath);
        }
    }

    // visible / castsShadow 走"缺字段→默认值"语义，便于 minor schema 升级。
    r.visible     = reader.GetBool(Join(componentPath, "visible"),     true);
    r.castsShadow = reader.GetBool(Join(componentPath, "castsShadow"), true);

    // materialInstanceId 是 schema v1.1 新增的可选字段；旧 v1.0 文件缺失时
    // GetString 返回空字符串，materialInstance 留 nullptr（与旧行为一致）。
    std::string materialId =
        reader.GetString(Join(componentPath, "materialInstanceId"), "");
    // GAP-2026-05-16 G4：与 mesh path mapping 对偶——namedMaterialInstances
    // key 从 "builtin/X" 迁移到 "assets/materials/builtin/X.material" 后，
    // 老 .scene.json 内 materialInstanceId 字段值也需要透明 mapping。
    // mapping 表与 BuildNamedMaterialInstances 内 key 列表保持一致。
    if (!materialId.empty() && materialId.rfind("builtin/", 0) == 0)
    {
        // 形如 "builtin/toon" → "assets/materials/builtin/toon.material"
        std::string remapped = "assets/materials/";
        remapped.append(materialId);
        remapped.append(".material");
        materialId = std::move(remapped);
    }
    if (!materialId.empty())
    {
        Render::MaterialInstance* resolved = nullptr;

        // 1) 先查静态表（内置 / showcase + 本 session 已 lazy 过的 user material）。
        //    命中即用，是最快路径，也是 pbr_showcase 等纯内置场景的常态。
        if (ctx.namedMaterialInstances != nullptr)
        {
            auto it = ctx.namedMaterialInstances->find(materialId);
            if (it != ctx.namedMaterialInstances->end())
            {
                resolved = it->second;
            }
        }

        // 2) 表里没有 → 用 resolver 从磁盘 lazy-create 兜底。修复"DCC 导入的
        //    .material 在新 session 重新打开场景时查表失败 → Inspector 显示
        //    None"：namedMaterialInstances 启动期是 one-shot snapshot，不含
        //    assets/<Type>/*.material 这类导入产物，而 mesh 走 AssetRegistry
        //    磁盘加载不受影响——本 resolver 让 material 解析与 mesh 对称。
        //    编辑器把它接到 EnsureMaterialInstance（见 LoadContext 字段注释）。
        if (resolved == nullptr && ctx.materialResolver)
        {
            resolved = ctx.materialResolver(materialId);
        }

        if (resolved != nullptr)
        {
            r.materialInstance = resolved;
        }
        else
        {
            ORANGE_LOG_WARN(
                "Scene load: materialInstanceId '{}' could not be resolved "
                "(absent from namedMaterialInstances and no resolver produced "
                "it); leaving materialInstance null.",
                materialId);
        }
    }

    ctx.world.AddComponent(entity, r);
    return true;
}

// ---------------------------------------------------------------------------
// SubMeshMaterialsComponent —— 与 Renderable 配对的可选组件（单 mesh 多
// material）。slots 里每个 MaterialInstance* 用与 Renderable.materialInstance
// 完全相同的 by-id 机制持久化：写时把指针反查成 namedMaterialInstances 里
// 的 name 字符串、落成字符串数组；读时把数组里每个 id 解析回 MaterialInstance*
// （先查 namedMaterialInstances，未命中走 materialResolver 从磁盘 lazy-create）。
// 空 slot（nullptr）落空字符串、读回 nullptr——保持"该 slot 回退默认材质"语义。
// ---------------------------------------------------------------------------

namespace
{

// MaterialInstance* → id 字符串：复刻 WriteRenderable 里的 O(N) 反查。
// 命中返回 name；nullptr 或查不到返回空字符串。
std::string MaterialInstanceToId(const Render::MaterialInstance* instance,
                                 const SaveContext& ctx)
{
    if (instance == nullptr || ctx.namedMaterialInstances == nullptr)
    {
        return std::string{};
    }
    for (const auto& [name, ptr] : *ctx.namedMaterialInstances)
    {
        if (ptr == instance)
        {
            return name;
        }
    }
    return std::string{};
}

// id 字符串 → MaterialInstance*：复刻 ReadRenderable 的解析路径（builtin/
// 前缀 remap + namedMaterialInstances 查表 + materialResolver lazy 兜底）。
// 空字符串直接返回 nullptr（对应空 slot）。
Render::MaterialInstance* MaterialIdToInstance(const std::string& rawId,
                                               const LoadContext& ctx)
{
    if (rawId.empty())
    {
        return nullptr;
    }
    std::string materialId = rawId;
    if (materialId.rfind("builtin/", 0) == 0)
    {
        std::string remapped = "assets/materials/";
        remapped.append(materialId);
        remapped.append(".material");
        materialId = std::move(remapped);
    }

    Render::MaterialInstance* resolved = nullptr;
    if (ctx.namedMaterialInstances != nullptr)
    {
        auto it = ctx.namedMaterialInstances->find(materialId);
        if (it != ctx.namedMaterialInstances->end())
        {
            resolved = it->second;
        }
    }
    if (resolved == nullptr && ctx.materialResolver)
    {
        resolved = ctx.materialResolver(materialId);
    }
    return resolved;
}

}  // namespace

bool HasSubMeshMaterials(const World& world, Entity entity)
{
    return world.HasComponent<Render::SubMeshMaterialsComponent>(entity);
}

void WriteSubMeshMaterials(JsonWriter& writer,
                           std::string_view componentPath,
                           Entity entity,
                           const SaveContext& ctx)
{
    const auto* c = ctx.world.GetComponent<Render::SubMeshMaterialsComponent>(entity);
    if (c == nullptr)
    {
        return;
    }

    // 每个 slot 反查成 id 字符串，落成字符串数组（顺序即 slot index）。
    // string 数组无专用 helper：BeginArray 声明长度 + 索引 WriteString
    // （与 ParticleEmitter polygon 顶点的数组写法同款）。
    const std::string slotsPath = Join(componentPath, "slots");
    writer.BeginArray(slotsPath, c->slots.size());
    for (std::size_t i = 0; i < c->slots.size(); ++i)
    {
        const Render::MaterialInstance* instance = c->slots[i];
        std::string id = MaterialInstanceToId(instance, ctx);
        if (instance != nullptr && id.empty())
        {
            ORANGE_LOG_WARN(
                "Scene save: SubMeshMaterialsComponent slot material not found in "
                "namedMaterialInstances; writing empty id for that slot.");
        }
        writer.WriteString(slotsPath + "/" + std::to_string(i), id);
    }
}

bool ReadSubMeshMaterials(const JsonReader& reader,
                          std::string_view componentPath,
                          Entity entity,
                          const LoadContext& ctx)
{
    const std::string slotsPath = Join(componentPath, "slots");
    const std::size_t count = reader.ArraySize(slotsPath);

    Render::SubMeshMaterialsComponent c;
    c.slots.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        // 缺字段 / 类型不符的 slot 当空 id（→ nullptr，回退默认材质），不
        // 整盘拒绝 Load——与 Renderable.materialInstance 缺省 nullptr 同款宽松。
        std::string id = reader.GetString(slotsPath + "/" + std::to_string(i), "");
        Render::MaterialInstance* resolved = MaterialIdToInstance(id, ctx);
        if (!id.empty() && resolved == nullptr)
        {
            ORANGE_LOG_WARN(
                "Scene load: SubMeshMaterialsComponent slot id '{}' could not be "
                "resolved; leaving that slot null (falls back to default material).",
                id);
        }
        c.slots.push_back(resolved);
    }

    ctx.world.AddComponent(entity, std::move(c));
    return true;
}

// ---------------------------------------------------------------------------
// DirectionalLight
//
// 4 个 plain 字段，全部 round-trip。注意结构名是 DirectionalLight 而非
// LightComponent——与 LightComponent.h 头里的实际类型一致。Scene JSON
// 的 component key 用 "DirectionalLight" 也保持类型忠实，方便将来加
// PointLight / SpotLight 时各自占独立 key 而不打架。
// ---------------------------------------------------------------------------

bool HasDirectionalLight(const World& world, Entity entity)
{
    return world.HasComponent<Render::DirectionalLight>(entity);
}

void WriteDirectionalLight(JsonWriter& writer,
                           std::string_view componentPath,
                           Entity entity,
                           const SaveContext& ctx)
{
    const auto* light = ctx.world.GetComponent<Render::DirectionalLight>(entity);
    if (light == nullptr)
    {
        return;
    }

    const float color[3] = {light->color.x, light->color.y, light->color.z};

    // 注意：direction 字段已废 —— 方向由 entity.Transform.rotation 派生，
    // 仅落 color / intensity / castsShadow 三项。旧 scene 的 direction
    // 字段由 Read 路径 graceful 兼容（migrator 转 Transform.rotation）。
    writer.WriteFloatArray(Join(componentPath, "color"),       color, 3);
    writer.WriteFloat(     Join(componentPath, "intensity"),   light->intensity);
    writer.WriteBool(      Join(componentPath, "castsShadow"), light->castsShadow);
}

bool ReadDirectionalLight(const JsonReader& reader,
                          std::string_view componentPath,
                          Entity entity,
                          const LoadContext& ctx)
{
    Render::DirectionalLight light;

    // v1 → v2 migrator：旧 scene 在 DirectionalLight 段写 direction 字段，
    // 新版本方向由 Transform.rotation 派生。命中 direction key 时把它转
    // quat 写回 entity 的 TransformComponent（若 entity 尚未挂 Transform
    // 则新建一个 default 的；保留已挂 Transform 的 position/scale 不动，
    // 只覆盖 rotation —— 旧 scene 里 direction 是光向唯一权威，rotation
    // 字段在旧版本对 light 完全无意义，覆盖它正好等价于"按原视觉迁移"）。
    if (reader.Has(Join(componentPath, "direction")))
    {
        float direction[3] = {0.3f, -1.0f, 0.4f};
        if (reader.ReadFloatArray(Join(componentPath, "direction"), direction, 3))
        {
            const glm::vec3 oldDir{direction[0], direction[1], direction[2]};
            const glm::quat rot = Render::MakeDirectionalLightRotationFromDir(oldDir);
            if (auto* tc = ctx.world.GetComponent<TransformComponent>(entity))
            {
                tc->rotation = rot;
            }
            else
            {
                TransformComponent newTc{};
                newTc.rotation = rot;
                ctx.world.AddComponent(entity, newTc);
            }
        }
    }

    float color[3] = {1.0f, 1.0f, 1.0f};
    if (!reader.ReadFloatArray(Join(componentPath, "color"), color, 3))
    {
        return false;
    }

    double intensity = 1.0;
    if (reader.Has(Join(componentPath, "intensity")))
    {
        if (!reader.ReadFloat(Join(componentPath, "intensity"), intensity))
        {
            return false;
        }
    }

    light.color       = {color[0], color[1], color[2]};
    light.intensity   = static_cast<float>(intensity);
    light.castsShadow = reader.GetBool(Join(componentPath, "castsShadow"), false);

    ctx.world.AddComponent(entity, light);
    return true;
}

// ---------------------------------------------------------------------------
// PointLight —— Render 模块的 PureData 组件（GAP-2026-05-11 G1）。
// 位置由 entity.Transform.position 派生，不在 component 上落 position；
// 字段：color / intensity / range / castsShadow。
// ---------------------------------------------------------------------------

bool HasPointLight(const World& world, Entity entity)
{
    return world.HasComponent<Render::PointLight>(entity);
}

void WritePointLight(JsonWriter& writer,
                     std::string_view componentPath,
                     Entity entity,
                     const SaveContext& ctx)
{
    const auto* light = ctx.world.GetComponent<Render::PointLight>(entity);
    if (light == nullptr) { return; }

    const float color[3] = {light->color.x, light->color.y, light->color.z};
    writer.WriteFloatArray(Join(componentPath, "color"),         color, 3);
    writer.WriteFloat(     Join(componentPath, "intensity"),     light->intensity);
    writer.WriteFloat(     Join(componentPath, "range"),         light->range);
    writer.WriteBool(      Join(componentPath, "castsShadow"),   light->castsShadow);
    // GAP-2026-05-11 G3 halo 字段。全 optional + 默认值与 LightComponent.h
    // struct ctor 默认一致——老 scene 不带这 3 字段 Load 后行为不变（halo
    // 默认 off，与 G3 落地前等价）。
    writer.WriteBool(      Join(componentPath, "haloEnabled"),   light->haloEnabled);
    writer.WriteFloat(     Join(componentPath, "haloRadius"),    light->haloRadius);
    writer.WriteFloat(     Join(componentPath, "haloIntensity"), light->haloIntensity);
}

bool ReadPointLight(const JsonReader& reader,
                    std::string_view componentPath,
                    Entity entity,
                    const LoadContext& ctx)
{
    Render::PointLight light;

    float color[3] = {1.0f, 1.0f, 1.0f};
    if (reader.Has(Join(componentPath, "color")))
    {
        if (!reader.ReadFloatArray(Join(componentPath, "color"), color, 3))
        {
            return false;
        }
    }
    light.color         = {color[0], color[1], color[2]};
    light.intensity     = static_cast<float>(reader.GetFloat(
        Join(componentPath, "intensity"),     light.intensity));
    light.range         = static_cast<float>(reader.GetFloat(
        Join(componentPath, "range"),         light.range));
    light.castsShadow   = reader.GetBool(Join(componentPath, "castsShadow"), false);
    // GAP-2026-05-11 G3 halo 字段（optional read，老 scene 不带 = 默认 off）。
    light.haloEnabled   = reader.GetBool(Join(componentPath, "haloEnabled"),
                                         light.haloEnabled);
    light.haloRadius    = static_cast<float>(reader.GetFloat(
        Join(componentPath, "haloRadius"),    light.haloRadius));
    light.haloIntensity = static_cast<float>(reader.GetFloat(
        Join(componentPath, "haloIntensity"), light.haloIntensity));

    ctx.world.AddComponent(entity, light);
    return true;
}

// ---------------------------------------------------------------------------
// SpotLight —— Render 模块的 PureData 组件（GAP-2026-05-26 G1）。
// 位置由 entity.Transform.position、方向由 entity.Transform.rotation 派生，
// 不在 component 上落几何状态；字段：color / intensity / range /
// innerConeAngle / outerConeAngle（半角弧度）/ castsShadow。
// ---------------------------------------------------------------------------

bool HasSpotLight(const World& world, Entity entity)
{
    return world.HasComponent<Render::SpotLight>(entity);
}

void WriteSpotLight(JsonWriter& writer,
                    std::string_view componentPath,
                    Entity entity,
                    const SaveContext& ctx)
{
    const auto* light = ctx.world.GetComponent<Render::SpotLight>(entity);
    if (light == nullptr) { return; }

    const float color[3] = {light->color.x, light->color.y, light->color.z};
    writer.WriteFloatArray(Join(componentPath, "color"),          color, 3);
    writer.WriteFloat(     Join(componentPath, "intensity"),      light->intensity);
    writer.WriteFloat(     Join(componentPath, "range"),          light->range);
    writer.WriteFloat(     Join(componentPath, "innerConeAngle"), light->innerConeAngle);
    writer.WriteFloat(     Join(componentPath, "outerConeAngle"), light->outerConeAngle);
    writer.WriteBool(      Join(componentPath, "castsShadow"),    light->castsShadow);
}

bool ReadSpotLight(const JsonReader& reader,
                   std::string_view componentPath,
                   Entity entity,
                   const LoadContext& ctx)
{
    Render::SpotLight light;

    float color[3] = {1.0f, 1.0f, 1.0f};
    if (reader.Has(Join(componentPath, "color")))
    {
        if (!reader.ReadFloatArray(Join(componentPath, "color"), color, 3))
        {
            return false;
        }
    }
    light.color          = {color[0], color[1], color[2]};
    light.intensity      = static_cast<float>(reader.GetFloat(
        Join(componentPath, "intensity"),      light.intensity));
    light.range          = static_cast<float>(reader.GetFloat(
        Join(componentPath, "range"),          light.range));
    light.innerConeAngle = static_cast<float>(reader.GetFloat(
        Join(componentPath, "innerConeAngle"), light.innerConeAngle));
    light.outerConeAngle = static_cast<float>(reader.GetFloat(
        Join(componentPath, "outerConeAngle"), light.outerConeAngle));
    light.castsShadow    = reader.GetBool(Join(componentPath, "castsShadow"), false);

    ctx.world.AddComponent(entity, light);
    return true;
}

// ---------------------------------------------------------------------------
// EnvironmentComponent
//
// 三个字段：cubemap（HDR equirect 资产路径，c7 真正接 HDR loader 后才能
// 加载非空数据；c6 阶段允许写空字符串）+ intensity + tint。schema 上与
// RenderableComponent 同样用 AssetRegistry::PathOf<TextureAsset> 反查 path
// 字符串，Load 路径调 AssetRegistry::Load<TextureAsset>，加载失败时留空
// handle 不阻断 scene load——与 mesh handle 的容错策略一致，编辑器修复
// 路径更友好。
// ---------------------------------------------------------------------------

bool HasEnvironment(const World& world, Entity entity)
{
    return world.HasComponent<Render::EnvironmentComponent>(entity);
}

void WriteEnvironment(JsonWriter& writer,
                      std::string_view componentPath,
                      Entity entity,
                      const SaveContext& ctx)
{
    const auto* env = ctx.world.GetComponent<Render::EnvironmentComponent>(entity);
    if (env == nullptr)
    {
        return;
    }

    std::string_view cubemapPath;
    if (ctx.assetRegistry != nullptr && env->cubemap.IsValid())
    {
        cubemapPath = ctx.assetRegistry->PathOf<Asset::TextureAsset>(env->cubemap);
        if (cubemapPath.empty())
        {
            ORANGE_LOG_WARN(
                "Scene save: EnvironmentComponent's cubemap handle has no path in "
                "AssetRegistry; writing empty path.");
        }
    }
    else if (env->cubemap.IsValid() && ctx.assetRegistry == nullptr)
    {
        ORANGE_LOG_WARN(
            "Scene save: EnvironmentComponent has a valid cubemap handle but no AssetRegistry "
            "was supplied to Save(); writing empty path.");
    }

    const float tint[3] = {env->tint.x, env->tint.y, env->tint.z};

    writer.WriteString(    Join(componentPath, "cubemap"),   cubemapPath);
    writer.WriteFloatArray(Join(componentPath, "tint"),      tint, 3);
    writer.WriteFloat(     Join(componentPath, "intensity"), env->intensity);
}

bool ReadEnvironment(const JsonReader& reader,
                     std::string_view componentPath,
                     Entity entity,
                     const LoadContext& ctx)
{
    Render::EnvironmentComponent env;

    std::string cubemapPath;
    if (reader.Has(Join(componentPath, "cubemap")))
    {
        if (!reader.ReadString(Join(componentPath, "cubemap"), cubemapPath))
        {
            return false;
        }
    }

    if (!cubemapPath.empty())
    {
        if (ctx.assetRegistry != nullptr)
        {
            auto loadResult = ctx.assetRegistry->Load<Asset::TextureAsset>(cubemapPath);
            if (loadResult.IsOk())
            {
                env.cubemap = loadResult.Value();
            }
            else
            {
                // 加载失败（如 c7 HDR loader 尚未到位时遇到 .hdr 资产）→
                // 留空 handle，Pipeline 退化到 dummy IBL；warn 提示让 caller
                // 排查，但不阻断 scene load。
                ORANGE_LOG_WARN(
                    "Scene load: failed to load environment cubemap '{}'; leaving handle empty.",
                    cubemapPath);
            }
        }
        else
        {
            ORANGE_LOG_WARN(
                "Scene load: EnvironmentComponent references cubemap '{}' but no AssetRegistry "
                "was supplied to Load(); leaving handle empty.",
                cubemapPath);
        }
    }

    float tint[3] = {1.0f, 1.0f, 1.0f};
    if (reader.Has(Join(componentPath, "tint")))
    {
        if (!reader.ReadFloatArray(Join(componentPath, "tint"), tint, 3))
        {
            return false;
        }
    }

    double intensity = 1.0;
    if (reader.Has(Join(componentPath, "intensity")))
    {
        if (!reader.ReadFloat(Join(componentPath, "intensity"), intensity))
        {
            return false;
        }
    }

    env.tint      = {tint[0], tint[1], tint[2]};
    env.intensity = static_cast<float>(intensity);

    ctx.world.AddComponent(entity, env);
    return true;
}

// ---------------------------------------------------------------------------
// PostProcessComponent
//
// 纯标量字段（无资产 handle）—— 扁平 write/read。所有字段 optional（reader.Has
// 缺省时保留组件默认），便于 schema 演进（新增字段旧场景不报错）+ v2 加 override
// 字段时旧文件兼容。
// ---------------------------------------------------------------------------

bool HasPostProcess(const World& world, Entity entity)
{
    return world.HasComponent<Render::PostProcessComponent>(entity);
}

void WritePostProcess(JsonWriter& writer,
                      std::string_view componentPath,
                      Entity entity,
                      const SaveContext& ctx)
{
    const auto* pp = ctx.world.GetComponent<Render::PostProcessComponent>(entity);
    if (pp == nullptr)
    {
        return;
    }
    const auto P = [&](const char* k) { return Join(componentPath, k); };

    // volume 容器
    writer.WriteInt(P("mode"), static_cast<std::int64_t>(pp->mode));
    const float ext[3] = {pp->localExtent.x, pp->localExtent.y, pp->localExtent.z};
    writer.WriteFloatArray(P("localExtent"), ext, 3);
    writer.WriteFloat(P("priority"),      pp->priority);
    writer.WriteFloat(P("blendDistance"), pp->blendDistance);

    // SSAO / GTAO
    writer.WriteBool( P("ssaoEnabled"),  pp->ssaoEnabled);
    writer.WriteBool( P("ssaoUseGtao"),  pp->ssaoUseGtao);
    writer.WriteFloat(P("ssaoRadius"),   pp->ssaoRadius);
    writer.WriteFloat(P("ssaoStrength"), pp->ssaoStrength);
    writer.WriteFloat(P("ssaoPower"),    pp->ssaoPower);

    // SSR
    writer.WriteBool( P("ssrEnabled"),     pp->ssrEnabled);
    writer.WriteFloat(P("ssrMaxDistance"), pp->ssrMaxDistance);
    writer.WriteFloat(P("ssrThickness"),   pp->ssrThickness);
    writer.WriteFloat(P("ssrStrength"),    pp->ssrStrength);

    // 接触阴影
    writer.WriteBool( P("contactEnabled"),   pp->contactEnabled);
    writer.WriteFloat(P("contactLength"),    pp->contactLength);
    writer.WriteFloat(P("contactThickness"), pp->contactThickness);
    writer.WriteFloat(P("contactStrength"),  pp->contactStrength);

    // 景深
    writer.WriteBool( P("dofEnabled"),       pp->dofEnabled);
    writer.WriteFloat(P("dofFocusDistance"), pp->dofFocusDistance);
    writer.WriteFloat(P("dofFocusRange"),    pp->dofFocusRange);
    writer.WriteFloat(P("dofMaxCoCRadius"),  pp->dofMaxCoCRadius);

    // TAA
    writer.WriteBool( P("taaEnabled"),  pp->taaEnabled);
    writer.WriteFloat(P("taaFeedback"), pp->taaFeedback);

    // 色彩分级
    writer.WriteBool( P("gradeEnabled"),     pp->gradeEnabled);
    writer.WriteFloat(P("gradeExposure"),    pp->gradeExposure);
    writer.WriteFloat(P("gradeContrast"),    pp->gradeContrast);
    writer.WriteFloat(P("gradeSaturation"),  pp->gradeSaturation);
    writer.WriteFloat(P("gradeTemperature"), pp->gradeTemperature);
    writer.WriteFloat(P("gradeTint"),        pp->gradeTint);

    // 相机运动模糊
    writer.WriteBool( P("motionBlurEnabled"),     pp->motionBlurEnabled);
    writer.WriteFloat(P("motionBlurIntensity"),   pp->motionBlurIntensity);
    writer.WriteFloat(P("motionBlurMaxRadius"),   pp->motionBlurMaxRadius);
    writer.WriteInt(  P("motionBlurSampleCount"), static_cast<std::int64_t>(pp->motionBlurSampleCount));

    // 镜头效果（色散 + 暗角）
    writer.WriteBool( P("lensEnabled"),             pp->lensEnabled);
    writer.WriteFloat(P("lensChromaticAberration"), pp->lensChromaticAberration);
    writer.WriteFloat(P("lensVignetteIntensity"),   pp->lensVignetteIntensity);
    writer.WriteFloat(P("lensVignetteSmoothness"),  pp->lensVignetteSmoothness);

    // 锐化（CAS）
    writer.WriteBool( P("sharpenEnabled"),  pp->sharpenEnabled);
    writer.WriteFloat(P("sharpenStrength"), pp->sharpenStrength);

    // 阴影质量
    writer.WriteFloat(P("pcssLightSize"),       pp->pcssLightSize);
    writer.WriteInt(  P("shadowMapResolution"), static_cast<std::int64_t>(pp->shadowMapResolution));
}

bool ReadPostProcess(const JsonReader& reader,
                     std::string_view componentPath,
                     Entity entity,
                     const LoadContext& ctx)
{
    Render::PostProcessComponent pp;  // 起始 = 默认值；下面按需覆盖

    // optional 读辅助（缺省保留默认）。
    const auto readF = [&](const char* k, float& v)
    {
        const auto path = Join(componentPath, k);
        double d = 0.0;
        if (reader.Has(path) && reader.ReadFloat(path, d)) { v = static_cast<float>(d); }
    };
    const auto readB = [&](const char* k, bool& v)
    {
        const auto path = Join(componentPath, k);
        bool b = false;
        if (reader.Has(path) && reader.ReadBool(path, b)) { v = b; }
    };
    const auto readI = [&](const char* k, std::int64_t& v)
    {
        const auto path = Join(componentPath, k);
        std::int64_t i = 0;
        if (reader.Has(path) && reader.ReadInt(path, i)) { v = i; }
    };

    std::int64_t mode = static_cast<std::int64_t>(pp.mode);
    readI("mode", mode);
    pp.mode = (mode == 1) ? Render::PostProcessComponent::Mode::Local
                          : Render::PostProcessComponent::Mode::Global;
    float ext[3] = {pp.localExtent.x, pp.localExtent.y, pp.localExtent.z};
    if (reader.Has(Join(componentPath, "localExtent")))
    {
        reader.ReadFloatArray(Join(componentPath, "localExtent"), ext, 3);
    }
    pp.localExtent = {ext[0], ext[1], ext[2]};
    readF("priority",      pp.priority);
    readF("blendDistance", pp.blendDistance);

    readB("ssaoEnabled",  pp.ssaoEnabled);
    readB("ssaoUseGtao",  pp.ssaoUseGtao);
    readF("ssaoRadius",   pp.ssaoRadius);
    readF("ssaoStrength", pp.ssaoStrength);
    readF("ssaoPower",    pp.ssaoPower);

    readB("ssrEnabled",     pp.ssrEnabled);
    readF("ssrMaxDistance", pp.ssrMaxDistance);
    readF("ssrThickness",   pp.ssrThickness);
    readF("ssrStrength",    pp.ssrStrength);

    readB("contactEnabled",   pp.contactEnabled);
    readF("contactLength",    pp.contactLength);
    readF("contactThickness", pp.contactThickness);
    readF("contactStrength",  pp.contactStrength);

    readB("dofEnabled",       pp.dofEnabled);
    readF("dofFocusDistance", pp.dofFocusDistance);
    readF("dofFocusRange",    pp.dofFocusRange);
    readF("dofMaxCoCRadius",  pp.dofMaxCoCRadius);

    readB("taaEnabled",  pp.taaEnabled);
    readF("taaFeedback", pp.taaFeedback);

    readB("gradeEnabled",     pp.gradeEnabled);
    readF("gradeExposure",    pp.gradeExposure);
    readF("gradeContrast",    pp.gradeContrast);
    readF("gradeSaturation",  pp.gradeSaturation);
    readF("gradeTemperature", pp.gradeTemperature);
    readF("gradeTint",        pp.gradeTint);

    readB("motionBlurEnabled",   pp.motionBlurEnabled);
    readF("motionBlurIntensity", pp.motionBlurIntensity);
    readF("motionBlurMaxRadius", pp.motionBlurMaxRadius);
    std::int64_t mbSamples = static_cast<std::int64_t>(pp.motionBlurSampleCount);
    readI("motionBlurSampleCount", mbSamples);
    pp.motionBlurSampleCount = static_cast<std::int32_t>(mbSamples);

    readB("lensEnabled",             pp.lensEnabled);
    readF("lensChromaticAberration", pp.lensChromaticAberration);
    readF("lensVignetteIntensity",   pp.lensVignetteIntensity);
    readF("lensVignetteSmoothness",  pp.lensVignetteSmoothness);

    readB("sharpenEnabled",  pp.sharpenEnabled);
    readF("sharpenStrength", pp.sharpenStrength);

    readF("pcssLightSize", pp.pcssLightSize);
    std::int64_t shadowRes = static_cast<std::int64_t>(pp.shadowMapResolution);
    readI("shadowMapResolution", shadowRes);
    pp.shadowMapResolution = static_cast<std::uint32_t>(shadowRes);

    ctx.world.AddComponent(entity, pp);
    return true;
}

// ---------------------------------------------------------------------------
// RigidBodyComponent
//
// 序列化 body 的"初始 desc"——type / initialPosition / initialAngle /
// 阻尼 / fixedRotation / gravityScale。**不**写运行时 velocity / 当前
// transform / handle——那些属于"runtime player state"，由 Save Game
// 子系统单独处理。
//
// Load 路径不在这里 attach component：见 SceneSerialization Pass 2。
// ---------------------------------------------------------------------------

const char* BodyTypeToString(Physics::BodyType type) noexcept
{
    switch (type)
    {
    case Physics::BodyType::Static:    return "Static";
    case Physics::BodyType::Kinematic: return "Kinematic";
    case Physics::BodyType::Dynamic:   return "Dynamic";
    }
    return "Dynamic";
}

bool BodyTypeFromString(std::string_view s, Physics::BodyType& out)
{
    if      (s == "Static")    { out = Physics::BodyType::Static;    return true; }
    else if (s == "Kinematic") { out = Physics::BodyType::Kinematic; return true; }
    else if (s == "Dynamic")   { out = Physics::BodyType::Dynamic;   return true; }
    return false;
}

bool HasRigidBody(const World& world, Entity entity)
{
    return world.HasComponent<Physics::RigidBodyComponent>(entity);
}

void WriteRigidBody(JsonWriter& writer,
                    std::string_view componentPath,
                    Entity entity,
                    const SaveContext& ctx)
{
    const auto* rb = ctx.world.GetComponent<Physics::RigidBodyComponent>(entity);
    if (rb == nullptr)
    {
        return;
    }

    writer.WriteString(Join(componentPath, "type"), BodyTypeToString(rb->type));

    const float pos[2] = {rb->initialPosition.x, rb->initialPosition.y};
    writer.WriteFloatArray(Join(componentPath, "initialPosition"), pos, 2);
    writer.WriteFloat(Join(componentPath, "initialAngle"),    rb->initialAngle);

    writer.WriteFloat(Join(componentPath, "linearDamping"),   rb->linearDamping);
    writer.WriteFloat(Join(componentPath, "angularDamping"),  rb->angularDamping);
    writer.WriteBool( Join(componentPath, "fixedRotation"),   rb->fixedRotation);
    writer.WriteFloat(Join(componentPath, "gravityScale"),    rb->gravityScale);

    // velocity / handle 刻意不写——前者属于 runtime state（Save Game 范围），
    // 后者是 backend 句柄，Load 时由 PhysicsWorld::AddBody 重新分配。
}

// ---------------------------------------------------------------------------
// ColliderComponent
//
// shape 是 std::variant<Circle, Box, Polygon, EdgeChain>——序列化用
// 字符串 "kind" 字段做 discriminator + per-kind 数据字段。Polygon /
// EdgeChain 的顶点用扁平化 float 数组（[x0,y0,x1,y1,...]），与 JsonReader
// 的纯数字路径段语义对齐。
// ---------------------------------------------------------------------------

void WriteCircleShape(JsonWriter& writer, std::string_view shapePath, const Physics::CircleDesc& d)
{
    writer.WriteString(Join(shapePath, "kind"), "Circle");
    writer.WriteFloat( Join(shapePath, "radius"), d.radius);
    const float c[2] = {d.center.x, d.center.y};
    writer.WriteFloatArray(Join(shapePath, "center"), c, 2);
}

void WriteBoxShape(JsonWriter& writer, std::string_view shapePath, const Physics::BoxDesc& d)
{
    writer.WriteString(Join(shapePath, "kind"), "Box");
    const float he[2] = {d.halfExtents.x, d.halfExtents.y};
    writer.WriteFloatArray(Join(shapePath, "halfExtents"), he, 2);
    const float c[2] = {d.center.x, d.center.y};
    writer.WriteFloatArray(Join(shapePath, "center"), c, 2);
}

void WritePolygonShape(JsonWriter& writer, std::string_view shapePath, const Physics::PolygonDesc& d)
{
    writer.WriteString(Join(shapePath, "kind"), "Polygon");
    std::vector<float> flat;
    flat.reserve(static_cast<std::size_t>(d.count) * 2u);
    for (std::uint32_t i = 0; i < d.count; ++i)
    {
        flat.push_back(d.vertices[i].x);
        flat.push_back(d.vertices[i].y);
    }
    writer.WriteFloatArray(Join(shapePath, "vertices"), flat.data(), flat.size());
}

void WriteEdgeChainShape(JsonWriter& writer, std::string_view shapePath, const Physics::EdgeChainDesc& d)
{
    writer.WriteString(Join(shapePath, "kind"), "EdgeChain");
    std::vector<float> flat;
    flat.reserve(static_cast<std::size_t>(d.count) * 2u);
    for (std::uint32_t i = 0; i < d.count; ++i)
    {
        flat.push_back(d.vertices[i].x);
        flat.push_back(d.vertices[i].y);
    }
    writer.WriteFloatArray(Join(shapePath, "vertices"), flat.data(), flat.size());
    writer.WriteBool(Join(shapePath, "isLoop"), d.isLoop);
}

bool HasCollider(const World& world, Entity entity)
{
    return world.HasComponent<Physics::ColliderComponent>(entity);
}

void WriteCollider(JsonWriter& writer,
                   std::string_view componentPath,
                   Entity entity,
                   const SaveContext& ctx)
{
    const auto* col = ctx.world.GetComponent<Physics::ColliderComponent>(entity);
    if (col == nullptr)
    {
        return;
    }

    const std::string shapePath = Join(componentPath, "shape");
    std::visit(
        [&](const auto& shape)
        {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, Physics::CircleDesc>)
            {
                WriteCircleShape(writer, shapePath, shape);
            }
            else if constexpr (std::is_same_v<T, Physics::BoxDesc>)
            {
                WriteBoxShape(writer, shapePath, shape);
            }
            else if constexpr (std::is_same_v<T, Physics::PolygonDesc>)
            {
                WritePolygonShape(writer, shapePath, shape);
            }
            else if constexpr (std::is_same_v<T, Physics::EdgeChainDesc>)
            {
                WriteEdgeChainShape(writer, shapePath, shape);
            }
        },
        col->shape);

    writer.WriteFloat(Join(componentPath, "density"),     col->density);
    writer.WriteFloat(Join(componentPath, "friction"),    col->friction);
    writer.WriteFloat(Join(componentPath, "restitution"), col->restitution);
    writer.WriteBool( Join(componentPath, "isSensor"),    col->isSensor);
}

// ---------------------------------------------------------------------------
// ParticleEmitterComponent —— Render 模块的 PureData 组件，desc + emitting
// flag 1:1 落 JSON。运行时粒子池由 VfxSystem 拥有，不持久化（属于
// runtime state，归 Save Game）。
// ---------------------------------------------------------------------------

bool HasParticleEmitter(const World& world, Entity entity)
{
    return world.HasComponent<Render::ParticleEmitterComponent>(entity);
}

void WriteParticleEmitter(JsonWriter& writer,
                          std::string_view componentPath,
                          Entity entity,
                          const SaveContext& ctx)
{
    const auto* pe = ctx.world.GetComponent<Render::ParticleEmitterComponent>(entity);
    if (pe == nullptr)
    {
        return;
    }
    const auto& d = pe->desc;

    writer.WriteFloat(Join(componentPath, "emissionRate"), d.emissionRate);
    writer.WriteFloat(Join(componentPath, "lifetimeMin"),  d.lifetimeMin);
    writer.WriteFloat(Join(componentPath, "lifetimeMax"),  d.lifetimeMax);

    const float spawnMin[2] = {d.spawnOffsetMin.x, d.spawnOffsetMin.y};
    const float spawnMax[2] = {d.spawnOffsetMax.x, d.spawnOffsetMax.y};
    writer.WriteFloatArray(Join(componentPath, "spawnOffsetMin"), spawnMin, 2);
    writer.WriteFloatArray(Join(componentPath, "spawnOffsetMax"), spawnMax, 2);

    const float velMin[2] = {d.initialVelocityMin.x, d.initialVelocityMin.y};
    const float velMax[2] = {d.initialVelocityMax.x, d.initialVelocityMax.y};
    writer.WriteFloatArray(Join(componentPath, "initialVelocityMin"), velMin, 2);
    writer.WriteFloatArray(Join(componentPath, "initialVelocityMax"), velMax, 2);

    const float gravity[2] = {d.gravity.x, d.gravity.y};
    writer.WriteFloatArray(Join(componentPath, "gravity"), gravity, 2);

    const float colorStart[4] = {d.colorStart.r, d.colorStart.g, d.colorStart.b, d.colorStart.a};
    const float colorEnd[4]   = {d.colorEnd.r,   d.colorEnd.g,   d.colorEnd.b,   d.colorEnd.a};
    writer.WriteFloatArray(Join(componentPath, "colorStart"), colorStart, 4);
    writer.WriteFloatArray(Join(componentPath, "colorEnd"),   colorEnd,   4);

    writer.WriteFloat(Join(componentPath, "sizeStart"), d.sizeStart);
    writer.WriteFloat(Join(componentPath, "sizeEnd"),   d.sizeEnd);
    writer.WriteInt(  Join(componentPath, "maxParticles"),
                      static_cast<std::int64_t>(d.maxParticles));

    writer.WriteBool(Join(componentPath, "emitting"), pe->emitting);
}

bool ReadParticleEmitter(const JsonReader& reader,
                         std::string_view componentPath,
                         Entity entity,
                         const LoadContext& ctx)
{
    Render::ParticleEmitterComponent pe{};
    auto& d = pe.desc;

    // 数值字段都走"缺字段→默认值"语义；需要的硬要求只是 colorStart/End
    // 与 spawn / velocity 区间是 vec 数组，类型不匹配视为格式坏。
    d.emissionRate = static_cast<float>(reader.GetFloat(Join(componentPath, "emissionRate"),
                                                       d.emissionRate));
    d.lifetimeMin  = static_cast<float>(reader.GetFloat(Join(componentPath, "lifetimeMin"),
                                                       d.lifetimeMin));
    d.lifetimeMax  = static_cast<float>(reader.GetFloat(Join(componentPath, "lifetimeMax"),
                                                       d.lifetimeMax));

    auto readVec2 = [&](const std::string& path, glm::vec2& out) -> bool
    {
        if (!reader.Has(path))
        {
            return true;
        }
        float buf[2] = {out.x, out.y};
        if (!reader.ReadFloatArray(path, buf, 2))
        {
            return false;
        }
        out = {buf[0], buf[1]};
        return true;
    };
    auto readVec4 = [&](const std::string& path, glm::vec4& out) -> bool
    {
        if (!reader.Has(path))
        {
            return true;
        }
        float buf[4] = {out.x, out.y, out.z, out.w};
        if (!reader.ReadFloatArray(path, buf, 4))
        {
            return false;
        }
        out = {buf[0], buf[1], buf[2], buf[3]};
        return true;
    };

    if (!readVec2(Join(componentPath, "spawnOffsetMin"),     d.spawnOffsetMin)) return false;
    if (!readVec2(Join(componentPath, "spawnOffsetMax"),     d.spawnOffsetMax)) return false;
    if (!readVec2(Join(componentPath, "initialVelocityMin"), d.initialVelocityMin)) return false;
    if (!readVec2(Join(componentPath, "initialVelocityMax"), d.initialVelocityMax)) return false;
    if (!readVec2(Join(componentPath, "gravity"),            d.gravity))         return false;
    if (!readVec4(Join(componentPath, "colorStart"),         d.colorStart))      return false;
    if (!readVec4(Join(componentPath, "colorEnd"),           d.colorEnd))        return false;

    d.sizeStart = static_cast<float>(reader.GetFloat(Join(componentPath, "sizeStart"), d.sizeStart));
    d.sizeEnd   = static_cast<float>(reader.GetFloat(Join(componentPath, "sizeEnd"),   d.sizeEnd));
    d.maxParticles = static_cast<std::uint32_t>(
        reader.GetInt(Join(componentPath, "maxParticles"),
                      static_cast<std::int64_t>(d.maxParticles)));

    pe.emitting = reader.GetBool(Join(componentPath, "emitting"), pe.emitting);

    ctx.world.AddComponent(entity, pe);
    return true;
}

// ---------------------------------------------------------------------------
// AudioSourceComponent —— Audio 模块的 PureData 组件。
//
// 与 RenderableComponent.mesh 同款 AssetHandle 路径模式：写时用
// AssetRegistry::PathOf 反查；读时按 path 调 AssetRegistry::Load。
// 运行时 SoundInstance 句柄不属于 component（公共面 PIMPL 不暴露），
// 由 PlayMode 驱动方（编辑器 EditorRenderLayer / 游戏侧 system）自持
// entity→instance map，详见 AudioSourceComponent.h 头注释。
// ---------------------------------------------------------------------------

bool HasAudioSource(const World& world, Entity entity)
{
    return world.HasComponent<Audio::AudioSourceComponent>(entity);
}

void WriteAudioSource(JsonWriter& writer,
                      std::string_view componentPath,
                      Entity entity,
                      const SaveContext& ctx)
{
    const auto* a = ctx.world.GetComponent<Audio::AudioSourceComponent>(entity);
    if (a == nullptr)
    {
        return;
    }

    std::string_view soundPath;
    if (ctx.assetRegistry != nullptr && a->sound.IsValid())
    {
        soundPath = ctx.assetRegistry->PathOf<Asset::SoundAsset>(a->sound);
        if (soundPath.empty())
        {
            ORANGE_LOG_WARN(
                "Scene save: AudioSourceComponent's sound handle has no path in "
                "AssetRegistry; writing empty path.");
        }
    }
    else if (a->sound.IsValid() && ctx.assetRegistry == nullptr)
    {
        ORANGE_LOG_WARN(
            "Scene save: AudioSourceComponent has a valid sound handle but no "
            "AssetRegistry was supplied to Save(); writing empty path.");
    }

    writer.WriteString(Join(componentPath, "sound"),       soundPath);
    writer.WriteBool(  Join(componentPath, "playOnAwake"), a->playOnAwake);
    writer.WriteBool(  Join(componentPath, "loop"),        a->loop);
    writer.WriteFloat( Join(componentPath, "volume"),      a->volume);
    writer.WriteFloat( Join(componentPath, "pitch"),       a->pitch);
}

bool ReadAudioSource(const JsonReader& reader,
                     std::string_view componentPath,
                     Entity entity,
                     const LoadContext& ctx)
{
    Audio::AudioSourceComponent a;

    // sound 字段可以为空（"未配音"），但若 key 缺失视为格式坏 —— 与
    // ReadRenderable.mesh 同款约定。
    std::string soundPath;
    if (!reader.ReadString(Join(componentPath, "sound"), soundPath))
    {
        return false;
    }
    if (!soundPath.empty())
    {
        if (ctx.assetRegistry != nullptr)
        {
            auto loadResult = ctx.assetRegistry->Load<Asset::SoundAsset>(soundPath);
            if (loadResult.IsOk())
            {
                a.sound = loadResult.Value();
            }
            else
            {
                ORANGE_LOG_WARN("Scene load: failed to load sound '{}'; leaving handle empty.",
                                soundPath);
            }
        }
        else
        {
            ORANGE_LOG_WARN(
                "Scene load: AudioSourceComponent references sound '{}' but no AssetRegistry "
                "was supplied to Load(); leaving handle empty.",
                soundPath);
        }
    }

    a.playOnAwake = reader.GetBool( Join(componentPath, "playOnAwake"), a.playOnAwake);
    a.loop        = reader.GetBool( Join(componentPath, "loop"),        a.loop);
    a.volume      = static_cast<float>(reader.GetFloat(Join(componentPath, "volume"), a.volume));
    a.pitch       = static_cast<float>(reader.GetFloat(Join(componentPath, "pitch"),  a.pitch));

    ctx.world.AddComponent(entity, a);
    return true;
}

// ---------------------------------------------------------------------------
// AnimatorComponent
//
// 当前阶段 Animator 的"重建数据"只是 backend 名字。SkeletalAnimator
// 需要 DragonBonesContext + SkeletonAsset + armatureName，ProceduralAnimator
// 需要任意闭包 channel——这些都不在 IAnimator 公共面、也无法纯数据
// 化。Load 时调 AnimatorRegistry::Create(name)，由游戏端在注册 factory
// 时 capture 好相关参数。
//
// 因此 scene 序列化对 Animator 的承诺仅限于"哪个 entity 有 Animator
// 以及是哪个 backend"——具体的播放状态 / channel 配置不在范围内。
// ---------------------------------------------------------------------------

bool HasAnimator(const World& world, Entity entity)
{
    return world.HasComponent<Animation::AnimatorComponent>(entity);
}

void WriteAnimator(JsonWriter& writer,
                   std::string_view componentPath,
                   Entity entity,
                   const SaveContext& ctx)
{
    const auto* ac = ctx.world.GetComponent<Animation::AnimatorComponent>(entity);
    if (ac == nullptr || ac->animator == nullptr)
    {
        return;
    }

    writer.WriteString(Join(componentPath, "backend"), ac->animator->BackendName());

    // "clip" backend 例外（B2.2）：ClipAnimator 的关键帧数据可纯数据化，
    // 额外以"形态 B"嵌入 clip 的 JSON 字符串（紧凑、单字段）。其它 backend
    // （procedural / skeletal）仍只持久化名字，由 AnimatorRegistry factory 重建。
    if (ac->animator->BackendName() == "clip")
    {
        const auto* clipAnim = static_cast<const Animation::ClipAnimator*>(ac->animator.get());
        writer.WriteString(Join(componentPath, "clipJson"),
                           Animation::AnimationClipToJson(clipAnim->Clip(), -1));
    }
}

// ---------------------------------------------------------------------------
// Camera
//
// 保存 view 和 projection 两个 mat4（各 16 个 float，列主序）。
// Load 时 view 是可选的——EditorCamera 每帧都会覆写，所以不依赖存档值；
// projection 是必填，缺失则视为损坏数据，返回 false。
// ---------------------------------------------------------------------------

bool HasCamera(const World& world, Entity entity)
{
    return world.HasComponent<Render::Camera>(entity);
}

void WriteCamera(JsonWriter& writer,
                 std::string_view componentPath,
                 Entity entity,
                 const SaveContext& ctx)
{
    const auto* cam = ctx.world.GetComponent<Render::Camera>(entity);
    if (cam == nullptr) { return; }

    float view[16], proj[16];
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            view[c * 4 + r] = cam->view[c][r];
            proj[c * 4 + r] = cam->projection[c][r];
        }
    }
    writer.WriteFloatArray(Join(componentPath, "view"),       view, 16);
    writer.WriteFloatArray(Join(componentPath, "projection"), proj, 16);
}

bool ReadCamera(const JsonReader& reader,
                std::string_view  componentPath,
                Entity            entity,
                const LoadContext& ctx)
{
    // view 可选（EditorCamera 每帧覆写）
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    reader.ReadFloatArray(Join(componentPath, "view"), view, 16);

    float proj[16] = {};
    if (!reader.ReadFloatArray(Join(componentPath, "projection"), proj, 16))
        return false;

    Render::Camera cam;
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            cam.view[c][r]       = view[c * 4 + r];
            cam.projection[c][r] = proj[c * 4 + r];
        }
    }
    ctx.world.AddComponent(entity, cam);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Backend-dependent helper read 接口（由 SceneSerialization Pass 2 调用）
// ---------------------------------------------------------------------------

bool ReadRigidBodyDesc(const JsonReader& reader,
                       std::string_view  componentPath,
                       Physics::RigidBodyComponent& out)
{
    Physics::RigidBodyComponent rb{};

    std::string typeStr;
    if (!reader.ReadString(Join(componentPath, "type"), typeStr))
    {
        return false;
    }
    if (!BodyTypeFromString(typeStr, rb.type))
    {
        return false;
    }

    float pos[2] = {0.0f, 0.0f};
    if (!reader.ReadFloatArray(Join(componentPath, "initialPosition"), pos, 2))
    {
        return false;
    }
    rb.initialPosition = {pos[0], pos[1]};

    double angle = 0.0;
    if (!reader.ReadFloat(Join(componentPath, "initialAngle"), angle))
    {
        return false;
    }
    rb.initialAngle = static_cast<float>(angle);

    // 阻尼 / fixedRotation / gravityScale 走"缺字段→默认值"语义——便于
    // minor schema 升级时新增字段不破老存档。
    rb.linearDamping  = static_cast<float>(reader.GetFloat(Join(componentPath, "linearDamping"),  0.0));
    rb.angularDamping = static_cast<float>(reader.GetFloat(Join(componentPath, "angularDamping"), 0.0));
    rb.fixedRotation  = reader.GetBool(Join(componentPath, "fixedRotation"), false);
    rb.gravityScale   = static_cast<float>(reader.GetFloat(Join(componentPath, "gravityScale"), 1.0));

    // velocity / handle 不读——前者是 runtime state、后者由 AddBody 反写。
    out = rb;
    return true;
}

namespace
{

bool ReadCircleShape(const JsonReader& reader, std::string_view shapePath, Physics::CircleDesc& out)
{
    Physics::CircleDesc d{};
    double radius = 1.0;
    if (!reader.ReadFloat(Join(shapePath, "radius"), radius))
    {
        return false;
    }
    d.radius = static_cast<float>(radius);
    float c[2] = {0.0f, 0.0f};
    if (reader.Has(Join(shapePath, "center")))
    {
        if (!reader.ReadFloatArray(Join(shapePath, "center"), c, 2))
        {
            return false;
        }
    }
    d.center = {c[0], c[1]};
    out = d;
    return true;
}

bool ReadBoxShape(const JsonReader& reader, std::string_view shapePath, Physics::BoxDesc& out)
{
    Physics::BoxDesc d{};
    float he[2] = {0.5f, 0.5f};
    if (!reader.ReadFloatArray(Join(shapePath, "halfExtents"), he, 2))
    {
        return false;
    }
    d.halfExtents = {he[0], he[1]};
    float c[2] = {0.0f, 0.0f};
    if (reader.Has(Join(shapePath, "center")))
    {
        if (!reader.ReadFloatArray(Join(shapePath, "center"), c, 2))
        {
            return false;
        }
    }
    d.center = {c[0], c[1]};
    out = d;
    return true;
}

bool ReadPolygonShape(const JsonReader& reader, std::string_view shapePath, Physics::PolygonDesc& out)
{
    const std::string verticesPath = Join(shapePath, "vertices");
    const std::size_t flatSize = reader.ArraySize(verticesPath);
    if (flatSize == 0 || (flatSize % 2u) != 0u)
    {
        return false;
    }
    const std::size_t count = flatSize / 2u;
    if (count > Physics::PolygonDesc::kMaxVertices)
    {
        return false;
    }

    std::vector<float> flat(flatSize, 0.0f);
    if (!reader.ReadFloatArray(verticesPath, flat.data(), flatSize))
    {
        return false;
    }

    Physics::PolygonDesc d{};
    d.count = static_cast<std::uint32_t>(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        d.vertices[i] = {flat[i * 2], flat[i * 2 + 1]};
    }
    out = d;
    return true;
}

bool ReadEdgeChainShape(const JsonReader& reader, std::string_view shapePath, Physics::EdgeChainDesc& out)
{
    const std::string verticesPath = Join(shapePath, "vertices");
    const std::size_t flatSize = reader.ArraySize(verticesPath);
    if (flatSize == 0 || (flatSize % 2u) != 0u)
    {
        return false;
    }
    const std::size_t count = flatSize / 2u;
    if (count > Physics::EdgeChainDesc::kMaxVertices)
    {
        return false;
    }

    std::vector<float> flat(flatSize, 0.0f);
    if (!reader.ReadFloatArray(verticesPath, flat.data(), flatSize))
    {
        return false;
    }

    Physics::EdgeChainDesc d{};
    d.count = static_cast<std::uint32_t>(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        d.vertices[i] = {flat[i * 2], flat[i * 2 + 1]};
    }
    d.isLoop = reader.GetBool(Join(shapePath, "isLoop"), false);
    out = d;
    return true;
}

}  // namespace

bool ReadColliderDesc(const JsonReader& reader,
                      std::string_view  componentPath,
                      Physics::ColliderComponent& out)
{
    Physics::ColliderComponent col{};

    const std::string shapePath = Join(componentPath, "shape");
    std::string kind;
    if (!reader.ReadString(Join(shapePath, "kind"), kind))
    {
        return false;
    }

    if (kind == "Circle")
    {
        Physics::CircleDesc d{};
        if (!ReadCircleShape(reader, shapePath, d)) return false;
        col.shape = d;
    }
    else if (kind == "Box")
    {
        Physics::BoxDesc d{};
        if (!ReadBoxShape(reader, shapePath, d)) return false;
        col.shape = d;
    }
    else if (kind == "Polygon")
    {
        Physics::PolygonDesc d{};
        if (!ReadPolygonShape(reader, shapePath, d)) return false;
        col.shape = d;
    }
    else if (kind == "EdgeChain")
    {
        Physics::EdgeChainDesc d{};
        if (!ReadEdgeChainShape(reader, shapePath, d)) return false;
        col.shape = d;
    }
    else
    {
        // 未知 shape kind → 视为格式坏。新增 shape 类型必然伴随 schema
        // major bump（Reader 在主调度处先拦掉），不在这里 graceful 跳过。
        return false;
    }

    col.density     = static_cast<float>(reader.GetFloat(Join(componentPath, "density"),     1.0));
    col.friction    = static_cast<float>(reader.GetFloat(Join(componentPath, "friction"),    0.3));
    col.restitution = static_cast<float>(reader.GetFloat(Join(componentPath, "restitution"), 0.0));
    col.isSensor    = reader.GetBool(Join(componentPath, "isSensor"), false);

    out = col;
    return true;
}

bool ReadAnimatorBackendName(const JsonReader& reader,
                             std::string_view  componentPath,
                             std::string&      outBackendName)
{
    return reader.ReadString(Join(componentPath, "backend"), outBackendName);
}

bool ReadAnimatorClipJson(const JsonReader& reader,
                          std::string_view  componentPath,
                          std::string&      outClipJson)
{
    return reader.ReadString(Join(componentPath, "clipJson"), outClipJson);
}

const std::vector<ComponentSerializerEntry>& GetBuiltinComponentSerializers()
{
    static const std::vector<ComponentSerializerEntry> kEntries = {
        // Pure-data：Pass 1 直接 Read 即可 attach。
        {"Transform",        ComponentKind::PureData,         &HasTransform,        &WriteTransform,        &ReadTransform},
        {"Hierarchy",        ComponentKind::PureData,         &HasHierarchy,        &WriteHierarchy,        &ReadHierarchy},
        {"Name",             ComponentKind::PureData,         &HasName,             &WriteName,             &ReadName},
        {"Guid",             ComponentKind::PureData,         &HasGuid,             &WriteGuid,             &ReadGuid},
        {"PrefabInstance",   ComponentKind::PureData,         &HasPrefabInstance,   &WritePrefabInstance,   &ReadPrefabInstance},
        {"Layer",            ComponentKind::PureData,         &HasLayer,            &WriteLayer,            &ReadLayer},
        {"Renderable",       ComponentKind::PureData,         &HasRenderable,       &WriteRenderable,       &ReadRenderable},
        {"SubMeshMaterials", ComponentKind::PureData,         &HasSubMeshMaterials, &WriteSubMeshMaterials, &ReadSubMeshMaterials},
        {"DirectionalLight", ComponentKind::PureData,         &HasDirectionalLight, &WriteDirectionalLight, &ReadDirectionalLight},
        {"PointLight",       ComponentKind::PureData,         &HasPointLight,       &WritePointLight,       &ReadPointLight},
        {"SpotLight",        ComponentKind::PureData,         &HasSpotLight,        &WriteSpotLight,        &ReadSpotLight},
        {"Environment",      ComponentKind::PureData,         &HasEnvironment,      &WriteEnvironment,      &ReadEnvironment},
        {"PostProcess",      ComponentKind::PureData,         &HasPostProcess,      &WritePostProcess,      &ReadPostProcess},
        {"ParticleEmitter",  ComponentKind::PureData,         &HasParticleEmitter,  &WriteParticleEmitter,  &ReadParticleEmitter},
        {"AudioSource",      ComponentKind::PureData,         &HasAudioSource,      &WriteAudioSource,      &ReadAudioSource},
        {"Camera",           ComponentKind::PureData,         &HasCamera,           &WriteCamera,           &ReadCamera},

        // Backend-dependent：Pass 2 由 SceneSerialization 主流程按 entity
        // 配对调用 PhysicsWorld::AddBody / AnimatorRegistry::Create；这里
        // Read 字段保持 nullptr，主流程不会经由 dispatch 调它。
        {"RigidBody",        ComponentKind::BackendDependent, &HasRigidBody, &WriteRigidBody, nullptr},
        {"Collider",         ComponentKind::BackendDependent, &HasCollider,  &WriteCollider,  nullptr},
        {"Animator",         ComponentKind::BackendDependent, &HasAnimator,  &WriteAnimator,  nullptr},
    };
    return kEntries;
}

}  // namespace Orange::Engine::Scene
