#include "McpCommandHandler.h"

#include "../EditorHost.h"
#include "../schema/ComponentSchemaRegistry.h"

#include <orange/engine/core/Serialization.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

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

// 统一错误响应：{"id":id,"ok":false,"error":msg}
std::string MakeError(std::int64_t id, const std::string& msg)
{
    JsonWriter w;
    w.WriteInt("id", id);
    w.WriteBool("ok", false);
    w.WriteString("error", msg);
    return DumpLine(w);
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

}  // namespace

std::string ExecuteMcpCommand(const std::string& requestJson, EditorHost& host)
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
