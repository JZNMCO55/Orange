#include "McpCommandHandler.h"

#include "../EditorCameraControl.h"
#include "../EditorHost.h"
#include "../EditorHierarchy.h"
#include "../EditorPrefabActions.h"
#include "../MaterialFileIO.h"
#include "../PrefabOverrideUI.h"
#include "../command/EntityCommands.h"
#include "../command/LambdaCommand.h"
#include "../command/PrefabCommands.h"
#include "../command/SetAnimationClipCommand.h"
#include "../command/SetFieldValueCommand.h"
#include "../import/ImportDispatcher.h"
#include "../schema/ComponentSchemaRegistry.h"
#include "../schema/SchemaInspector.h"

#include "../schema/AssetRefSideEffects.h"  // PrepareAssetRefWrite / FinishAssetRefWrite（镜像 GUI 副作用）
#include "../schema/PropertyDescriptor.h"

#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/animation/AnimationClipSerialization.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/core/Guid.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/PrefabInstanceComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/script/ScriptComponent.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
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

// PlayState / gizmo Mode / Space → 稳定字符串名（get_editor_state 用）。
const char* PlayStateName(PlayState s)
{
    switch (s)
    {
        case PlayState::Edit:   return "Edit";
        case PlayState::Play:   return "Play";
        case PlayState::Paused: return "Paused";
    }
    return "Edit";
}

const char* GizmoModeName(EditorGizmoState::Mode m)
{
    switch (m)
    {
        case EditorGizmoState::Mode::Translate: return "Translate";
        case EditorGizmoState::Mode::Rotate:    return "Rotate";
        case EditorGizmoState::Mode::Scale:     return "Scale";
    }
    return "Translate";
}

const char* GizmoSpaceName(EditorGizmoState::Space sp)
{
    switch (sp)
    {
        case EditorGizmoState::Space::World: return "World";
        case EditorGizmoState::Space::Local: return "Local";
    }
    return "World";
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

// ---- op: get_editor_state --------------------------------------------------
// 轻量编辑器状态快照：playState（Edit/Play/Paused）、当前场景路径 + 名 + dirty、
// 当前选中实体 guid + 选中数、gizmo 模式 / 参考系 / 可见开关。AI 每次操作前据此
// 知道"现在能不能写（Play 期默认拒）、选中了谁、场景脏没脏"，省一轮试错。
std::string HandleGetEditorState(std::int64_t id, EditorHost& host)
{
    namespace Scene = Orange::Engine::Scene;

    // 选中实体的 guid —— 有选中且有 World 时先幂等补 guid 再读。
    std::string selectedGuid;
    if (host.selection.selectedEntity.IsValid())
    {
        if (auto* pWorld = host.scene.pWorld.get(); pWorld != nullptr)
        {
            Scene::EnsureEntityGuids(*pWorld);
            selectedGuid = EntityGuidString(pWorld->Registry(), host.selection.selectedEntity);
        }
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteString("result/playState", PlayStateName(host.scene.playState));
    w.WriteString("result/scenePath", host.scene.currentScenePath);
    w.WriteString("result/sceneName", SceneDisplayName(host.scene.currentScenePath));
    w.WriteBool("result/dirty", host.scene.dirty);
    w.WriteString("result/selectedGuid", selectedGuid);
    w.WriteInt("result/selectedCount", static_cast<std::int64_t>(host.selection.SelectedCount()));
    w.WriteString("result/gizmoMode", GizmoModeName(host.gizmo.mode));
    w.WriteString("result/gizmoSpace", GizmoSpaceName(host.gizmo.space));
    w.WriteBool("result/gizmoVisible", host.gizmo.visible);
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

// ===========================================================================
// M2 写闭环 —— 全部走命令栈（除 delete/select/save，见各注）；ADR-020 invariant ②。
// ===========================================================================

// 按 typeName 线性查 schema（registry 只有 type_index 索引，MCP 用字符串名寻址）。
const Schema::ComponentSchema* FindSchemaByName(const std::string& typeName)
{
    for (const auto& sc : Schema::ComponentSchemaRegistry::Instance().All())
        if (sc.typeName != nullptr && typeName == sc.typeName) { return &sc; }
    return nullptr;
}

// Play 期写保护（ADR-020 Q-b）：Play 模式下写操作默认拒绝（改动会被 Stop 还原，
// AI/用户易误以为持久），除非 args.allowInPlay==true 逃生门。返回 true = 应拒绝。
bool IsPlayBlocked(EditorHost& host, const JsonReader& req)
{
    if (host.scene.playState != PlayState::Play) { return false; }
    return !req.GetBool("args/allowInPlay", false);
}

// 通用标量/向量字段写命令：读旧值 + 构造 SetFieldValueCommand<T> + Push（自动
// Execute 应用新值）。apply 闭包按 guid 时刻捕获的 Entity + schema.get 重解组件
// （对齐 SchemaInspector MakeFieldApply，World 切换走 nullptr 早退）。
template <typename T>
void PushFieldSet(EditorHost& host, Orange::Engine::Entity e,
                  const Schema::ComponentSchema& sc, const Schema::PropertyDescriptor& pd,
                  const std::string& fieldKey, const T& newVal)
{
    auto* pWorld = host.scene.pWorld.get();
    T     oldVal{};
    if (pWorld != nullptr)
    {
        if (void* comp = sc.get(*pWorld, e); comp != nullptr) { pd.get(comp, &oldVal); }
    }
    auto apply = [pHost = &host, e, pSchema = &sc, setFn = pd.set](const T& v) {
        auto* w = pHost->scene.pWorld.get();
        if (w == nullptr || pSchema->get == nullptr || setFn == nullptr) { return; }
        if (void* c = pSchema->get(*w, e); c != nullptr) { setFn(c, &v); }
    };
    host.cmdStack.Push(std::make_unique<SetFieldValueCommand<T>>(
        e, fieldKey, oldVal, newVal, std::move(apply)));
}

// AssetRef 字段写命令变体：setter 多带 const EditorAssetContext&。
void PushAssetRefSet(EditorHost& host, Orange::Engine::Entity e,
                     const Schema::ComponentSchema& sc, const Schema::PropertyDescriptor& pd,
                     const std::string& fieldKey, const std::string& newVal)
{
    auto* pWorld = host.scene.pWorld.get();
    std::string oldVal;
    if (pWorld != nullptr && pd.assetRefGet != nullptr)
    {
        if (void* comp = sc.get(*pWorld, e); comp != nullptr) { pd.assetRefGet(comp, host.assets, &oldVal); }
    }
    auto apply = [pHost = &host, e, pSchema = &sc, setFn = pd.assetRefSet](const std::string& v) {
        auto* w = pHost->scene.pWorld.get();
        if (w == nullptr || pSchema->get == nullptr || setFn == nullptr) { return; }
        if (void* c = pSchema->get(*w, e); c != nullptr) { setFn(c, pHost->assets, &v); }
    };
    host.cmdStack.Push(std::make_unique<SetFieldValueCommand<std::string>>(
        e, fieldKey, oldVal, newVal, std::move(apply)));
}

float ClampRange(float v, const Schema::PropertyAttributes& a)
{
    if (!a.hasRange) { return v; }
    return std::min(std::max(v, a.minValue), a.maxValue);
}

// ---- op: set_field ---------------------------------------------------------
// 改任意组件任意字段（schema get/set → SetFieldValueCommand<T>，与 Inspector 同
// 路径，可 Undo + coalesce）。args: guid, component, field, value[, allowInPlay]。
std::string HandleSetField(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;
    using PT = Schema::PropertyType;

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }
    if (IsPlayBlocked(host, req))
    {
        return MakeError(id, "in play mode (pass allowInPlay:true to override; changes revert on stop)");
    }

    std::string guidStr, compName, fieldName;
    if (!req.ReadString("args/guid", guidStr) || guidStr.empty()) { return MakeError(id, "missing 'guid'"); }
    if (!req.ReadString("args/component", compName)) { return MakeError(id, "missing 'component'"); }
    if (!req.ReadString("args/field", fieldName)) { return MakeError(id, "missing 'field'"); }
    if (!req.Has("args/value")) { return MakeError(id, "missing 'value'"); }

    Guid guid;
    if (!Guid::FromString(guidStr, guid)) { return MakeError(id, "invalid guid format"); }
    Scene::EnsureEntityGuids(*pWorld);
    const Entity e = Scene::FindEntityByGuid(*pWorld, guid);
    if (!e.IsValid()) { return MakeError(id, "entity not found"); }

    const Schema::ComponentSchema* sc = FindSchemaByName(compName);
    if (sc == nullptr) { return MakeError(id, "unknown component: " + compName); }
    if (sc->has == nullptr || !sc->has(*pWorld, e)) { return MakeError(id, "component not on entity: " + compName); }

    const Schema::PropertyDescriptor* pd = nullptr;
    for (const auto& p : sc->properties)
        if (p.name != nullptr && fieldName == p.name) { pd = &p; break; }
    if (pd == nullptr) { return MakeError(id, "unknown field: " + fieldName); }

    const std::string fieldKey = compName + "." + fieldName;
    const char*       vp       = "args/value";

    switch (pd->type)
    {
        case PT::Float: { double d; if (!req.ReadFloat(vp, d)) return MakeError(id, "value must be a number");
                          PushFieldSet<float>(host, e, *sc, *pd, fieldKey, ClampRange(static_cast<float>(d), pd->attribs)); break; }
        case PT::Int:   { std::int64_t i; if (!req.ReadInt(vp, i)) return MakeError(id, "value must be an integer");
                          if (pd->attribs.hasRange) i = static_cast<std::int64_t>(ClampRange(static_cast<float>(i), pd->attribs));
                          PushFieldSet<int>(host, e, *sc, *pd, fieldKey, static_cast<int>(i)); break; }
        case PT::UInt:  { std::int64_t i; if (!req.ReadInt(vp, i)) return MakeError(id, "value must be an integer");
                          if (i < 0) i = 0;
                          if (pd->attribs.hasRange) i = static_cast<std::int64_t>(ClampRange(static_cast<float>(i), pd->attribs));
                          PushFieldSet<unsigned>(host, e, *sc, *pd, fieldKey, static_cast<unsigned>(i)); break; }
        case PT::Bool:  { bool b; if (!req.ReadBool(vp, b)) return MakeError(id, "value must be a bool");
                          PushFieldSet<bool>(host, e, *sc, *pd, fieldKey, b); break; }
        case PT::Vec2:  { float a[2]; if (!req.ReadFloatArray(vp, a, 2)) return MakeError(id, "value must be a 2-number array");
                          PushFieldSet<glm::vec2>(host, e, *sc, *pd, fieldKey, glm::vec2(a[0], a[1])); break; }
        case PT::Vec3:  { float a[3]; if (!req.ReadFloatArray(vp, a, 3)) return MakeError(id, "value must be a 3-number array");
                          PushFieldSet<glm::vec3>(host, e, *sc, *pd, fieldKey, glm::vec3(a[0], a[1], a[2])); break; }
        case PT::Vec4:  { float a[4]; if (!req.ReadFloatArray(vp, a, 4)) return MakeError(id, "value must be a 4-number array");
                          PushFieldSet<glm::vec4>(host, e, *sc, *pd, fieldKey, glm::vec4(a[0], a[1], a[2], a[3])); break; }
        case PT::Quat:  { float a[4]; if (!req.ReadFloatArray(vp, a, 4)) return MakeError(id, "value must be a 4-number array [w,x,y,z]");
                          PushFieldSet<glm::quat>(host, e, *sc, *pd, fieldKey, glm::quat(a[0], a[1], a[2], a[3])); break; }
        case PT::String:{ std::string s; if (!req.ReadString(vp, s)) return MakeError(id, "value must be a string");
                          PushFieldSet<std::string>(host, e, *sc, *pd, fieldKey, s); break; }
        case PT::Enum:  { int idx = -1; std::string name;
                          if (req.ReadString(vp, name))
                          {
                              if (pd->attribs.enumNames != nullptr)
                                  for (int k = 0; k < pd->attribs.enumCount; ++k)
                                      if (name == pd->attribs.enumNames[k]) { idx = k; break; }
                              if (idx < 0) return MakeError(id, "invalid enum name: " + name);
                          }
                          else { std::int64_t i; if (!req.ReadInt(vp, i)) return MakeError(id, "value must be enum name or int");
                                 idx = static_cast<int>(i); }
                          if (idx < 0 || (pd->attribs.enumCount > 0 && idx >= pd->attribs.enumCount))
                              return MakeError(id, "enum value out of range");
                          PushFieldSet<int>(host, e, *sc, *pd, fieldKey, idx); break; }
        case PT::AssetRef: { std::string s; if (!req.ReadString(vp, s)) return MakeError(id, "value must be an asset path string");
                          if (pd->assetRefSet == nullptr) return MakeError(id, "asset field not writable");
                          // 镜像 GUI writePath（SchemaInspector）：Material 写前 lazy 注册 instance，
                          // 否则 materialSet 查表 miss 静默 no-op（F1：经 MCP 设 materialInstance
                          // 不视觉生效，但仍返回 ok:true）。ensure 失败显式报错而非静默 ok。
                          if (!Orange::Editor::PrepareAssetRefWrite(host, *pd, s))
                              return MakeError(id, "failed to ensure material instance for: " + s);
                          PushAssetRefSet(host, e, *sc, *pd, fieldKey, s);
                          Orange::Editor::FinishAssetRefWrite(host, e, *pd, s);  // Mesh 写后同步多材质 slot
                          break; }
        default:
            return MakeError(id, "field type not writable in first version (EntityRef / AssetRefArray / vertex tables are read-only)");
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", true);
    w.WriteString("result/component", compName);
    w.WriteString("result/field", fieldName);
    return DumpLine(w);
}

// ---- op: create_entity -----------------------------------------------------
// 建空实体（Name+Transform[+Hierarchy 若指定父]），走 CreateEntityCommand（可 Undo）。
// 返回新实体 guid。args: name?, parentGuid?, allowInPlay?
std::string HandleCreateEntity(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;
    using Orange::Engine::World;

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }
    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }

    const std::string name = req.GetString("args/name", "New Entity");

    Entity parent = Entity::Invalid();
    std::string parentGuidStr;
    if (req.ReadString("args/parentGuid", parentGuidStr) && !parentGuidStr.empty())
    {
        Guid pg;
        if (!Guid::FromString(parentGuidStr, pg)) { return MakeError(id, "invalid parentGuid format"); }
        Scene::EnsureEntityGuids(*pWorld);
        parent = Scene::FindEntityByGuid(*pWorld, pg);
        if (!parent.IsValid()) { return MakeError(id, "parent entity not found"); }
    }

    auto cmd = std::make_unique<CreateEntityCommand>(
        host,
        [name, parent](World& w) -> Entity {
            Entity e = w.CreateEntity();
            w.AddComponent<Scene::NameComponent>(e, Scene::NameComponent{name});
            w.AddComponent<Scene::TransformComponent>(e, Scene::TransformComponent{});
            if (parent.IsValid() && w.IsValid(parent)) { EditorHierarchy::LinkAsLastChild(w, parent, e); }
            return e;
        });
    auto* raw = cmd.get();
    host.cmdStack.Push(std::move(cmd));
    const Entity created = raw->CreatedEntity();
    if (!created.IsValid()) { return MakeError(id, "create failed"); }

    // 给新实体补 guid 后回报。
    Scene::EnsureEntityGuids(*pWorld);
    std::string newGuid;
    if (const auto* g = pWorld->Registry().try_get<Scene::GuidComponent>(World::ToEntt(created));
        g != nullptr && g->guid.IsValid())
    {
        newGuid = g->guid.ToString();
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", true);
    w.WriteString("result/guid", newGuid);
    return DumpLine(w);
}

// ---- op: add_component -----------------------------------------------------
// 挂组件（schema.add）。⚠️ 现状编辑器 Add Component 走 schema.add + cmdStack.Clear()
//（无 AddComponentCommand），故**不可 Undo** —— 如实返回 undoable:false（NF-3）。
// args: guid, component, allowInPlay?
std::string HandleAddComponent(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }
    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }

    std::string guidStr, compName;
    if (!req.ReadString("args/guid", guidStr) || guidStr.empty()) { return MakeError(id, "missing 'guid'"); }
    if (!req.ReadString("args/component", compName)) { return MakeError(id, "missing 'component'"); }

    Guid guid;
    if (!Guid::FromString(guidStr, guid)) { return MakeError(id, "invalid guid format"); }
    Scene::EnsureEntityGuids(*pWorld);
    const Entity e = Scene::FindEntityByGuid(*pWorld, guid);
    if (!e.IsValid()) { return MakeError(id, "entity not found"); }

    const Schema::ComponentSchema* sc = FindSchemaByName(compName);
    if (sc == nullptr) { return MakeError(id, "unknown component: " + compName); }
    if (sc->add == nullptr) { return MakeError(id, "component not addable: " + compName); }
    if (sc->has != nullptr && sc->has(*pWorld, e)) { return MakeError(id, "component already present: " + compName); }

    sc->add(host, e);
    host.cmdStack.Clear();  // 镜像 InspectorPanel：Add Component 清栈（不可 Undo）

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);  // 现状清栈，不可 undo（NF-3 显式声明）
    return DumpLine(w);
}

// ---- op: delete_entity -----------------------------------------------------
// 删实体及子树。设 pendingDelete，下一帧 EntityTreePanel 消费——该路径现已**可
// Undo**（SaveSubtreeToString + do=DestroySubtree / undo=LoadFromString 精确复位，
// 不再 Clear 栈），故 undoable:true（ADR-020 §4.D 预期的"删除命令化后契约升级"，
// 协议字段不变纯行为增强）。极端兜底：子树序列化失败时该帧退化为不可 undo 的直接
// 销毁——属罕见边界，契约按常态声明。args: guid, allowInPlay?
std::string HandleDeleteEntity(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }
    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }

    std::string guidStr;
    if (!req.ReadString("args/guid", guidStr) || guidStr.empty()) { return MakeError(id, "missing 'guid'"); }
    Guid guid;
    if (!Guid::FromString(guidStr, guid)) { return MakeError(id, "invalid guid format"); }
    Scene::EnsureEntityGuids(*pWorld);
    const Entity e = Scene::FindEntityByGuid(*pWorld, guid);
    if (!e.IsValid()) { return MakeError(id, "entity not found"); }

    // 帧末设 pendingDelete；下一帧 EntityTreePanel 消费（SaveSubtreeToString +
    // do=DestroySubtree / undo=LoadFromString 复位，可 Undo）。
    host.selection.pendingDelete = e;

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", true);  // 删除已命令化，用户可 Ctrl+Z 撤销
    return DumpLine(w);
}

// ---- op: select_entity -----------------------------------------------------
// 设编辑器选中（让用户看到 AI 在操作谁，Inspector 联动）。guid 空 = 清空选中。
// 非命令栈（UI 状态）。args: guid
std::string HandleSelectEntity(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }

    std::string guidStr;
    req.ReadString("args/guid", guidStr);
    if (guidStr.empty())
    {
        host.selection.selectedEntity = Entity::Invalid();
        host.selection.additionalSelectedEntities.clear();
    }
    else
    {
        Guid guid;
        if (!Guid::FromString(guidStr, guid)) { return MakeError(id, "invalid guid format"); }
        Scene::EnsureEntityGuids(*pWorld);
        const Entity e = Scene::FindEntityByGuid(*pWorld, guid);
        if (!e.IsValid()) { return MakeError(id, "entity not found"); }
        host.selection.selectedEntity = e;
        host.selection.additionalSelectedEntities.clear();
        host.assets.selectedAssetPath.clear();  // 把 Inspector 焦点拉回实体模式
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    return DumpLine(w);
}

// ---- op: save_scene --------------------------------------------------------
// 保存场景（pendingSceneOp=Save；path 非空先设 currentScenePath 走 SaveAs 语义）。
// 非命令栈（文件 IO）。实际写盘在下一帧 ApplyPendingSceneOp，故 ok 表示"已排队"。
// args: path?
std::string HandleSaveScene(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    std::string path;
    if (req.ReadString("args/path", path) && !path.empty())
    {
        host.scene.currentScenePath = path;
    }
    host.scene.pendingSceneOp = SceneOp::Save;

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);
    w.WriteBool("result/queued", true);  // 写盘在下一帧执行
    w.WriteString("result/path", host.scene.currentScenePath);
    return DumpLine(w);
}

// ===========================================================================
// P1 工具 —— E1 协同效率包 / E2 资产管线 / E3 Play 调试。
// ===========================================================================

// ASCII 小写化（无 locale 依赖；find_entities 大小写不敏感匹配 + list_assets ext）。
std::string AsciiLower(std::string s)
{
    for (auto& c : s) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); } }
    return s;
}

// 按 guid 字符串定位实体（先幂等补 guid）。成功返回有效 Entity；失败置 err 返回
// Invalid（caller 直接 MakeError(id, err)）。P1 写类 / 相机类 tool 复用。
Orange::Engine::Entity ResolveEntityByGuid(EditorHost& host, const std::string& guidStr,
                                           std::string& err)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { err = "no active world"; return Entity::Invalid(); }
    if (guidStr.empty())   { err = "missing 'guid'"; return Entity::Invalid(); }
    Guid guid;
    if (!Guid::FromString(guidStr, guid)) { err = "invalid guid format"; return Entity::Invalid(); }
    Scene::EnsureEntityGuids(*pWorld);
    const Entity e = Scene::FindEntityByGuid(*pWorld, guid);
    if (!e.IsValid()) { err = "entity not found"; return Entity::Invalid(); }
    return e;
}

// ---- op: find_entities -----------------------------------------------------
// 按条件过滤实体（全可选、多条件 AND）：name 子串（大小写不敏感）/ 含某组件
// （schema typeName）/ underGuid 子树范围（含该实体自身）。返回 {guid, name} 数组。
// 无命中 → 空数组非 error（NF）。args: name?, component?, underGuid?
std::string HandleFindEntities(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;
    using Orange::Engine::World;

    World* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }
    Scene::EnsureEntityGuids(*pWorld);
    auto& reg = pWorld->Registry();

    std::string nameSub, compName, underGuidStr;
    req.ReadString("args/name", nameSub);
    req.ReadString("args/component", compName);
    req.ReadString("args/underGuid", underGuidStr);
    const std::string nameSubLower = AsciiLower(nameSub);

    const Schema::ComponentSchema* compSchema = nullptr;
    if (!compName.empty())
    {
        compSchema = FindSchemaByName(compName);
        if (compSchema == nullptr) { return MakeError(id, "unknown component: " + compName); }
    }

    Entity under = Entity::Invalid();
    if (!underGuidStr.empty())
    {
        Guid ug;
        if (!Guid::FromString(underGuidStr, ug)) { return MakeError(id, "invalid underGuid format"); }
        under = Scene::FindEntityByGuid(*pWorld, ug);
        if (!under.IsValid()) { return MakeError(id, "underGuid entity not found"); }
    }

    struct Hit { std::string guid; std::string name; };
    std::vector<Hit> hits;
    bool truncated = false;
    for (auto e : reg.view<entt::entity>())
    {
        if (hits.size() >= kMaxSceneEntities) { truncated = true; break; }
        const Entity entity = World::FromEntt(e);

        std::string nm;
        if (const auto* nc = reg.try_get<Scene::NameComponent>(e); nc != nullptr) { nm = nc->name; }
        if (!nameSubLower.empty() && AsciiLower(nm).find(nameSubLower) == std::string::npos) { continue; }
        if (compSchema != nullptr &&
            (compSchema->has == nullptr || !compSchema->has(*pWorld, entity))) { continue; }
        // under 是 entity 的祖先（含自身）== entity 在 under 的子树内。
        if (under.IsValid() && !EditorHierarchy::IsAncestorOf(*pWorld, under, entity)) { continue; }

        hits.push_back(Hit{EntityGuidString(reg, entity), nm});
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteInt("result/count", static_cast<std::int64_t>(hits.size()));
    w.WriteBool("result/truncated", truncated);
    w.BeginArray("result/entities", hits.size());
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        const std::string base = "result/entities/" + std::to_string(i);
        w.WriteString(base + "/guid", hits[i].guid);
        w.WriteString(base + "/name", hits[i].name);
    }
    return DumpLine(w);
}

// 把当前编辑器相机参数写进 writer 的 result（get_camera / set_camera / frame_entity
// 共用，AI 拿到生效后参数）。
void WriteCameraResult(JsonWriter& w, const EditorCameraState& ec)
{
    const float pivot[3]{ec.pivot.x, ec.pivot.y, ec.pivot.z};
    w.WriteFloatArray("result/pivot", pivot, 3);
    w.WriteFloat("result/azimuth", ec.azimuth);
    w.WriteFloat("result/elevation", ec.elevation);
    w.WriteFloat("result/radius", ec.radius);
    w.WriteFloat("result/fovYDegrees", ec.fovYDegrees);
    w.WriteFloat("result/zNear", ec.zNear);
    w.WriteFloat("result/zFar", ec.zFar);
}

// ---- op: get_camera --------------------------------------------------------
// 读编辑器轨道相机（pivot/azimuth/elevation/radius/fov/near/far）。非命令栈。
std::string HandleGetCamera(std::int64_t id, EditorHost& host)
{
    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    WriteCameraResult(w, host.camera);
    return DumpLine(w);
}

// ---- op: set_camera --------------------------------------------------------
// 写编辑器轨道相机（AI 自主构图截图，亦覆盖 Standard Views）。各字段可选；非法值
// clamp 到合法域。非命令栈（相机非场景数据，与手动转相机一致不产 undo）。
// args: pivot?[3], azimuth?, elevation?, radius?, fovYDegrees?, zNear?, zFar?
std::string HandleSetCamera(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    auto& ec = host.camera;
    float p[3];
    if (req.ReadFloatArray("args/pivot", p, 3)) { ec.pivot = glm::vec3(p[0], p[1], p[2]); }
    double d;
    if (req.ReadFloat("args/azimuth", d))   { ec.azimuth = static_cast<float>(d); }
    if (req.ReadFloat("args/elevation", d))
    {
        // clamp 到 ±~89°（1.55334 rad）避免轨道翻面（gimbal flip，相机越过天/地顶）。
        ec.elevation = std::min(std::max(static_cast<float>(d), -1.55334f), 1.55334f);
    }
    if (req.ReadFloat("args/radius", d))      { ec.radius = std::max(static_cast<float>(d), 0.01f); }
    if (req.ReadFloat("args/fovYDegrees", d)) { ec.fovYDegrees = std::min(std::max(static_cast<float>(d), 1.0f), 179.0f); }
    if (req.ReadFloat("args/zNear", d))       { ec.zNear = std::max(static_cast<float>(d), 1e-4f); }
    if (req.ReadFloat("args/zFar", d))        { ec.zFar = std::max(static_cast<float>(d), ec.zNear + 1e-3f); }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    WriteCameraResult(w, ec);
    return DumpLine(w);
}

// ---- op: frame_entity ------------------------------------------------------
// 相机对准实体（复用 FrameEntityCamera；不传 guid = Frame All）。非命令栈。
// args: guid?
std::string HandleFrameEntity(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    using Orange::Engine::Entity;
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return MakeError(id, "no active world"); }

    std::string guidStr;
    req.ReadString("args/guid", guidStr);

    bool ok = false;
    if (guidStr.empty())
    {
        ok = FrameAllCamera(host);
    }
    else
    {
        std::string err;
        const Entity e = ResolveEntityByGuid(host, guidStr, err);
        if (!e.IsValid()) { return MakeError(id, err); }
        ok = FrameEntityCamera(host, e);
    }
    if (!ok) { return MakeError(id, "frame failed (empty scene / entity lacks transform)"); }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    WriteCameraResult(w, host.camera);
    return DumpLine(w);
}

// ---- op: duplicate_entity --------------------------------------------------
// 复制子树（复用 Duplicate 路径：SaveSubtreeToString → LoadFromString +
// SeparateClonedIdentities 换新身份 + reparent 到原父）。可 Undo。返回克隆根 guid。
// args: guid, allowInPlay?
std::string HandleDuplicateEntity(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Entity;
    using HC = Scene::HierarchyComponent;

    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    std::string guidStr;
    req.ReadString("args/guid", guidStr);
    std::string err;
    const Entity root = ResolveEntityByGuid(host, guidStr, err);
    if (!root.IsValid()) { return MakeError(id, err); }

    auto* pW = host.scene.pWorld.get();
    Scene::SaveOptions saveOpts;
    saveOpts.assetRegistry          = host.assets.pAssets.get();
    saveOpts.namedMaterialInstances = &host.assets.namedMaterialInstances;
    saveOpts.extraSerializers       = host.extraSerializers;
    const std::vector<Entity> dupRoots{root};
    auto blobRes = Scene::SaveSubtreeToString(*pW, dupRoots, saveOpts);
    if (blobRes.IsErr()) { return MakeError(id, "subtree serialize failed"); }

    const auto* rh = pW->GetComponent<HC>(root);
    const Entity origParent = (rh != nullptr) ? rh->parent : Entity::Invalid();
    const std::string blob = blobRes.Value();
    auto* pH = &host;
    auto createdPtr = std::make_shared<std::vector<Entity>>();
    // 与 EntityTreePanel 帧末 Duplicate（Ctrl+D）完全同款 LambdaCommand。
    host.cmdStack.Push(std::make_unique<LambdaCommand>(
        "duplicate",
        [pH, blob, origParent, createdPtr]() {
            auto* w = pH->scene.pWorld.get();
            if (w == nullptr) { return; }
            Scene::LoadOptions lo;
            lo.assetRegistry          = pH->assets.pAssets.get();
            lo.animatorRegistry       = pH->assets.pAnimators.get();
            lo.namedMaterialInstances = &pH->assets.namedMaterialInstances;
            lo.extraSerializers       = pH->extraSerializers;
            std::vector<Entity> created;
            if (Scene::LoadFromString(blob, *w, lo, &created).IsErr()) { return; }
            *createdPtr = created;
            Scene::SeparateClonedIdentities(*w, created);
            for (const auto ce : created) {
                const auto* eh = w->GetComponent<HC>(ce);
                if (eh == nullptr || !eh->parent.IsValid()) {
                    if (origParent.IsValid() && w->IsValid(origParent)) {
                        EditorHierarchy::ReparentTo(*w, ce, origParent);
                    }
                    pH->selection.selectedEntity = ce;
                    pH->selection.ClearAdditional();
                    pH->assets.selectedAssetPath.clear();
                    break;
                }
            }
        },
        [pH, createdPtr]() {
            auto* w = pH->scene.pWorld.get();
            if (w == nullptr) { return; }
            for (const auto ce : *createdPtr) {
                if (!w->IsValid(ce)) { continue; }
                const auto* eh = w->GetComponent<HC>(ce);
                const bool isRoot = (eh == nullptr) || !eh->parent.IsValid()
                    || std::find(createdPtr->begin(), createdPtr->end(), eh->parent) == createdPtr->end();
                if (isRoot) { EditorHierarchy::DestroySubtree(*w, ce); }
            }
        }));

    // Push 已执行 do-lambda → createdPtr 已填、SeparateClonedIdentities 已换新
    // guid。克隆根 = 父不在 created 集内者（reparent 后父=origParent 在集外，或无父）。
    Entity cloneRoot = Entity::Invalid();
    for (const auto ce : *createdPtr)
    {
        if (!pW->IsValid(ce)) { continue; }
        const auto* eh = pW->GetComponent<HC>(ce);
        const bool isRoot = (eh == nullptr) || !eh->parent.IsValid()
            || std::find(createdPtr->begin(), createdPtr->end(), eh->parent) == createdPtr->end();
        if (isRoot) { cloneRoot = ce; break; }
    }
    std::string newGuid;
    if (cloneRoot.IsValid())
    {
        Scene::EnsureEntityGuids(*pW);
        newGuid = EntityGuidString(pW->Registry(), cloneRoot);
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", true);
    w.WriteString("result/guid", newGuid);
    return DumpLine(w);
}

// ---- op: reparent_entity ---------------------------------------------------
// 改父（复用 EditorHierarchy keep-world 变体，世界位姿保持）。可 Undo（捕获旧
// parent + prevSibling 精确复位）。环检测：把祖先挂到后代下 → error。
// args: guid, newParentGuid?(空=提为根), keepWorld?=true, allowInPlay?
std::string HandleReparentEntity(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;
    using HC = Scene::HierarchyComponent;

    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    auto* pW = host.scene.pWorld.get();
    if (pW == nullptr) { return MakeError(id, "no active world"); }

    std::string guidStr;
    req.ReadString("args/guid", guidStr);
    std::string err;
    const Entity child = ResolveEntityByGuid(host, guidStr, err);
    if (!child.IsValid()) { return MakeError(id, err); }

    Entity newParent = Entity::Invalid();
    std::string npStr;
    if (req.ReadString("args/newParentGuid", npStr) && !npStr.empty())
    {
        Guid np;
        if (!Guid::FromString(npStr, np)) { return MakeError(id, "invalid newParentGuid format"); }
        newParent = Scene::FindEntityByGuid(*pW, np);
        if (!newParent.IsValid()) { return MakeError(id, "new parent entity not found"); }
    }
    const bool keepWorld = req.GetBool("args/keepWorld", true);

    // 防环：child 是 newParent 的祖先（IsAncestorOf 含自身）→ 拒绝（含 self-parent）。
    if (newParent.IsValid() && EditorHierarchy::IsAncestorOf(*pW, child, newParent))
    {
        return MakeError(id, "cycle: cannot reparent under itself or its own descendant");
    }

    const auto* hc = pW->GetComponent<HC>(child);
    const Entity oldParent = (hc != nullptr) ? hc->parent : Entity::Invalid();
    const Entity oldPrev   = (hc != nullptr) ? hc->prevSibling : Entity::Invalid();

    auto* pH = &host;
    // 与 EntityTreePanel DnD reparent 同款 LambdaCommand（keep-world 自逆）。
    host.cmdStack.Push(std::make_unique<LambdaCommand>(
        "reparent",
        [pH, child, newParent, keepWorld]() {
            auto* w = pH->scene.pWorld.get();
            if (w == nullptr || !w->IsValid(child)) { return; }
            if (keepWorld) { EditorHierarchy::ReparentToKeepWorld(*w, child, newParent); }
            else           { EditorHierarchy::ReparentTo(*w, child, newParent); }
        },
        [pH, child, oldParent, oldPrev, keepWorld]() {
            auto* w = pH->scene.pWorld.get();
            if (w == nullptr || !w->IsValid(child)) { return; }
            if (keepWorld) { EditorHierarchy::MoveToPositionKeepWorld(*w, child, oldParent, oldPrev); }
            else           { EditorHierarchy::MoveToPosition(*w, child, oldParent, oldPrev); }
        }));

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", true);
    return DumpLine(w);
}

// ---- op: remove_component --------------------------------------------------
// 卸组件（schema.remove）。复用 Inspector Remove 的状态快照还原路径：有 add+get
// 的组件 → 可 Undo（CaptureComponentValues + LambdaCommand，undo 重建并还原原值）；
// 否则（Name/Hierarchy/Animator 等无 add）→ remove + Clear 不可 undo。
// args: guid, component, allowInPlay?
std::string HandleRemoveComponent(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    using Orange::Engine::Entity;

    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    auto* pW = host.scene.pWorld.get();
    if (pW == nullptr) { return MakeError(id, "no active world"); }

    std::string guidStr, compName;
    req.ReadString("args/guid", guidStr);
    if (!req.ReadString("args/component", compName)) { return MakeError(id, "missing 'component'"); }
    std::string err;
    const Entity e = ResolveEntityByGuid(host, guidStr, err);
    if (!e.IsValid()) { return MakeError(id, err); }

    const Schema::ComponentSchema* sc = FindSchemaByName(compName);
    if (sc == nullptr) { return MakeError(id, "unknown component: " + compName); }
    if (sc->remove == nullptr) { return MakeError(id, "component not removable: " + compName); }
    if (sc->has == nullptr || !sc->has(*pW, e)) { return MakeError(id, "component not on entity: " + compName); }

    bool undoable = false;
    void* comp = (sc->get != nullptr) ? sc->get(*pW, e) : nullptr;
    if (sc->add != nullptr && comp != nullptr)
    {
        // 可 Undo：移除前快照字段 → undo 时 schema.add 重建 + restorer 还原原值。
        auto restorers = Orange::Editor::Schema::CaptureComponentValues(host, *sc, comp);
        auto* pH = &host;
        host.cmdStack.Push(std::make_unique<LambdaCommand>(
            "remove_component",
            [pH, e, sc]() {
                if (pH->scene.pWorld && sc->remove) { sc->remove(*pH->scene.pWorld, e); }
            },
            [pH, e, sc, restorers]() {
                if (sc->add) { sc->add(*pH, e); }
                if (pH->scene.pWorld && sc->get) {
                    if (void* c = sc->get(*pH->scene.pWorld, e); c != nullptr) {
                        for (const auto& r : restorers) { r(c); }
                    }
                }
            }));
        undoable = true;
    }
    else
    {
        // 无 add（不可重建）→ 破坏性 remove + 清栈（同 Inspector）。
        sc->remove(*pW, e);
        host.cmdStack.Clear();
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", undoable);
    return DumpLine(w);
}

// ---- op: begin_undo_group --------------------------------------------------
// 开命令组：之后的写操作合并为单条 undo 记录（CommandStack::BeginGroup）。开组态
// 由 mcp 桥记录（含开组时刻 + 客户端代），TickMcpUndoGroupGuard 据此做 30s 超时 /
// 断连自动闭合护栏。嵌套开组 → error。args: label?（仅信息性，组名固定）
std::string HandleBeginUndoGroup(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    if (host.cmdStack.InGroup()) { return MakeError(id, "undo group already open"); }
    // CommandStack 组名要求静态生命周期（不复制），故用固定字面量；label 入参仅
    // 信息性（未来若需自定义 undo 菜单标签再扩）。
    (void)req;
    host.cmdStack.BeginGroup("MCP Batch", MergeMode::Disable);
    host.mcp.undoGroupOpen      = true;
    host.mcp.undoGroupClientGen = host.mcp.clientGeneration.load();
    host.mcp.undoGroupStart     = std::chrono::steady_clock::now();

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoGroupOpen", true);
    return DumpLine(w);
}

// ---- op: end_undo_group ----------------------------------------------------
// 合组（CommandStack::EndGroup）：组内命令打包成单条 undo 记录。无打开的组 → error。
std::string HandleEndUndoGroup(std::int64_t id, EditorHost& host)
{
    if (!host.cmdStack.InGroup())
    {
        host.mcp.undoGroupOpen = false;  // 会话态可能因场景切换 Clear 残留，顺手清。
        return MakeError(id, "no undo group open");
    }
    host.cmdStack.EndGroup();
    host.mcp.undoGroupOpen = false;

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoGroupOpen", false);
    return DumpLine(w);
}

// ---- op: open_scene --------------------------------------------------------
// 打开 .scene.json（复用 requestedOpenScenePath 桥接 = 双击打开 / Open Recent 同
// 路径）。非命令栈（场景切换会 Clear 栈）。dirty 保护：dirty 且未传 force → error
// （防默默丢用户未保存工作）；force=true 时清 dirty 抑制确认模态（丢弃改动）。
// 仅 Edit 态可开。实际 swap World 在下一帧帧首消费。args: path, force?=false
std::string HandleOpenScene(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace fs = std::filesystem;
    std::string path;
    if (!req.ReadString("args/path", path) || path.empty()) { return MakeError(id, "missing 'path'"); }
    if (host.scene.playState != PlayState::Edit) { return MakeError(id, "cannot open scene while in play mode"); }

    std::error_code ec;
    if (!fs::exists(path, ec)) { return MakeError(id, "scene file not found: " + path); }

    const bool force = req.GetBool("args/force", false);
    if (host.scene.dirty && !force)
    {
        return MakeError(id, "unsaved changes (pass force:true to discard, or save_scene first)");
    }
    if (force)
    {
        // 丢弃未保存改动 → 抑制帧首 requestedOpenScenePath 的 unsaved-confirm 模态
        //（模态会阻塞等用户，不适合自动化）。
        host.scene.dirty = false;
    }
    host.scene.requestedOpenScenePath = path;

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);
    w.WriteBool("result/queued", true);  // 下一帧帧首 swap World
    w.WriteString("result/path", path);
    return DumpLine(w);
}

// ---- op: play / pause / resume / stop --------------------------------------
// 设 pendingPlayOp，帧末 ApplyPendingPlayOp 执行状态迁移（World 快照 / 物理 / VFX /
// 动画 tick 启停）。非命令栈（模式切换；快照机制保证 Stop 还原）。状态不符 → error。
std::string HandleSetPlayOp(std::int64_t id, EditorHost& host, const std::string& opName)
{
    const PlayState st = host.scene.playState;
    PlayOp op = PlayOp::None;
    if (opName == "play")
    {
        if (st != PlayState::Edit) { return MakeError(id, "already in play/paused"); }
        op = PlayOp::EnterPlay;
    }
    else if (opName == "pause")
    {
        if (st != PlayState::Play) { return MakeError(id, "not in play (pause requires Play)"); }
        op = PlayOp::Pause;
    }
    else if (opName == "resume")
    {
        if (st != PlayState::Paused) { return MakeError(id, "not paused (resume requires Paused)"); }
        op = PlayOp::Resume;
    }
    else if (opName == "stop")
    {
        if (st == PlayState::Edit) { return MakeError(id, "already in edit"); }
        op = PlayOp::Stop;
    }
    else
    {
        return MakeError(id, "unknown play op: " + opName);
    }
    host.scene.pendingPlayOp = op;

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/queued", true);  // 帧末执行
    w.WriteString("result/requestedOp", opName);
    return DumpLine(w);
}

// 扩展名 / 文件名 → AssetKind 名（与编辑器 Asset Browser AssetCategoryOf 一致）。
// 非资产文件（.meta 等）返回空串。
const char* AssetKindByPath(const std::filesystem::path& p)
{
    const std::string ext  = AsciiLower(p.extension().string());
    const std::string name = p.filename().string();
    if (name.size() >= 11 && name.compare(name.size() - 11, 11, ".scene.json") == 0) { return "Scene"; }
    if (ext == ".mesh" || ext == ".obj") { return "Mesh"; }
    if (ext == ".material") { return "Material"; }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".ktx"
        || ext == ".hdr" || ext == ".exr" || ext == ".texture") { return "Texture"; }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") { return "Sound"; }
    if (ext == ".anim") { return "AnimationClip"; }
    return "";
}

// ---- op: list_assets -------------------------------------------------------
// 枚举 assets/ 下资产（递归扫文件系统），按 AssetKind 过滤 + pathPrefix 子串过滤。
// 返回 {path（forward-slash 相对路径，可直接喂 AssetRef 字段）, kind} 数组。
// 非命令栈。大目录上限 + truncated 标记。args: kind?, pathPrefix?
std::string HandleListAssets(std::int64_t id, const JsonReader& req)
{
    namespace fs = std::filesystem;
    std::string kindFilter, prefix;
    req.ReadString("args/kind", kindFilter);
    req.ReadString("args/pathPrefix", prefix);
    for (auto& c : prefix) { if (c == '\\') { c = '/'; } }

    constexpr std::size_t kMaxAssets = 4000;  // 大目录护栏（防爆 token / 爆遍历）
    struct A { std::string path; std::string kind; };
    std::vector<A> assets;
    bool truncated = false;

    const std::string root = "assets";
    std::error_code ec;
    if (fs::exists(root, ec) && fs::is_directory(root, ec))
    {
        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) { break; }
            if (!it->is_regular_file(ec)) { continue; }
            const char* kind = AssetKindByPath(it->path());
            if (kind[0] == '\0') { continue; }                  // 非资产文件跳过
            if (!kindFilter.empty() && kindFilter != kind) { continue; }
            std::string rel = it->path().generic_string();      // forward slashes
            if (!prefix.empty() && rel.find(prefix) == std::string::npos) { continue; }
            assets.push_back(A{std::move(rel), kind});
            if (assets.size() >= kMaxAssets) { truncated = true; break; }
        }
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteInt("result/count", static_cast<std::int64_t>(assets.size()));
    w.WriteBool("result/truncated", truncated);
    w.BeginArray("result/assets", assets.size());
    for (std::size_t i = 0; i < assets.size(); ++i)
    {
        const std::string base = "result/assets/" + std::to_string(i);
        w.WriteString(base + "/path", assets[i].path);
        w.WriteString(base + "/kind", assets[i].kind);
    }
    return DumpLine(w);
}

// ---- op: import_asset ------------------------------------------------------
// 导入外部资产（复用 ImportDispatcher，与拖拽 / File→Import 同路径）。同步执行
// （主线程帧末，与 ApplyPendingImports 同上下文）→ 直接拿产物路径回报。非命令栈
// （资产文件 IO）。args: srcPath, scale?（FBX importScale，默认 1.0）
std::string HandleImportAsset(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace fs = std::filesystem;
    std::string src;
    if (!req.ReadString("args/srcPath", src) || src.empty()) { return MakeError(id, "missing 'srcPath'"); }
    std::error_code ec;
    if (!fs::exists(src, ec)) { return MakeError(id, "source file not found: " + src); }

    double scaleD = 1.0;
    req.ReadFloat("args/scale", scaleD);

    const auto result =
        ::Orange::Editor::Import::Dispatch(src, host, static_cast<float>(scaleD));
    if (result.status != ::Orange::Editor::Import::ImportStatus::Success)
    {
        return MakeError(id, "import failed: " + result.message);
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);
    w.WriteString("result/destPath", result.destPath);
    w.WriteString("result/message", result.message);
    w.BeginArray("result/materialPaths", result.materialPaths.size());
    for (std::size_t i = 0; i < result.materialPaths.size(); ++i)
    {
        w.WriteString("result/materialPaths/" + std::to_string(i), result.materialPaths[i]);
    }
    return DumpLine(w);
}

// ===========================================================================
// P2 工具 —— E5 prefab / E6 动画 / E7 脚本 / E8 杂项（可观测 / 材质 / 场景）。
// 各域数据层均已落地，MCP 层只消费、不重写。
// ===========================================================================

// 取实体的 ClipAnimator（AnimatorComponent.animator dynamic_cast）。任一环空→nullptr。
Orange::Engine::Animation::ClipAnimator* GetClipAnimator(EditorHost& host,
                                                         Orange::Engine::Entity e)
{
    namespace Anim = Orange::Engine::Animation;
    auto* pW = host.scene.pWorld.get();
    if (pW == nullptr || !pW->IsValid(e)) { return nullptr; }
    auto* ac = pW->GetComponent<Anim::AnimatorComponent>(e);
    if (ac == nullptr || !ac->animator) { return nullptr; }
    return dynamic_cast<Anim::ClipAnimator*>(ac->animator.get());
}

// ---- op: set_entity_order --------------------------------------------------
// 根级 sibling 重排（HierarchyComponent.sortIndex，复用 EditorHierarchy::
// MoveRootRelative，对应右键 Move Up/Down）。可 Undo。非根实体 / 边界 → error。
// args: guid, direction("up"|"down"), allowInPlay?
std::string HandleSetEntityOrder(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    using Orange::Engine::Entity;
    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    auto* pW = host.scene.pWorld.get();
    if (pW == nullptr) { return MakeError(id, "no active world"); }

    std::string guidStr, dir;
    req.ReadString("args/guid", guidStr);
    if (!req.ReadString("args/direction", dir)) { return MakeError(id, "missing 'direction' (up|down)"); }
    int delta = 0;
    if (dir == "up")        { delta = -1; }
    else if (dir == "down") { delta = +1; }
    else { return MakeError(id, "direction must be 'up' or 'down'"); }

    std::string err;
    const Entity e = ResolveEntityByGuid(host, guidStr, err);
    if (!e.IsValid()) { return MakeError(id, err); }

    // dryRun 预检（非根 / 单根 / 已在边界 → 不能移动），避免"判断执行一次 + 命令
    // Execute 再执行一次"移两位（BUG-2026-06-01-root-reorder-double-apply）。
    if (!EditorHierarchy::MoveRootRelative(*pW, e, delta, /*dryRun*/ true))
    {
        return MakeError(id, "cannot move (not a root entity / single root / already at boundary)");
    }
    auto* pH = &host;
    host.cmdStack.Push(std::make_unique<LambdaCommand>(
        "MoveRoot",
        [pH, e, delta]() {
            if (auto* w = pH->scene.pWorld.get(); w != nullptr && w->IsValid(e))
                EditorHierarchy::MoveRootRelative(*w, e, delta);
        },
        [pH, e, delta]() {
            if (auto* w = pH->scene.pWorld.get(); w != nullptr && w->IsValid(e))
                EditorHierarchy::MoveRootRelative(*w, e, -delta);
        }));

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", true);
    return DumpLine(w);
}

// ---- op: new_scene ---------------------------------------------------------
// 新建空场景（pendingSceneOp=New，下一帧 ApplyPendingSceneOp swap 空 World）。
// 非命令栈（场景切换 Clear 栈）。dirty 保护同 open_scene。仅 Edit 态。
// args: force?=false
std::string HandleNewScene(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    if (host.scene.playState != PlayState::Edit) { return MakeError(id, "cannot new scene while in play mode"); }
    const bool force = req.GetBool("args/force", false);
    if (host.scene.dirty && !force)
    {
        return MakeError(id, "unsaved changes (pass force:true to discard, or save_scene first)");
    }
    // New 分支无条件 swap 空 World（不读 dirty），故 force 时无需额外抑制模态。
    host.scene.pendingSceneOp = SceneOp::New;

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);
    w.WriteBool("result/queued", true);
    return DumpLine(w);
}

// ---- op: create_prefab -----------------------------------------------------
// 子树落盘 .prefab.json（复用 Prefab::CommitNewPrefabFile）。非命令栈（文件 IO）。
// prefabName 取 path basename（去 .prefab.json）。args: rootGuid, path
std::string HandleCreatePrefab(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    using Orange::Engine::Entity;
    std::string rootGuidStr, path;
    req.ReadString("args/rootGuid", rootGuidStr);
    if (!req.ReadString("args/path", path) || path.empty()) { return MakeError(id, "missing 'path'"); }
    std::string err;
    const Entity root = ResolveEntityByGuid(host, rootGuidStr, err);
    if (!root.IsValid()) { return MakeError(id, err); }

    // prefabName = 文件名去目录去 .prefab.json / .json 后缀。
    std::string name = path;
    if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos) { name = name.substr(slash + 1); }
    for (const char* suf : {".prefab.json", ".json"})
    {
        const std::size_t sl = std::char_traits<char>::length(suf);
        if (name.size() >= sl && name.compare(name.size() - sl, sl, suf) == 0)
        {
            name = name.substr(0, name.size() - sl);
            break;
        }
    }

    if (!Orange::Editor::Prefab::CommitNewPrefabFile(host, root, path, name))
    {
        return MakeError(id, "create prefab failed (see editor log)");
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);
    w.WriteString("result/path", path);
    w.WriteString("result/prefabName", name);
    return DumpLine(w);
}

// ---- op: instantiate_prefab ------------------------------------------------
// 实例化 .prefab.json（复用 InstantiatePrefabCommand，可 Undo），返回新根 guid。
// parentGuid 给定时实例化后 reparent 到该父（keep-world，注：undo 销毁整树、redo
// 重建为根不重放 reparent——MVP 限制）。args: path, parentGuid?, allowInPlay?
std::string HandleInstantiatePrefab(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    namespace Asset = Orange::Engine::Asset;
    using Orange::Engine::Core::Guid;
    using Orange::Engine::Entity;

    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    auto* pW = host.scene.pWorld.get();
    if (pW == nullptr) { return MakeError(id, "no active world"); }
    auto* pReg = host.assets.pAssets.get();
    if (pReg == nullptr) { return MakeError(id, "AssetRegistry not ready"); }

    std::string path;
    if (!req.ReadString("args/path", path) || path.empty()) { return MakeError(id, "missing 'path'"); }

    Entity parent = Entity::Invalid();
    std::string parentGuidStr;
    if (req.ReadString("args/parentGuid", parentGuidStr) && !parentGuidStr.empty())
    {
        Guid pg;
        if (!Guid::FromString(parentGuidStr, pg)) { return MakeError(id, "invalid parentGuid format"); }
        Scene::EnsureEntityGuids(*pW);
        parent = Scene::FindEntityByGuid(*pW, pg);
        if (!parent.IsValid()) { return MakeError(id, "parent entity not found"); }
    }

    auto loaded = pReg->Load<Asset::PrefabAsset>(path);
    if (loaded.IsErr()) { return MakeError(id, "load prefab failed: " + path); }

    auto cmd = std::make_unique<InstantiatePrefabCommand>(host, loaded.Value());
    auto* raw = cmd.get();
    host.cmdStack.Push(std::move(cmd));  // Push 自动 Execute → InstanceRoot 有效
    const Entity root = raw->InstanceRoot();
    if (!root.IsValid()) { return MakeError(id, "instantiate failed"); }

    // parentGuid 给定 → keep-world reparent（MVP：plain op，非命令；undo 整树销毁仍正确）。
    if (parent.IsValid() && pW->IsValid(parent))
    {
        EditorHierarchy::ReparentToKeepWorld(*pW, root, parent);
    }

    Scene::EnsureEntityGuids(*pW);
    const std::string newGuid = EntityGuidString(pW->Registry(), root);

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", true);
    w.WriteString("result/guid", newGuid);
    return DumpLine(w);
}

// ---- op: get_prefab_status -------------------------------------------------
// prefab 实例的 override 状态：模板路径 + overriddenPaths 持久化集 + 锚定信息。
// 非 prefab 实例 → error。非命令栈。args: guid
std::string HandleGetPrefabStatus(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Scene = Orange::Engine::Scene;
    using Orange::Engine::Entity;
    using Orange::Engine::World;

    std::string guidStr;
    req.ReadString("args/guid", guidStr);
    std::string err;
    const Entity e = ResolveEntityByGuid(host, guidStr, err);
    if (!e.IsValid()) { return MakeError(id, err); }

    if (!Orange::Editor::Prefab::IsPrefabInstance(host, e))
    {
        return MakeError(id, "not a prefab instance");
    }
    auto* pW = host.scene.pWorld.get();
    const auto* pi = pW->GetComponent<Scene::PrefabInstanceComponent>(e);
    if (pi == nullptr) { return MakeError(id, "no PrefabInstanceComponent"); }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/isPrefabInstance", true);
    w.WriteString("result/templatePath", pi->sourcePrefabPath);
    w.WriteBool("result/isInstanceRoot", pi->isInstanceRoot);
    w.WriteInt("result/overrideCount", static_cast<std::int64_t>(pi->overriddenPaths.size()));
    w.BeginArray("result/overriddenPaths", pi->overriddenPaths.size());
    for (std::size_t i = 0; i < pi->overriddenPaths.size(); ++i)
    {
        w.WriteString("result/overriddenPaths/" + std::to_string(i), pi->overriddenPaths[i]);
    }
    return DumpLine(w);
}

// ---- op: revert_override ---------------------------------------------------
// 单字段（component+field 都给）或全部（都不给）回退到模板值（复用 PrefabOverrideUI
// RevertField / RevertAllFields）。⚠️ 引擎层无 typed by-path 逆写原语，**不可 Undo**。
// args: guid, component?, field?
std::string HandleRevertOverride(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    using Orange::Engine::Entity;
    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    std::string guidStr, comp, field;
    req.ReadString("args/guid", guidStr);
    req.ReadString("args/component", comp);
    req.ReadString("args/field", field);
    std::string err;
    const Entity e = ResolveEntityByGuid(host, guidStr, err);
    if (!e.IsValid()) { return MakeError(id, err); }

    if (!Orange::Editor::Prefab::IsPrefabInstance(host, e))
    {
        return MakeError(id, "not a prefab instance");
    }

    bool ok = false;
    if (!comp.empty() && !field.empty())
    {
        ok = Orange::Editor::Prefab::RevertField(host, e, comp, field);
    }
    else
    {
        ok = Orange::Editor::Prefab::RevertAllFields(host, e);
    }
    if (!ok) { return MakeError(id, "revert no-op (field not overridden / no template / no overrides)"); }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);  // 缺 typed 逆写原语，不可 undo（显式声明）
    return DumpLine(w);
}

// ---- op: apply_instance ----------------------------------------------------
// 把实例当前态推回模板（复用 ApplyInstanceToPrefab：重写 .prefab.json + reload +
// 失效缩略图）。⚠️ 资产层 IO，**不可 Undo**。args: guid
std::string HandleApplyInstance(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    using Orange::Engine::Entity;
    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    std::string guidStr;
    req.ReadString("args/guid", guidStr);
    std::string err;
    const Entity e = ResolveEntityByGuid(host, guidStr, err);
    if (!e.IsValid()) { return MakeError(id, err); }

    if (!Orange::Editor::Prefab::IsPrefabInstance(host, e))
    {
        return MakeError(id, "not a prefab instance");
    }
    if (!Orange::Editor::Prefab::ApplyInstanceToPrefab(host, e))
    {
        return MakeError(id, "apply failed (no template / write failed, see editor log)");
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);  // 资产文件 IO，不可 undo（显式声明）
    return DumpLine(w);
}

// ---- op: get_animation_clip ------------------------------------------------
// 读实体 ClipAnimator 当前 clip（或 .anim 文件路径）→ AnimationClipToJson。
// 非命令栈。args: guid 或 path（二选一，guid 优先）
std::string HandleGetAnimationClip(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Anim = Orange::Engine::Animation;
    std::string guidStr, path;
    req.ReadString("args/guid", guidStr);
    req.ReadString("args/path", path);

    std::string clipJson;
    if (!guidStr.empty())
    {
        std::string err;
        const auto e = ResolveEntityByGuid(host, guidStr, err);
        if (!e.IsValid()) { return MakeError(id, err); }
        auto* clip = GetClipAnimator(host, e);
        if (clip == nullptr) { return MakeError(id, "entity has no ClipAnimator"); }
        clipJson = Anim::AnimationClipToJson(clip->Clip(), -1);
    }
    else if (!path.empty())
    {
        auto loaded = Anim::LoadAnimationClip(path);
        if (loaded.IsErr()) { return MakeError(id, "load .anim failed: " + path); }
        clipJson = Anim::AnimationClipToJson(loaded.Value(), -1);
    }
    else
    {
        return MakeError(id, "missing 'guid' or 'path'");
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    // clip 以紧凑 JSON 字符串回（Python 侧 json.loads）。schema = animation/Clip。
    w.WriteString("result/clipJson", clipJson);
    return DumpLine(w);
}

// ---- op: set_animation_clip ------------------------------------------------
// 整 clip 写回实体 ClipAnimator（AnimationClipFromJson + SetAnimationClipCommand，
// 与 timeline GUI 同命令，可 Undo）。clip JSON 校验失败 → error 不落。
// args: guid, clipJson(string), allowInPlay?
std::string HandleSetAnimationClip(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Anim = Orange::Engine::Animation;
    using Orange::Engine::Entity;

    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    std::string guidStr, clipJson;
    req.ReadString("args/guid", guidStr);
    if (!req.ReadString("args/clipJson", clipJson) || clipJson.empty())
    {
        return MakeError(id, "missing 'clipJson'");
    }
    std::string err;
    const Entity e = ResolveEntityByGuid(host, guidStr, err);
    if (!e.IsValid()) { return MakeError(id, err); }

    auto* clip = GetClipAnimator(host, e);
    if (clip == nullptr) { return MakeError(id, "entity has no ClipAnimator"); }

    auto parsed = Anim::AnimationClipFromJson(clipJson);
    if (parsed.IsErr()) { return MakeError(id, "invalid clip JSON (schema/key order/interp)"); }

    Anim::AnimationClip oldClip = clip->Clip();
    Anim::AnimationClip newClip = parsed.Value();
    host.cmdStack.Push(std::make_unique<SetAnimationClipCommand>(
        host, e, std::move(oldClip), std::move(newClip),
        std::string("mcp_set_clip"), std::string("Set Animation Clip")));

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", true);
    return DumpLine(w);
}

// ---- op: preview_animation -------------------------------------------------
// 编辑期预览（EditorAnimationPreviewState：play/pause/seek 单 animator tick；与
// PlayState::Play 互斥）。非命令栈（预览态不改场景数据）。
// args: guid, action(play|pause|seek), time?
std::string HandlePreviewAnimation(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    using Orange::Engine::Entity;
    if (host.scene.playState == PlayState::Play || host.scene.playState == PlayState::Paused)
    {
        return MakeError(id, "in play mode (edit-time preview is mutually exclusive with Play)");
    }
    std::string guidStr, action;
    req.ReadString("args/guid", guidStr);
    if (!req.ReadString("args/action", action)) { return MakeError(id, "missing 'action' (play|pause|seek)"); }
    std::string err;
    const Entity e = ResolveEntityByGuid(host, guidStr, err);
    if (!e.IsValid()) { return MakeError(id, err); }
    auto* clip = GetClipAnimator(host, e);
    if (clip == nullptr) { return MakeError(id, "entity has no ClipAnimator"); }

    if (action == "play")
    {
        host.animPreview.previewEntity  = e;
        host.animPreview.previewPlaying = true;
    }
    else if (action == "pause")
    {
        host.animPreview.previewPlaying = false;
    }
    else if (action == "seek")
    {
        double t = 0.0;
        if (!req.ReadFloat("args/time", t)) { return MakeError(id, "seek requires 'time' (seconds)"); }
        host.animPreview.previewEntity  = e;
        host.animPreview.previewPlaying = false;
        clip->Seek(static_cast<float>(t));
    }
    else
    {
        return MakeError(id, "action must be play|pause|seek");
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteString("result/action", action);
    w.WriteBool("result/previewPlaying", host.animPreview.previewPlaying);
    return DumpLine(w);
}

// ---- op: set_script_field --------------------------------------------------
// 改 ScriptComponent.fieldOverrides 的某条（B1.3 authored tweakable）。找不到该
// name 则追加一条。⚠️ 直接 mutate（镜像 ScriptFieldOverridesInspectorPlugin），
// **不可 Undo**。value 统一字符串持久化；type 可显式给（Float/Int/Bool/String），
// 否则按 value JSON 形态推断。args: guid, fieldName, value, type?, allowInPlay?
std::string HandleSetScriptField(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    namespace Script = Orange::Engine::Script;
    using Orange::Engine::Entity;

    if (IsPlayBlocked(host, req)) { return MakeError(id, "in play mode (pass allowInPlay:true to override)"); }
    auto* pW = host.scene.pWorld.get();
    if (pW == nullptr) { return MakeError(id, "no active world"); }

    std::string guidStr, fieldName;
    req.ReadString("args/guid", guidStr);
    if (!req.ReadString("args/fieldName", fieldName) || fieldName.empty()) { return MakeError(id, "missing 'fieldName'"); }
    if (!req.Has("args/value")) { return MakeError(id, "missing 'value'"); }
    std::string err;
    const Entity e = ResolveEntityByGuid(host, guidStr, err);
    if (!e.IsValid()) { return MakeError(id, err); }

    auto* sc = pW->GetComponent<Script::ScriptComponent>(e);
    if (sc == nullptr) { return MakeError(id, "entity has no ScriptComponent"); }

    // value → (字符串值, type)。显式 type 优先；否则按 JSON 形态推断。
    std::string valueStr;
    Script::ScriptFieldType ftype = Script::ScriptFieldType::Float;
    std::string typeStr;
    const bool hasExplicitType = req.ReadString("args/type", typeStr);
    if (hasExplicitType)
    {
        if      (typeStr == "Float")  { ftype = Script::ScriptFieldType::Float; }
        else if (typeStr == "Int")    { ftype = Script::ScriptFieldType::Int; }
        else if (typeStr == "Bool")   { ftype = Script::ScriptFieldType::Bool; }
        else if (typeStr == "String") { ftype = Script::ScriptFieldType::String; }
        else { return MakeError(id, "type must be Float|Int|Bool|String"); }
    }

    // 取 value 并 marshal 成字符串（按 ftype 或推断）。
    if (hasExplicitType)
    {
        switch (ftype)
        {
            case Script::ScriptFieldType::Bool:
            { bool b; if (!req.ReadBool("args/value", b)) return MakeError(id, "value must be a bool");
              valueStr = b ? "true" : "false"; break; }
            case Script::ScriptFieldType::Int:
            { std::int64_t i; if (!req.ReadInt("args/value", i)) return MakeError(id, "value must be an integer");
              valueStr = std::to_string(i); break; }
            case Script::ScriptFieldType::Float:
            { double d; if (!req.ReadFloat("args/value", d)) return MakeError(id, "value must be a number");
              valueStr = std::to_string(d); break; }
            case Script::ScriptFieldType::String:
            { if (!req.ReadString("args/value", valueStr)) return MakeError(id, "value must be a string"); break; }
        }
    }
    else
    {
        // 推断顺序：bool → string → int → float。
        bool b;
        std::int64_t i;
        double d;
        std::string s;
        if (req.ReadBool("args/value", b))        { ftype = Script::ScriptFieldType::Bool;  valueStr = b ? "true" : "false"; }
        else if (req.ReadString("args/value", s)) { ftype = Script::ScriptFieldType::String; valueStr = s; }
        else if (req.ReadInt("args/value", i))    { ftype = Script::ScriptFieldType::Int;   valueStr = std::to_string(i); }
        else if (req.ReadFloat("args/value", d))  { ftype = Script::ScriptFieldType::Float; valueStr = std::to_string(d); }
        else { return MakeError(id, "value must be a number / bool / string"); }
    }

    // 找现有同名 override 更新，否则追加。
    bool found = false;
    for (auto& fo : sc->fieldOverrides)
    {
        if (fo.name == fieldName) { fo.type = ftype; fo.value = valueStr; found = true; break; }
    }
    if (!found)
    {
        sc->fieldOverrides.push_back(Script::ScriptFieldOverride{fieldName, ftype, valueStr});
    }
    // 直接 mutate 走 dirty（与 plugin 一致：常规保存承载），但不进命令栈。
    host.scene.dirty = true;

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);  // fieldOverrides 直接 mutate，不可 undo
    w.WriteString("result/fieldName", fieldName);
    w.WriteBool("result/added", !found);
    return DumpLine(w);
}

// ---- op: create_material ---------------------------------------------------
// 建 .material 文件（复用 Material::WriteMaterialFile + 模板名）。非命令栈（文件 IO）。
// args: path, templateName
std::string HandleCreateMaterial(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    (void)host;
    std::string path, templateName;
    if (!req.ReadString("args/path", path) || path.empty()) { return MakeError(id, "missing 'path'"); }
    if (!req.ReadString("args/templateName", templateName) || templateName.empty())
    {
        return MakeError(id, "missing 'templateName'");
    }
    Orange::Editor::Material::MaterialFileData data;
    data.templateName = templateName;
    if (!Orange::Editor::Material::WriteMaterialFile(path, data))
    {
        return MakeError(id, "write material failed (see editor log)");
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);
    w.WriteString("result/path", path);
    return DumpLine(w);
}

// ---- op: set_material_param ------------------------------------------------
// 改 .material 的某个 uniform override 值并落盘（ReadMaterialFile → 改 → Write）。
// 该 uniform 必须已在文件里（按 name 找，按其现有 type 解析 value）；不在 → error。
// 非命令栈（资产 IO）。args: path, param(uniform name), value
std::string HandleSetMaterialParam(std::int64_t id, EditorHost& host, const JsonReader& req)
{
    (void)host;
    namespace Mat = Orange::Editor::Material;
    using MUT = Orange::Engine::Render::MaterialUniformType;

    std::string path, param;
    if (!req.ReadString("args/path", path) || path.empty()) { return MakeError(id, "missing 'path'"); }
    if (!req.ReadString("args/param", param) || param.empty()) { return MakeError(id, "missing 'param'"); }
    if (!req.Has("args/value")) { return MakeError(id, "missing 'value'"); }

    auto loaded = Mat::ReadMaterialFile(path);
    if (!loaded) { return MakeError(id, "read material failed (missing / bad schema): " + path); }
    Mat::MaterialFileData data = *loaded;

    Mat::UniformOverrideValue* target = nullptr;
    for (auto& u : data.uniforms) { if (u.name == param) { target = &u; break; } }
    if (target == nullptr)
    {
        return MakeError(id, "uniform '" + param + "' not present in material (add it in the template first)");
    }

    // 按现有 type 解析 value 写入 variant。
    switch (target->type)
    {
        case MUT::Float: { double d; if (!req.ReadFloat("args/value", d)) return MakeError(id, "value must be a number");
                           target->value = static_cast<float>(d); break; }
        case MUT::Int:   { std::int64_t i; if (!req.ReadInt("args/value", i)) return MakeError(id, "value must be an integer");
                           target->value = static_cast<std::int32_t>(i); break; }
        case MUT::Vec2:  { float a[2]; if (!req.ReadFloatArray("args/value", a, 2)) return MakeError(id, "value must be a 2-number array");
                           target->value = glm::vec2(a[0], a[1]); break; }
        case MUT::Vec3:  { float a[3]; if (!req.ReadFloatArray("args/value", a, 3)) return MakeError(id, "value must be a 3-number array");
                           target->value = glm::vec3(a[0], a[1], a[2]); break; }
        case MUT::Vec4:  { float a[4]; if (!req.ReadFloatArray("args/value", a, 4)) return MakeError(id, "value must be a 4-number array");
                           target->value = glm::vec4(a[0], a[1], a[2], a[3]); break; }
        case MUT::Mat4:
        default:
            return MakeError(id, "mat4 uniform not writable via MCP");
    }

    if (!Mat::WriteMaterialFile(path, data))
    {
        return MakeError(id, "write material failed (see editor log)");
    }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteBool("result/undoable", false);
    w.WriteString("result/path", path);
    w.WriteString("result/param", param);
    return DumpLine(w);
}

// ---- op: get_editor_log ----------------------------------------------------
// 拉编辑器最近日志（编辑器层经 logReader 注入读 Console ring buffer）。无注入 →
// 空数组。非命令栈。args: lines?(默认 100), minLevel?(0=Trace 起)
std::string HandleGetEditorLog(std::int64_t id, const JsonReader& req, const McpLogReader& logReader)
{
    const int lines    = static_cast<int>(req.GetInt("args/lines", 100));
    const int minLevel = static_cast<int>(req.GetInt("args/minLevel", 0));

    std::vector<McpLogLine> entries;
    if (logReader) { entries = logReader(lines > 0 ? lines : 100, minLevel); }

    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", true);
    w.WriteInt("result/count", static_cast<std::int64_t>(entries.size()));
    w.BeginArray("result/lines", entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const std::string base = "result/lines/" + std::to_string(i);
        w.WriteInt(base + "/level", entries[i].level);
        w.WriteString(base + "/timestamp", entries[i].timestamp);
        w.WriteString(base + "/message", entries[i].message);
    }
    return DumpLine(w);
}

}  // namespace

std::string ExecuteMcpCommand(const std::string&                requestJson,
                              EditorHost&                       host,
                              Orange::Engine::Render::Pipeline* viewportPipeline,
                              const McpLogReader&               logReader)
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
        if (op == "get_editor_state") { return HandleGetEditorState(id, host); }
        if (op == "list_component_types") { return HandleListComponentTypes(id); }
        if (op == "capture_viewport") { return HandleCaptureViewport(id, viewportPipeline); }
        if (op == "set_field") { return HandleSetField(id, host, r); }
        if (op == "create_entity") { return HandleCreateEntity(id, host, r); }
        if (op == "add_component") { return HandleAddComponent(id, host, r); }
        if (op == "delete_entity") { return HandleDeleteEntity(id, host, r); }
        if (op == "select_entity") { return HandleSelectEntity(id, host, r); }
        if (op == "save_scene") { return HandleSaveScene(id, host, r); }

        // ---- P1 ----
        if (op == "find_entities") { return HandleFindEntities(id, host, r); }
        if (op == "get_camera") { return HandleGetCamera(id, host); }
        if (op == "set_camera") { return HandleSetCamera(id, host, r); }
        if (op == "frame_entity") { return HandleFrameEntity(id, host, r); }
        if (op == "duplicate_entity") { return HandleDuplicateEntity(id, host, r); }
        if (op == "reparent_entity") { return HandleReparentEntity(id, host, r); }
        if (op == "remove_component") { return HandleRemoveComponent(id, host, r); }
        if (op == "begin_undo_group") { return HandleBeginUndoGroup(id, host, r); }
        if (op == "end_undo_group") { return HandleEndUndoGroup(id, host); }
        if (op == "open_scene") { return HandleOpenScene(id, host, r); }
        if (op == "play" || op == "pause" || op == "resume" || op == "stop")
        {
            return HandleSetPlayOp(id, host, op);
        }
        if (op == "list_assets") { return HandleListAssets(id, r); }
        if (op == "import_asset") { return HandleImportAsset(id, host, r); }

        // ---- P2 ----
        if (op == "set_entity_order") { return HandleSetEntityOrder(id, host, r); }
        if (op == "new_scene") { return HandleNewScene(id, host, r); }
        if (op == "create_prefab") { return HandleCreatePrefab(id, host, r); }
        if (op == "instantiate_prefab") { return HandleInstantiatePrefab(id, host, r); }
        if (op == "get_prefab_status") { return HandleGetPrefabStatus(id, host, r); }
        if (op == "revert_override") { return HandleRevertOverride(id, host, r); }
        if (op == "apply_instance") { return HandleApplyInstance(id, host, r); }
        if (op == "get_animation_clip") { return HandleGetAnimationClip(id, host, r); }
        if (op == "set_animation_clip") { return HandleSetAnimationClip(id, host, r); }
        if (op == "preview_animation") { return HandlePreviewAnimation(id, host, r); }
        if (op == "set_script_field") { return HandleSetScriptField(id, host, r); }
        if (op == "create_material") { return HandleCreateMaterial(id, host, r); }
        if (op == "set_material_param") { return HandleSetMaterialParam(id, host, r); }
        if (op == "get_editor_log") { return HandleGetEditorLog(id, r, logReader); }

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

void TickMcpUndoGroupGuard(EditorHost& host)
{
    auto& bridge = host.mcp;
    if (!bridge.undoGroupOpen) { return; }

    // 命令栈已被场景切换（New/Open/Stop）Clear → 组已不在；清会话态即可（不 EndGroup）。
    if (!host.cmdStack.InGroup())
    {
        bridge.undoGroupOpen = false;
        return;
    }

    // 开组的那个客户端断开 / 被新连接替换 → 自动闭合（AI 没机会再 end）。
    const bool disconnected = !bridge.clientConnected.load()
        || bridge.clientGeneration.load() != bridge.undoGroupClientGen;
    // 开组超过 30s（AI 忘调 end_undo_group）→ 自动闭合，防栈长期卡在组内。
    const auto elapsed  = std::chrono::steady_clock::now() - bridge.undoGroupStart;
    const bool timedOut = elapsed > std::chrono::seconds(30);

    if (disconnected || timedOut)
    {
        host.cmdStack.EndGroup();
        bridge.undoGroupOpen = false;
        ORANGE_LOG_INFO("[mcp] undo group 自动闭合（{}）",
                        disconnected ? "客户端断开" : "30s 超时");
    }
}

}  // namespace Orange::Editor::Mcp
