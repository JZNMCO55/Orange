#include "ImportDispatcher.h"

#include "FbxImporter.h"
#include "GltfImporter.h"
#include "MetaSidecar.h"
#include "ObjImporter.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/core/Log.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace Orange::Editor::Import
{

    namespace
    {
        // 目标根 —— ADR-008 议题 B2：copy 到 `assets/<TypeDir>/`，scene 引用项目
        // 内路径不依赖外部文件位置。
        constexpr const char* kTexturesDir = "assets/Textures";
        constexpr const char* kModelsDir   = "assets/Models";

        // 大小写不敏感的 ext 比较。ext 形参允许带 '.' 前缀。
        bool ExtMatches(std::string_view ext, std::string_view target)
        {
            std::string_view body = ext;
            if (!body.empty() && body.front() == '.')
            {
                body = body.substr(1);
            }
            if (body.size() != target.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < body.size(); ++i)
            {
                char a = body[i];
                char b = target[i];
                if (a >= 'A' && a <= 'Z')
                {
                    a = static_cast<char>(a + 32);
                }
                if (b >= 'A' && b <= 'Z')
                {
                    b = static_cast<char>(b + 32);
                }
                if (a != b)
                {
                    return false;
                }
            }
            return true;
        }

        std::string_view ExtractExt(std::string_view path)
        {
            const auto dotPos = path.rfind('.');
            if (dotPos == std::string_view::npos || dotPos + 1 >= path.size())
            {
                return {};
            }
            return path.substr(dotPos + 1);
        }
    } // namespace

    ImportKind ClassifyByExt(std::string_view ext)
    {
        if (ExtMatches(ext, "png") || ExtMatches(ext, "jpg") ||
            ExtMatches(ext, "jpeg") || ExtMatches(ext, "tga") ||
            ExtMatches(ext, "hdr"))
        {
            return ImportKind::Texture;
        }
        if (ExtMatches(ext, "obj"))
        {
            return ImportKind::ObjMesh;
        }
        if (ExtMatches(ext, "gltf") || ExtMatches(ext, "glb"))
        {
            return ImportKind::GltfMesh;
        }
        if (ExtMatches(ext, "fbx"))
        {
            return ImportKind::FbxMesh;
        }
        return ImportKind::Unsupported;
    }

    ImportResult ImportTextureToRegistry(std::string_view                        srcPath,
                                         ::Orange::Engine::Asset::AssetRegistry& registry,
                                         std::string_view                        destDirOverride)
    {
        namespace fs = std::filesystem;

        ImportResult result{};

        fs::path        src(srcPath.begin(), srcPath.end());
        std::error_code ec;
        if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec))
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = "source file missing or not a regular file";
            ORANGE_LOG_ERROR("ImportTexture: '{}': {}", srcPath, result.message);
            return result;
        }

        // 计算源文件 hash —— 用于 .meta sourceHash 字段，T5 接通 hash 增量
        // re-import 时按它判定 "源文件改没改"。
        const auto hashOpt = ComputeFileHashFnv1a(srcPath);
        if (!hashOpt.has_value())
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = "FNV-1a hash failed";
            return result; // ComputeFileHashFnv1a 自身已经 log
        }

        // 目标路径：默认 assets/Textures/<filename>；模型 importer 传 destDirOverride
        // 时落到模型自己的 assets/Models/<stem>/ 子目录（co-locate 整套资产）。
        // 文件已存在时 overwrite（reimport 语义；下面 .meta 的 hash 比对短路：
        // 相同 hash 跳全套）。
        fs::path destDir = destDirOverride.empty()
                               ? fs::path(kTexturesDir)
                               : fs::path(std::string(destDirOverride));
        fs::create_directories(destDir, ec); // 失败下面 copy 一并兜
        fs::path          dest         = destDir / src.filename();
        const std::string destStrEarly = dest.generic_string();

        // T5 hash 增量短路：源 hash 与既有 .meta 匹配 → 已是最新，跳过 copy /
        // Load / 写 .meta 全套。这是 reimport 的最快路径（典型场景：用户重复拖
        // 同一文件 / .gitignored 资产首次进项目但已 ready）。
        if (MetaSourceHashMatches(destStrEarly, hashOpt.value()))
        {
            result.status   = ImportStatus::Success;
            result.destPath = destStrEarly;
            result.message  = "texture unchanged, skipped reimport";
            ORANGE_LOG_INFO("ImportTexture: '{}' unchanged (hash={}), skip",
                            srcPath, HashToHexString(hashOpt.value()));
            return result;
        }

        // same-file 守卫：源已经就在目标位置时跳过 copy（否则 copy_file(self, self)
        // 在标准下报错）。典型场景是 gltf 内嵌贴图——importer 已把内嵌 image 字节直接
        // 落到模型自己的 assets/Models/<stem>/ 目录，再走本函数 co-locate 时 src==dest。
        // 此时文件已在位，直接进 Load + 写 .meta 即可。equivalent 要求两端都存在，dest
        // 不存在（常规外部贴图首次导入）时 ec 置位 → 视为非同一文件，照常 copy。
        std::error_code eqEc;
        const bool      sameFile =
            fs::exists(dest, eqEc) && fs::equivalent(src, dest, eqEc) && !eqEc;
        if (!sameFile)
        {
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                result.status  = ImportStatus::CopyFailed;
                result.message = "copy_file failed: " + ec.message();
                ORANGE_LOG_ERROR("ImportTexture: '{}' -> '{}': {}",
                                 srcPath, dest.generic_string(), result.message);
                return result;
            }
        }

        const std::string destStr = dest.generic_string();

        // 让 AssetRegistry 走 TextureLoader::Load 路径加载。dedup by path：同
        // path 已存在 entry 时 reuse 旧 handle，但底层文件已被 copy_file 覆盖
        // —— Load 内部会重新解码新文件。
        auto loadRes = registry.Load<::Orange::Engine::Asset::TextureAsset>(destStr);
        if (loadRes.IsErr())
        {
            result.status  = ImportStatus::AssetLoadFailed;
            result.message = "AssetRegistry::Load<TextureAsset> failed";
            ORANGE_LOG_ERROR("ImportTexture: '{}' -> '{}': {}",
                             srcPath, destStr, result.message);
            return result;
        }

        // 写 .meta sidecar。sourcePath 存 src 的 generic string（跨平台 '/'
        // 风格）；reimport 时按它定位源文件（找不到时 T5 兜底 fallback 走
        // dest 自身 hash）。
        TextureMetaV1 meta{};
        meta.sourcePath = fs::path(srcPath.begin(), srcPath.end()).generic_string();
        meta.sourceHash = hashOpt.value();
        meta.handleId   = 0; // 占位，v1 未启用

        const std::string metaPath = MetaPathFor(destStr);
        if (!WriteTextureMeta(metaPath, meta))
        {
            result.status  = ImportStatus::MetaWriteFailed;
            result.message = ".meta write failed";
            ORANGE_LOG_ERROR("ImportTexture: '{}' -> '{}': {}",
                             srcPath, metaPath, result.message);
            return result;
        }

        result.status   = ImportStatus::Success;
        result.destPath = destStr;
        result.message  = "imported texture";
        ORANGE_LOG_INFO("ImportTexture: '{}' -> '{}' (hash={})",
                        srcPath, destStr, HashToHexString(meta.sourceHash));
        return result;
    }

    ImportResult ImportObjMeshToRegistry(std::string_view                        srcPath,
                                         ::Orange::Engine::Asset::AssetRegistry& registry)
    {
        // v1.1 T3 路由到 ObjImporter 模块（tinyobjloader IMPLEMENTATION 仅在
        // ObjImporter.cpp 单 TU expand）。
        return RunObjImportToRegistry(srcPath, registry);
    }

    ImportResult ImportGltfMeshToRegistry(std::string_view                        srcPath,
                                          ::Orange::Engine::Asset::AssetRegistry& registry,
                                          const MaterialRegisterFn&               onMaterialWritten)
    {
        // v1.1 T4 路由到 GltfImporter 模块（cgltf IMPLEMENTATION 仅在
        // GltfImporter.cpp 单 TU expand）。
        return RunGltfImportToRegistry(srcPath, registry, onMaterialWritten);
    }

    ImportResult ImportFbxMeshToRegistry(std::string_view                        srcPath,
                                         ::Orange::Engine::Asset::AssetRegistry& registry,
                                         const MaterialRegisterFn&               onMaterialWritten,
                                         float                                   importScale)
    {
        // 路由到 FbxImporter 模块（OpenFBX 头声明只在 FbxImporter.cpp 引用；
        // ofbx.cpp / libdeflate.c 作为独立 TU 由 CMake 接进 target）。
        return RunFbxImportToRegistry(srcPath, registry, onMaterialWritten, importScale);
    }

    ImportResult DispatchToRegistry(std::string_view                        srcPath,
                                    ::Orange::Engine::Asset::AssetRegistry& registry,
                                    const MaterialRegisterFn&               onMaterialWritten,
                                    float                                   importScale)
    {
        const auto       ext  = ExtractExt(srcPath);
        const ImportKind kind = ClassifyByExt(ext);
        switch (kind)
        {
            case ImportKind::Texture:
                return ImportTextureToRegistry(srcPath, registry);
            case ImportKind::ObjMesh:
                return ImportObjMeshToRegistry(srcPath, registry);
            case ImportKind::GltfMesh:
                return ImportGltfMeshToRegistry(srcPath, registry,
                                                onMaterialWritten);
            // importScale 仅 FBX 路径消费（FBX 单位歧义见 FbxAxisConverter.h）；
            // obj/gltf/texture 忽略它。
            case ImportKind::FbxMesh:
                return ImportFbxMeshToRegistry(srcPath, registry,
                                               onMaterialWritten,
                                               importScale);
            case ImportKind::Unsupported:
            default:
            {
                ImportResult result{};
                result.status  = ImportStatus::UnsupportedExt;
                result.message = "unsupported extension";
                ORANGE_LOG_WARN("ImportDispatcher: '{}': {} (ext='{}')",
                                srcPath, result.message, ext);
                return result;
            }
        }
    }

    // GUI 入口（Dispatch / ImportTexture / ImportObjMesh / ImportGltfMesh，全部
    // 取 EditorHost&）在 ImportHostBridge.cpp —— 集中所有引用 EditorHost /
    // EnsureMaterialInstance 的薄壳到那个单独 TU，让本 TU 保持 headless 可链
    // （只依赖 AssetRegistry，不引编辑器态）。

} // namespace Orange::Editor::Import
