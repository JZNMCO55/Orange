#include "ImportDispatcher.h"

#include "MetaSidecar.h"
#include "ObjImporter.h"
#include "../EditorHost.h"

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
    if (!body.empty() && body.front() == '.') { body = body.substr(1); }
    if (body.size() != target.size()) { return false; }
    for (std::size_t i = 0; i < body.size(); ++i)
    {
        char a = body[i];
        char b = target[i];
        if (a >= 'A' && a <= 'Z') { a = static_cast<char>(a + 32); }
        if (b >= 'A' && b <= 'Z') { b = static_cast<char>(b + 32); }
        if (a != b) { return false; }
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
}  // namespace

ImportKind ClassifyByExt(std::string_view ext)
{
    if (ExtMatches(ext, "png") || ExtMatches(ext, "jpg")  ||
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
    return ImportKind::Unsupported;
}

ImportResult ImportTexture(std::string_view srcPath, EditorHost& host)
{
    namespace fs = std::filesystem;

    ImportResult result{};
    if (host.assets.pAssets == nullptr)
    {
        result.status  = ImportStatus::AssetLoadFailed;
        result.message = "AssetRegistry not initialized";
        ORANGE_LOG_ERROR("ImportTexture: '{}': {}", srcPath, result.message);
        return result;
    }

    fs::path src(srcPath.begin(), srcPath.end());
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
        return result;  // ComputeFileHashFnv1a 自身已经 log
    }

    // 目标路径：assets/Textures/<filename>。文件已存在时 overwrite（reimport
    // 语义；与 T5 接通后 .meta 的 hash 比对配合，相同源 hash 时跳过 copy）。
    fs::path destDir = kTexturesDir;
    fs::create_directories(destDir, ec);  // 失败下面 copy 一并兜
    fs::path dest = destDir / src.filename();

    fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        result.status  = ImportStatus::CopyFailed;
        result.message = "copy_file failed: " + ec.message();
        ORANGE_LOG_ERROR("ImportTexture: '{}' -> '{}': {}",
                         srcPath, dest.generic_string(), result.message);
        return result;
    }

    const std::string destStr = dest.generic_string();

    // 让 AssetRegistry 走 TextureLoader::Load 路径加载。dedup by path：同
    // path 已存在 entry 时 reuse 旧 handle，但底层文件已被 copy_file 覆盖
    // —— Load 内部会重新解码新文件。
    auto loadRes = host.assets.pAssets
        ->Load<::Orange::Engine::Asset::TextureAsset>(destStr);
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
    meta.handleId   = 0;  // 占位，v1 未启用

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

ImportResult ImportObjMesh(std::string_view srcPath, EditorHost& host)
{
    // v1.1 T3 路由到 ObjImporter 模块（tinyobjloader IMPLEMENTATION 仅在
    // ObjImporter.cpp 单 TU expand）。
    return RunObjImport(srcPath, host);
}

ImportResult ImportGltfMesh(std::string_view srcPath, EditorHost& /*host*/)
{
    ImportResult result{};
    result.status  = ImportStatus::NotImplemented;
    result.message = "gltf importer hooked in v1.1 T4";
    ORANGE_LOG_WARN("ImportGltfMesh: '{}': {}", srcPath, result.message);
    return result;
}

ImportResult Dispatch(std::string_view srcPath, EditorHost& host)
{
    const auto ext = ExtractExt(srcPath);
    const ImportKind kind = ClassifyByExt(ext);
    switch (kind)
    {
        case ImportKind::Texture:  return ImportTexture(srcPath, host);
        case ImportKind::ObjMesh:  return ImportObjMesh(srcPath, host);
        case ImportKind::GltfMesh: return ImportGltfMesh(srcPath, host);
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

}  // namespace Orange::Editor::Import
