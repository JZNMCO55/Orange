#include "McpCommandHandler.h"

#include "../EditorHost.h"
#include "../schema/ComponentSchemaRegistry.h"

#include "../schema/PropertyDescriptor.h"

#include <orange/engine/core/Guid.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <string>
#include <unordered_map>
#include <vector>

namespace Orange::Editor::Mcp
{

namespace
{

using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;

// MCP 命令端的协议 / 版本握手常量。
constexpr const char* kEditorVersion   = "OrangeEditor 1.3.0";
constexpr const char* kProtocolVersion = "1.0";

// get_scene_info 单响应实体上限（NF-9 大场景护栏）：超过则截断 + truncated 标记，
// 防单响应爆 token / 爆帧预算。M0 取保守值，后续可加分页参数。
constexpr std::size_t kMaxSceneEntities = 2000;

// currentScenePath → 仅文件名（去目录）。空 → "untitled"。
std::string SceneDisplayName(const std::string& path)
{
    if (path.empty()) { return "untitled"; }
    const std::size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// 把 JsonWriter 产出的对象 dump 成单行 NDJSON（Dump(-1) = nlohmann 紧凑模式，
// 无换行，满足 NDJSON 一行一消息）。
std::string DumpLine(const JsonWriter& w)
{
    return w.Dump(-1);
}

// capture_viewport 截图回读尺寸硬上限（NF-6：防 w*h*4 整数溢出 / 巨帧爆显存与
// token）。viewport 实际尺寸远小于此；纯防御。
constexpr std::uint32_t kMaxCaptureDim = 8192;

// 标准 base64 编码（capture_viewport 把 BGRA 像素编成 ASCII 塞进 JSON 字符串）。
std::string Base64Encode(const std::uint8_t* data, std::size_t len)
{
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 2 < len; i += 3)
    {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                 static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back(kTable[n & 0x3F]);
    }
    const std::size_t rem = len - i;
    if (rem == 1)
    {
        const std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (rem == 2)
    {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back(kTable[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

// 统一错误响应：{"id":id,"ok":false,"error":msg}
std::string MakeError(std::int64_t id, const std::string& msg)
{
    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", false);
    w.WriteString("error", msg);
    return DumpLine(w);
}

// PropertyType → 稳定字符串名（list_component_types / 调试用）。与 schema 字段
// 类型枚举一一对应；AI 据此决定 set_field 时该用什么 JSON 值形态（M2）。
const char* PropertyTypeName(Schema::PropertyType t)
{
    using PT = Schema::PropertyType;
    switch (t)
    {
        case PT::Float:             return "Float";
        case PT::Int:               return "Int";
        case PT::UInt:              return "UInt";
        case PT::Bool:              return "Bool";
        case PT::Vec2:              return "Vec2";
        case PT::Vec3:              return "Vec3";
        case PT::Vec4:              return "Vec4";
        case PT::Quat:              return "Quat";
        case PT::String:            return "String";
        case PT::Enum:              return "Enum";
        case PT::EntityRef:         return "EntityRef";
        case PT::AssetRef:          return "AssetRef";
        case PT::AssetRefArray:     return "AssetRefArray";
        case PT::PolygonVertices:   return "PolygonVertices";
        case PT::EdgeChainVertices: return "EdgeChainVertices";
    }
    return "Unknown";
}

const char* AssetKindName(Schema::AssetKind k)
{
    using AK = Schema::AssetKind;
    switch (k)
    {
        case AK::Unknown:       return "Unknown";
        case AK::Mesh:          return "Mesh";
        case AK::Material:      return "Material";
        case AK::Texture:       return "Texture";
        case AK::Scene:         return "Scene";
        case AK::Sound:         return "Sound";
        case AK::AnimationClip: return "AnimationClip";
    }
    return "Unknown";
}

// Entity → 稳定 guid 字符串（EntityRef 字段编码用）；无效 / 无 guid → ""。
std::string EntityGuidString(entt::registry& reg, Orange::Engine::Entity ent)
{
    if (!ent.IsValid()) { return ""; }
    const auto e = Orange::Engine::World::ToEntt(ent);
    if (!reg.valid(e)) { return ""; }
    const auto* g = reg.try_get<Orange::Engine::Scene::GuidComponent>(e);
    return (g != nullptr && g->guid.IsValid()) ? g->guid.ToString() : "";
}

// 把单个 component 字段值按 PropertyType 编进 writer 的 `path`（schema 驱动，
// 零逐组件手写）。EntityRef → 目标 guid 字符串；AssetRef → 资源路径；两类顶点
// 表 → {count, vertices:[[x,y]...], isLoop?}。caller 已保证 pd 可读（get 或
// assetRefGet 非空）。
void EncodeField(JsonWriter& w, const std::string& path, const Schema::PropertyDescriptor& pd,
                 const void* comp, const EditorAssetContext& assets, entt::registry& reg)
{
    namespace Physics = Orange::Engine::Physics;
    using PT          = Schema::PropertyType;
    switch (pd.type)
    {
        case PT::Float: { float v = 0.0f; pd.get(comp, &v); w.WriteFloat(path, v); break; }
        case PT::Int:   { int v = 0; pd.get(comp, &v); w.WriteInt(path, v); break; }
        case PT::UInt:  { unsigned v = 0; pd.get(comp, &v); w.WriteInt(path, static_cast<std::int64_t>(v)); break; }
        case PT::Bool:  { bool v = false; pd.get(comp, &v); w.WriteBool(path, v); break; }
        case PT::Vec2:  { glm::vec2 v{0.0f}; pd.get(comp, &v); const float a[2]{v.x, v.y}; w.WriteFloatArray(path, a, 2); break; }
        case PT::Vec3:  { glm::vec3 v{0.0f}; pd.get(comp, &v); const float a[3]{v.x, v.y, v.z}; w.WriteFloatArray(path, a, 3); break; }
        case PT::Vec4:  { glm::vec4 v{0.0f}; pd.get(comp, &v); const float a[4]{v.x, v.y, v.z, v.w}; w.WriteFloatArray(path, a, 4); break; }
        case PT::Quat:  { glm::quat v{1.0f, 0.0f, 0.0f, 0.0f}; pd.get(comp, &v);
                          const float a[4]{v.w, v.x, v.y, v.z}; w.WriteFloatArray(path, a, 4); break; }
        case PT::String:{ std::string v; pd.get(comp, &v); w.WriteString(path, v); break; }
        case PT::Enum:  { int v = 0; pd.get(comp, &v);
                          if (pd.attribs.enumNames != nullptr && v >= 0 && v < pd.attribs.enumCount)
                              w.WriteString(path, pd.attribs.enumNames[v]);
                          else
                              w.WriteInt(path, v);
                          break; }
        case PT::EntityRef: { Orange::Engine::Entity v; pd.get(comp, &v); w.WriteString(path, EntityGuidString(reg, v)); break; }
        case PT::AssetRef:  { std::string v; if (pd.assetRefGet != nullptr) pd.assetRefGet(comp, assets, &v); w.WriteString(path, v); break; }
        case PT::AssetRefArray: { std::vector<std::string> v; if (pd.assetRefGet != nullptr) pd.assetRefGet(comp, assets, &v);
                          w.BeginArray(path, v.size());
                          for (std::size_t i = 0; i < v.size(); ++i) w.WriteString(path + "/" + std::to_string(i), v[i]);
                          break; }
        case PT::PolygonVertices: { Physics::PolygonDesc d; pd.get(comp, &d);
                          const auto n = std::min<std::uint32_t>(d.count, Physics::PolygonDesc::kMaxVertices);
                          w.WriteInt(path + "/count", d.count);
                          w.BeginArray(path + "/vertices", n);
                          for (std::uint32_t i = 0; i < n; ++i) { const float a[2]{d.vertices[i].x, d.vertices[i].y};
                              w.WriteFloatArray(path + "/vertices/" + std::to_string(i), a, 2); }
                          break; }
        case PT::EdgeChainVertices: { Physics::EdgeChainDesc d; pd.get(comp, &d);
                          const auto n = std::min<std::uint32_t>(d.count, Physics::EdgeChainDesc::kMaxVertices);
                          w.WriteInt(path + "/count", d.count);
                          w.WriteBool(path + "/isLoop", d.isLoop);
                          w.BeginArray(path + "/vertices", n);
                          for (std::uint32_t i = 0; i < n; ++i) { const float a[2]{d.vertices[i].x, d.vertices[i].y};
                              w.WriteFloatArray(path + "/vertices/" + std::to_string(i), a, 2); }
                          break; }
    }
}

// ---- op: ping --------------------------------------------------------------
// {"result":{editorVersion, protocolVersion, sceneName}}
std::string HandlePing(std::int64_t id, EditorHost& host)
{
    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteString("result/editorVersion", kEditorVersion);
    w.WriteString("result/protocolVersion", kProtocolVersion);
    w.WriteString("result/sceneName", SceneDisplayName(host.scene.currentScenePath));
    return DumpLine(w);
}

// ---- op: get_scene_info ----------------------------------------------------
// 遍历 World 产实体树：每实体 {guid, name, parentGuid, components[], position[3]}。
// EnsureEntityGuids 先行（幂等给无 guid 实体补 guid），保证所有实体可被 MCP 寻址。
std::string HandleGetSceneInfo(std::int64_t id, EditorHost& host)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Entity;
    using Orange::Engine::World;

    World* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }

    // 幂等补 guid —— 主线程帧末执行，安全 mutate World（ADR-020 invariant ①）。
    Scene::EnsureEntityGuids(*pWorld);

    auto& reg = pWorld->Registry();

    // 预扫一遍建 entity→guid 字符串映射（解析 parentGuid 用）。
    std::unordered_map<std::uint32_t, std::string> guidByEntity;
    for (auto e : reg.view<entt::entity>())
    {
        const auto* g = reg.try_get<Scene::GuidComponent>(e);
        if (g != nullptr && g->guid.IsValid())
        {
            guidByEntity.emplace(static_cast<std::uint32_t>(entt::to_integral(e)),
                                 g->guid.ToString());
        }
    }

    // 单实体记录（先收集，后写 JSON，避免遍历中频繁 reallocate JSON 树）。
    struct Record
    {
        std::string              guid;
        std::string              name;
        std::string              parentGuid;
        std::vector<std::string> components;
        bool                     hasTransform = false;
        float                    position[3]{0.0f, 0.0f, 0.0f};
    };
    std::vector<Record> records;

    const auto& schemas = Schema::ComponentSchemaRegistry::Instance().All();

    bool truncated = false;
    for (auto e : reg.view<entt::entity>())
    {
        if (records.size() >= kMaxSceneEntities) { truncated = true; break; }
        const Entity entity = World::FromEntt(e);

        Record rec;
        const auto entId = static_cast<std::uint32_t>(entt::to_integral(e));
        if (auto it = guidByEntity.find(entId); it != guidByEntity.end())
        {
            rec.guid = it->second;
        }

        if (const auto* nc = reg.try_get<Scene::NameComponent>(e); nc != nullptr)
        {
            rec.name = nc->name;
        }

        if (const auto* hc = reg.try_get<Scene::HierarchyComponent>(e);
            hc != nullptr && hc->parent.IsValid())
        {
            const auto pid = static_cast<std::uint32_t>(hc->parent.Value());
            if (auto it = guidByEntity.find(pid); it != guidByEntity.end())
            {
                rec.parentGuid = it->second;
            }
        }

        // 内联 Transform.position（Q-e：AI 空间推理几乎每次都要，省一轮 get_entity）。
        if (const auto* tc = reg.try_get<Scene::TransformComponent>(e); tc != nullptr)
        {
            rec.hasTransform = true;
            rec.position[0]  = tc->position.x;
            rec.position[1]  = tc->position.y;
            rec.position[2]  = tc->position.z;
        }

        // schema 驱动枚举组件名（has==true 的 schema typeName）——新增组件自动出现。
        for (const auto& sc : schemas)
        {
            if (sc.has != nullptr && sc.typeName != nullptr && sc.has(*pWorld, entity))
            {
                rec.components.emplace_back(sc.typeName);
            }
        }

        records.push_back(std::move(rec));
    }

    // ---- 编 JSON 响应 ----
    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteString("result/sceneName", SceneDisplayName(host.scene.currentScenePath));
    w.WriteInt("result/entityCount", static_cast<std::int64_t>(records.size()));
    w.WriteBool("result/truncated", truncated);
    w.BeginArray("result/entities", records.size());
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        const Record&     r    = records[i];
        const std::string base = "result/entities/" + std::to_string(i);
        w.WriteString(base + "/guid", r.guid);
        w.WriteString(base + "/name", r.name);
        w.WriteString(base + "/parentGuid", r.parentGuid);
        w.BeginArray(base + "/components", r.components.size());
        for (std::size_t j = 0; j < r.components.size(); ++j)
        {
            w.WriteString(base + "/components/" + std::to_string(j), r.components[j]);
        }
        if (r.hasTransform)
        {
            w.WriteFloatArray(base + "/position", r.position, 3);
        }
    }
    return DumpLine(w);
}

// ---- op: get_entity --------------------------------------------------------
// 单实体全字段（ADR-020 Q3 = schema 逐字段，结构化 {component:{field:value}}）。
// args.guid 定位实体；遍历 has==true 的 schema 逐字段 EncodeField。
std::string HandleGetEntity(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;
    using Orange::Engine::World;

    World* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }

    std::string guidStr;
    if (!req.ReadString("args/guid", guidStr) || guidStr.empty())
    {
        return MakeError(id, "missing 'guid'");
    }
    Guid guid;
    if (!Guid::FromString(guidStr, guid))
    {
        return MakeError(id, "invalid guid format");
    }

    // EntityRef 字段要回报目标 guid → 先幂等补全所有实体 guid。
    Scene::EnsureEntityGuids(*pWorld);
    const Entity entity = Scene::FindEntityByGuid(*pWorld, guid);
    if (!entity.IsValid()) { return MakeError(id, "entity not found"); }

    auto& reg = pWorld->Registry();

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteString("result/guid", guidStr);
    if (const auto* nc = reg.try_get<Scene::NameComponent>(World::ToEntt(entity)); nc != nullptr)
        w.WriteString("result/name", nc->name);
    else
        w.WriteString("result/name", "");

    const auto& schemas = Schema::ComponentSchemaRegistry::Instance().All();

    // componentTypes：has==true 的全部组件名（保证 AI 看到完整组件集，即便某组件
    // 无可读字段——components 对象里可能缺该键）。
    std::vector<const char*> presentTypes;
    for (const auto& sc : schemas)
        if (sc.has != nullptr && sc.typeName != nullptr && sc.has(*pWorld, entity))
            presentTypes.push_back(sc.typeName);
    w.BeginArray("result/componentTypes", presentTypes.size());
    for (std::size_t i = 0; i < presentTypes.size(); ++i)
        w.WriteString("result/componentTypes/" + std::to_string(i), presentTypes[i]);

    // components 对象：每个 has 组件逐字段编码。
    for (const auto& sc : schemas)
    {
        if (sc.has == nullptr || sc.typeName == nullptr || !sc.has(*pWorld, entity)) { continue; }
        void* comp = (sc.get != nullptr) ? sc.get(*pWorld, entity) : nullptr;
        if (comp == nullptr) { continue; }
        const std::string cbase = std::string("result/components/") + sc.typeName;
        for (const auto& pd : sc.properties)
        {
            if (pd.name == nullptr) { continue; }
            // Group header / 不可读字段（get 与 assetRefGet 都空）跳过。
            if (pd.get == nullptr && pd.assetRefGet == nullptr) { continue; }
            EncodeField(w, cbase + "/" + pd.name, pd, comp, host.assets, reg);
        }
    }
    return DumpLine(w);
}

// ---- op: list_component_types ----------------------------------------------
// 枚举 schema 注册表：每组件 {typeName, displayName, addable, removable, fields[]}；
// 每字段 {name, label, type, readable, range?, enumNames?, assetKind?}。让 AI 自
// 发现能力面——新增组件 schema 后本 tool 零代码改动即列出（NF-10 验收信号）。
std::string HandleListComponentTypes(std::int64_t id)
{
    const auto& schemas = Schema::ComponentSchemaRegistry::Instance().All();

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.BeginArray("result/componentTypes", schemas.size());

    std::size_t ci = 0;
    for (const auto& sc : schemas)
    {
        const std::string base = "result/componentTypes/" + std::to_string(ci);
        w.WriteString(base + "/typeName", sc.typeName != nullptr ? sc.typeName : "");
        w.WriteString(base + "/displayName", sc.displayName != nullptr ? sc.displayName : "");
        w.WriteBool(base + "/addable", sc.add != nullptr);
        w.WriteBool(base + "/removable", sc.remove != nullptr);

        // 只列有 name 的字段（跳过 Group header 占位 PD 里没 name 的——实际 Group
        // 也带 name 占位，但其 get/set 为空，用 readable 标出）。
        std::vector<const Schema::PropertyDescriptor*> fields;
        for (const auto& pd : sc.properties)
            if (pd.name != nullptr) { fields.push_back(&pd); }
        w.BeginArray(base + "/fields", fields.size());
        for (std::size_t fi = 0; fi < fields.size(); ++fi)
        {
            const auto&       pd = *fields[fi];
            const std::string fb = base + "/fields/" + std::to_string(fi);
            w.WriteString(fb + "/name", pd.name);
            w.WriteString(fb + "/label", pd.label != nullptr ? pd.label : "");
            w.WriteString(fb + "/type", PropertyTypeName(pd.type));
            w.WriteBool(fb + "/readable", pd.get != nullptr || pd.assetRefGet != nullptr);
            if (pd.attribs.hasRange)
            {
                w.WriteFloat(fb + "/min", pd.attribs.minValue);
                w.WriteFloat(fb + "/max", pd.attribs.maxValue);
            }
            if (pd.attribs.enumNames != nullptr && pd.attribs.enumCount > 0)
            {
                w.BeginArray(fb + "/enumNames", static_cast<std::size_t>(pd.attribs.enumCount));
                for (int k = 0; k < pd.attribs.enumCount; ++k)
                    w.WriteString(fb + "/enumNames/" + std::to_string(k), pd.attribs.enumNames[k]);
            }
            if (pd.type == Schema::PropertyType::AssetRef ||
                pd.type == Schema::PropertyType::AssetRefArray)
            {
                w.WriteString(fb + "/assetKind", AssetKindName(pd.attribs.assetKind));
            }
        }
        ++ci;
    }
    return DumpLine(w);
}

// ---- op: capture_viewport --------------------------------------------------
// 把当前 viewport 离屏渲染结果（用户屏幕所见，含后处理）回读 → base64。
// Python 侧解码 → PNG → MCP image content。让 AI「看见」场景（ADR-020 M1 核心）。
std::string HandleCaptureViewport(std::int64_t id, Orange::Engine::Render::Pipeline* pipeline)
{
    if (pipeline == nullptr) { return MakeError(id, "viewport pipeline not ready"); }

    std::vector<std::uint8_t> bgra;
    std::uint32_t             w = 0, h = 0;
    if (!pipeline->CaptureViewportToCpu(bgra, w, h))
    {
        return MakeError(id, "viewport capture failed (not in offscreen mode / not yet rendered)");
    }
    if (w == 0 || h == 0 || w > kMaxCaptureDim || h > kMaxCaptureDim
        || bgra.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u)
    {
        return MakeError(id, "invalid capture dimensions");
    }

    JsonWriter w2;
    w2.WriteInt("id", id);
    w2.WriteBool("ok", true);
    w2.WriteString("result/format", "BGRA8");
    w2.WriteInt("result/width", static_cast<std::int64_t>(w));
    w2.WriteInt("result/height", static_cast<std::int64_t>(h));
    w2.WriteString("result/base64", Base64Encode(bgra.data(), bgra.size()));
    return DumpLine(w2);
}

}  // namespace

std::string ExecuteMcpCommand(const std::string&                requestJson,
                              EditorHost&                       host,
                              Orange::Engine::Render::Pipeline* viewportPipeline)
{
    std::int64_t id = 0;
    try
    {
        auto parsed = JsonReader::FromString(requestJson);
        if (parsed.IsErr())
        {
            return MakeError(0, "malformed json");
        }
        const JsonReader& r = parsed.Value();
        id                  = r.GetInt("id", 0);
        std::string op;
        if (!r.ReadString("op", op))
        {
            return MakeError(id, "missing 'op'");
        }

        if (op == "ping") { return HandlePing(id, host); }
        if (op == "get_scene_info") { return HandleGetSceneInfo(id, host); }
        if (op == "get_entity") { return HandleGetEntity(id, host, r); }
        if (op == "list_component_types") { return HandleListComponentTypes(id); }
        if (op == "capture_viewport") { return HandleCaptureViewport(id, viewportPipeline); }

        return MakeError(id, "unknown op: " + op);
    }
    catch (const std::exception& ex)
    {
        return MakeError(id, std::string("exception: ") + ex.what());
    }
    catch (...)
    {
        return MakeError(id, "unknown exception");
    }
}

}  // namespace Orange::Editor::Mcp
