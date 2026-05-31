// ImportHostBridge —— 资产导入的 GUI（EditorHost）薄壳层（GAP-2026-05-27 G1）。
//
// 这个单独 TU 集中所有"取 EditorHost& / 调 EnsureMaterialInstance"的导入入口
// 包装，让真正的 importer 实现 TU（ImportDispatcher.cpp / ObjImporter.cpp /
// GltfImporter.cpp）保持 headless 可链 —— 它们只依赖
// Orange::Engine::Asset::AssetRegistry，不引 EditorHost / BuiltinAssets，故能被
// headless ctest（tests/editor/HeadlessMeshImportTest.cpp）单独编进测试 exe 而
// 无需链接编辑器态（AudioEngine / ThumbnailService / EnsureMaterialInstance）。
//
// 本 TU 只被 OrangeEditor 主程序编译（不进任何 headless 测试 target）。每个包装
// 取 host.assets.pAssets 当 AssetRegistry，做 nullptr 守卫（与重构前一致），再
// 委托到 import/ 下的 registry-only seam；gltf 路径额外注入 EnsureMaterialInstance
// 回调，把刚写出的 .material 注册进编辑器 namedMaterialInstances / userMaterials
// 缓存（导入后即可在 Inspector Material 下拉选中）。

#include "GltfImporter.h"
#include "ImportDispatcher.h"
#include "ObjImporter.h"
#include "../BuiltinAssets.h"  // EnsureMaterialInstance
#include "../EditorHost.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/core/Log.h>

#include <string>

namespace Orange::Editor::Import
{

namespace
{
// 取 host 的 AssetRegistry；未初始化时填好 result 并返回 false（caller 直接
// 返回错误，与重构前 "pAssets == nullptr → AssetLoadFailed" 行为一致）。
bool ResolveHostRegistry(EditorHost& host, std::string_view what,
                         std::string_view srcPath,
                         ::Orange::Engine::Asset::AssetRegistry*& outRegistry,
                         ImportResult& result)
{
    if (host.assets.pAssets == nullptr)
    {
        result.status  = ImportStatus::AssetLoadFailed;
        result.message = "AssetRegistry not initialized";
        ORANGE_LOG_ERROR("{}: '{}': {}", what, srcPath, result.message);
        outRegistry = nullptr;
        return false;
    }
    outRegistry = host.assets.pAssets.get();
    return true;
}
}  // namespace

ImportResult RunObjImport(std::string_view srcPath, EditorHost& host)
{
    ImportResult result{};
    ::Orange::Engine::Asset::AssetRegistry* registry = nullptr;
    if (!ResolveHostRegistry(host, "ObjImporter", srcPath, registry, result))
    {
        return result;
    }
    return RunObjImportToRegistry(srcPath, *registry);
}

ImportResult RunGltfImport(std::string_view srcPath, EditorHost& host)
{
    ImportResult result{};
    ::Orange::Engine::Asset::AssetRegistry* registry = nullptr;
    if (!ResolveHostRegistry(host, "GltfImporter", srcPath, registry, result))
    {
        return result;
    }
    // GUI 路径注入 EnsureMaterialInstance 把刚写出的 .material 注册进编辑器
    // namedMaterialInstances / userMaterials 缓存。EnsureMaterialInstance 在
    // ::（全局）命名空间，签名 (EditorHost&, const std::string&)。
    auto registerMaterial = [&host](const std::string& matPath) {
        if (::EnsureMaterialInstance(host, matPath) != nullptr)
        {
            ORANGE_LOG_INFO("GltfImporter: material '{}' 已注册 → 可在 Renderable "
                            "Material 字段选用", matPath);
        }
    };
    return RunGltfImportToRegistry(srcPath, *registry, registerMaterial);
}

ImportResult ImportTexture(std::string_view srcPath, EditorHost& host,
                           std::string_view destDirOverride)
{
    ImportResult result{};
    ::Orange::Engine::Asset::AssetRegistry* registry = nullptr;
    if (!ResolveHostRegistry(host, "ImportTexture", srcPath, registry, result))
    {
        return result;
    }
    return ImportTextureToRegistry(srcPath, *registry, destDirOverride);
}

ImportResult ImportObjMesh(std::string_view srcPath, EditorHost& host)
{
    return RunObjImport(srcPath, host);
}

ImportResult ImportGltfMesh(std::string_view srcPath, EditorHost& host)
{
    return RunGltfImport(srcPath, host);
}

ImportResult Dispatch(std::string_view srcPath, EditorHost& host)
{
    // 委托到 registry-only DispatchToRegistry（ext 分类逻辑单点在那里），注入
    // host registry + EnsureMaterialInstance 回调。gltf 路径才会用到回调，
    // texture / obj 路径忽略它，行为与逐函数包装完全一致。
    ImportResult result{};
    ::Orange::Engine::Asset::AssetRegistry* registry = nullptr;
    if (!ResolveHostRegistry(host, "ImportDispatcher", srcPath, registry, result))
    {
        return result;
    }
    auto registerMaterial = [&host](const std::string& matPath) {
        if (::EnsureMaterialInstance(host, matPath) != nullptr)
        {
            ORANGE_LOG_INFO("GltfImporter: material '{}' 已注册 → 可在 Renderable "
                            "Material 字段选用", matPath);
        }
    };
    return DispatchToRegistry(srcPath, *registry, registerMaterial);
}

}  // namespace Orange::Editor::Import
