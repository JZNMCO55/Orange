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

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/MeshAsset.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/render/LightComponent.h"
#include "orange/engine/render/RenderableComponent.h"
#include "orange/engine/scene/HierarchyComponent.h"
#include "orange/engine/scene/NameComponent.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"

#include <cstdint>
#include <string>
#include <string_view>

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

    h.parent      = EntityForPersistentId(parent,      ctx.idToEntity);
    h.firstChild  = EntityForPersistentId(firstChild,  ctx.idToEntity);
    h.nextSibling = EntityForPersistentId(nextSibling, ctx.idToEntity);
    h.prevSibling = EntityForPersistentId(prevSibling, ctx.idToEntity);

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

    writer.WriteString(Join(componentPath, "mesh"),        meshPath);
    writer.WriteBool(  Join(componentPath, "visible"),     r->visible);
    writer.WriteBool(  Join(componentPath, "castsShadow"), r->castsShadow);
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

    // materialInstance 留 nullptr —— 调用方在 Load 之后重新挂 instance。
    ctx.world.AddComponent(entity, r);
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

    const float direction[3] = {light->direction.x, light->direction.y, light->direction.z};
    const float color[3]     = {light->color.x,     light->color.y,     light->color.z};

    writer.WriteFloatArray(Join(componentPath, "direction"),   direction, 3);
    writer.WriteFloatArray(Join(componentPath, "color"),       color,     3);
    writer.WriteFloat(     Join(componentPath, "intensity"),   light->intensity);
    writer.WriteBool(      Join(componentPath, "castsShadow"), light->castsShadow);
}

bool ReadDirectionalLight(const JsonReader& reader,
                          std::string_view componentPath,
                          Entity entity,
                          const LoadContext& ctx)
{
    Render::DirectionalLight light;

    float direction[3] = {0.3f, -1.0f, 0.4f};
    if (!reader.ReadFloatArray(Join(componentPath, "direction"), direction, 3))
    {
        return false;
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

    light.direction   = {direction[0], direction[1], direction[2]};
    light.color       = {color[0],     color[1],     color[2]};
    light.intensity   = static_cast<float>(intensity);
    light.castsShadow = reader.GetBool(Join(componentPath, "castsShadow"), false);

    ctx.world.AddComponent(entity, light);
    return true;
}

}  // namespace

const std::vector<ComponentSerializerEntry>& GetBuiltinComponentSerializers()
{
    static const std::vector<ComponentSerializerEntry> kEntries = {
        {"Transform",        &HasTransform,        &WriteTransform,        &ReadTransform},
        {"Hierarchy",        &HasHierarchy,        &WriteHierarchy,        &ReadHierarchy},
        {"Name",             &HasName,             &WriteName,             &ReadName},
        {"Renderable",       &HasRenderable,       &WriteRenderable,       &ReadRenderable},
        {"DirectionalLight", &HasDirectionalLight, &WriteDirectionalLight, &ReadDirectionalLight},
    };
    return kEntries;
}

}  // namespace Orange::Engine::Scene
