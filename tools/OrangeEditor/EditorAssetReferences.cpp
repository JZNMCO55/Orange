// EditorAssetReferences 实现 —— 只读资产引用扫描（见头注释）。

#include "EditorAssetReferences.h"

#include "EditorHost.h"
#include "schema/ComponentSchemaRegistry.h"

#include <orange/engine/scene/World.h>

#include <entt/entity/registry.hpp>

namespace Orange::Editor
{

std::vector<AssetReference> FindAssetReferences(EditorHost& host, std::string_view assetPath)
{
    std::vector<AssetReference> refs;
    if (assetPath.empty()) { return refs; }
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return refs; }

    auto& reg       = pWorld->Registry();
    auto& schemaReg = Orange::Editor::Schema::ComponentSchemaRegistry::Instance();

    // 预筛：只关心带 AssetRef 字段且可取组件指针的 schema。
    for (auto e : reg.view<entt::entity>())
    {
        const Orange::Engine::Entity entity = Orange::Engine::World::FromEntt(e);
        for (const auto& schema : schemaReg.All())
        {
            if (schema.get == nullptr) { continue; }

            bool hasAssetRef = false;
            for (const auto& prop : schema.properties)
            {
                if (prop.assetRefGet != nullptr) { hasAssetRef = true; break; }
            }
            if (!hasAssetRef) { continue; }

            void* component = schema.get(*pWorld, entity);
            if (component == nullptr) { continue; }  // 该实体未挂此 component

            for (const auto& prop : schema.properties)
            {
                if (prop.assetRefGet == nullptr) { continue; }
                std::string path;
                prop.assetRefGet(component, host.assets, &path);
                if (!path.empty() && path == assetPath)
                {
                    refs.push_back({entity,
                                    schema.typeName ? schema.typeName : "?",
                                    prop.name ? prop.name : "?"});
                }
            }
        }
    }
    return refs;
}

std::size_t RemapAssetReferences(EditorHost& host, std::string_view fromPath, std::string_view toPath)
{
    std::size_t changed = 0;
    if (fromPath.empty() || toPath.empty()) { return 0; }
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return 0; }

    auto& reg          = pWorld->Registry();
    auto& schemaReg    = Orange::Editor::Schema::ComponentSchemaRegistry::Instance();
    const std::string toStr{toPath};

    for (auto e : reg.view<entt::entity>())
    {
        const Orange::Engine::Entity entity = Orange::Engine::World::FromEntt(e);
        for (const auto& schema : schemaReg.All())
        {
            if (schema.get == nullptr) { continue; }

            bool hasAssetRef = false;
            for (const auto& prop : schema.properties)
            {
                if (prop.assetRefGet != nullptr && prop.assetRefSet != nullptr)
                {
                    hasAssetRef = true;
                    break;
                }
            }
            if (!hasAssetRef) { continue; }

            void* component = schema.get(*pWorld, entity);
            if (component == nullptr) { continue; }

            for (const auto& prop : schema.properties)
            {
                if (prop.assetRefGet == nullptr || prop.assetRefSet == nullptr) { continue; }
                std::string cur;
                prop.assetRefGet(component, host.assets, &cur);
                if (cur == fromPath)
                {
                    prop.assetRefSet(component, host.assets, &toStr);
                    ++changed;
                }
            }
        }
    }
    return changed;
}

std::vector<ClearedAssetRef> ClearAssetReferences(EditorHost& host, std::string_view assetPath)
{
    std::vector<ClearedAssetRef> cleared;
    if (assetPath.empty()) { return cleared; }
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return cleared; }

    auto& reg             = pWorld->Registry();
    auto& schemaReg       = Orange::Editor::Schema::ComponentSchemaRegistry::Instance();
    const std::string emptyStr;  // assetRefSet 空串 → 字段置 none

    for (auto e : reg.view<entt::entity>())
    {
        const Orange::Engine::Entity entity = Orange::Engine::World::FromEntt(e);
        for (const auto& schema : schemaReg.All())
        {
            if (schema.get == nullptr) { continue; }
            void* component = nullptr;  // lazy：仅在该 schema 有 AssetRef 字段时取
            for (const auto& prop : schema.properties)
            {
                if (prop.assetRefGet == nullptr || prop.assetRefSet == nullptr) { continue; }
                if (component == nullptr)
                {
                    component = schema.get(*pWorld, entity);
                    if (component == nullptr) { break; }  // 该实体未挂此 component
                }
                std::string cur;
                prop.assetRefGet(component, host.assets, &cur);
                if (cur == assetPath)
                {
                    cleared.push_back({entity, &schema, &prop});
                    prop.assetRefSet(component, host.assets, &emptyStr);
                }
            }
        }
    }
    return cleared;
}

void RestoreAssetReferences(EditorHost& host,
                            const std::vector<ClearedAssetRef>& cleared,
                            std::string_view assetPath)
{
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return; }
    const std::string pathStr{assetPath};
    for (const auto& c : cleared)
    {
        if (c.schema == nullptr || c.prop == nullptr) { continue; }
        if (c.schema->get == nullptr || c.prop->assetRefSet == nullptr) { continue; }
        if (!pWorld->IsValid(c.entity)) { continue; }
        void* component = c.schema->get(*pWorld, c.entity);
        if (component == nullptr) { continue; }
        c.prop->assetRefSet(component, host.assets, &pathStr);
    }
}

}  // namespace Orange::Editor
