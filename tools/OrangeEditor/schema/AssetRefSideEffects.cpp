#include "AssetRefSideEffects.h"

#include "../BuiltinAssets.h"          // ::EnsureMaterialInstance（全局命名空间）
#include "../EditorAssetDropHandler.h" // Orange::Editor::SyncSubMeshMaterialsForMesh
#include "PropertyDescriptor.h"        // Schema::PropertyDescriptor + AssetKind

namespace Orange::Editor
{

    bool PrepareAssetRefWrite(EditorHost&                       host,
                              const Schema::PropertyDescriptor& prop,
                              const std::string&                path)
    {
        if (prop.attribs.assetKind == Schema::AssetKind::Material && !path.empty())
        {
            // EnsureMaterialInstance 读 .material → CreateInstance → ApplyDataToInstance
            // → 写回 namedMaterialInstances，使随后的 materialSet 查表命中。
            return ::EnsureMaterialInstance(host, path) != nullptr;
        }
        return true;
    }

    void FinishAssetRefWrite(EditorHost&                       host,
                             Orange::Engine::Entity            entity,
                             const Schema::PropertyDescriptor& prop,
                             const std::string&                path)
    {
        if (prop.attribs.assetKind == Schema::AssetKind::Mesh)
        {
            SyncSubMeshMaterialsForMesh(host, entity, path);
        }
    }

} // namespace Orange::Editor
