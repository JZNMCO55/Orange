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

}  // namespace Orange::Editor
