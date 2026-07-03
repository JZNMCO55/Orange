#include "EditorPrefabActions.h"

#include "EditorHost.h"
#include "command/PrefabCommands.h"
#include "render/ThumbnailService.h" // Invalidate（重存 prefab 后失效旧缩略图）

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/asset/PrefabLoader.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/World.h>

#include <memory>
#include <vector>

namespace Orange::Editor::Prefab
{

    namespace
    {
        // Create Prefab modal 的跨 TU 请求状态（见 EditorPrefabActions.h 头注释）。
        // module-level static：菜单 TU 写、modal TU 读，二者跨帧解耦。
        bool                   sPendingCreatePrefab = false;
        Orange::Engine::Entity sCreatePrefabSourceRoot =
            Orange::Engine::Entity::Invalid();
    } // anonymous namespace

    void RequestCreatePrefab(Orange::Engine::Entity sourceRoot)
    {
        sPendingCreatePrefab    = true;
        sCreatePrefabSourceRoot = sourceRoot;
    }

    bool ConsumeCreatePrefabRequest(Orange::Engine::Entity* outSourceRoot)
    {
        if (!sPendingCreatePrefab)
        {
            return false;
        }
        sPendingCreatePrefab = false;
        if (outSourceRoot != nullptr)
        {
            *outSourceRoot = sCreatePrefabSourceRoot;
        }
        return true;
    }

    bool CommitNewPrefabFile(EditorHost&            host,
                             Orange::Engine::Entity sourceRoot,
                             const std::string&     targetPath,
                             const std::string&     prefabName)
    {
        auto* pWorld = host.scene.pWorld.get();
        if (pWorld == nullptr)
        {
            ORANGE_LOG_WARN("Asset Browser: create prefab 失败 —— 无活动 World");
            return false;
        }
        if (!sourceRoot.IsValid() || !pWorld->IsValid(sourceRoot))
        {
            ORANGE_LOG_WARN("Asset Browser: create prefab 失败 —— 源实体无效");
            return false;
        }

        // 子树序列化 → templateBlob。SaveOptions 与 EntityTreePanel 的 serializeSubtree
        // lambda 填法一致（assetRegistry + namedMaterialInstances + extraSerializers），
        // 让 Renderable 等组件的 AssetHandle / materialInstance 能反查成路径 / id 字符串。
        Orange::Engine::Scene::SaveOptions saveOpts;
        saveOpts.assetRegistry          = host.assets.pAssets.get();
        saveOpts.namedMaterialInstances = &host.assets.namedMaterialInstances;
        saveOpts.extraSerializers       = host.extraSerializers;

        const std::vector<Orange::Engine::Entity> roots{sourceRoot};
        auto                                      blobRes =
            Orange::Engine::Scene::SaveSubtreeToString(*pWorld, roots, saveOpts);
        if (blobRes.IsErr())
        {
            ORANGE_LOG_ERROR("Asset Browser: create prefab '{}' 失败 —— 子树序列化 "
                             "(code={})",
                             targetPath,
                             static_cast<unsigned>(blobRes.Error()));
            return false;
        }

        auto saveRc = Orange::Engine::Asset::PrefabLoader::Save(
            targetPath, prefabName, blobRes.Value());
        if (saveRc.IsErr())
        {
            ORANGE_LOG_ERROR("Asset Browser: create prefab '{}' 写盘失败 (code={})",
                             targetPath,
                             static_cast<unsigned>(saveRc.Error()));
            return false;
        }

        ORANGE_LOG_INFO("Asset Browser: created prefab '{}' (name '{}')",
                        targetPath, prefabName);

        // 失效该路径的旧缩略图：覆盖写已有 .prefab.json 时，缓存里可能还留着上一版
        // 的预览图。Invalidate 把 content-hash 置哨兵 + 入 pending，下一帧 FlushPending
        // 帧外重烘（content-hash 本就会逮到 templateBlob 变化，这里显式失效让重烘
        // 不等下次 hash 比对，立即生效）。
        if (host.thumbnails)
        {
            host.thumbnails->Invalidate(targetPath);
        }
        return true;
    }

    bool InstantiatePrefabFromPath(EditorHost& host, const std::string& path)
    {
        auto* pReg = host.assets.pAssets.get();
        if (pReg == nullptr)
        {
            ORANGE_LOG_WARN("Prefab drop: AssetRegistry 未就绪 '{}'", path);
            return false;
        }

        auto loaded =
            pReg->Load<Orange::Engine::Asset::PrefabAsset>(path);
        if (loaded.IsErr())
        {
            ORANGE_LOG_WARN("Prefab drop: Load<PrefabAsset> '{}' 失败 (code={})",
                            path,
                            static_cast<unsigned>(loaded.Error()));
            return false;
        }

        host.cmdStack.Push(
            std::make_unique<InstantiatePrefabCommand>(host, loaded.Value()));
        return true;
    }

} // namespace Orange::Editor::Prefab
