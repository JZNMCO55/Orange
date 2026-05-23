#include "GltfImporter.h"

#include "MetaSidecar.h"
#include "../EditorHost.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/core/Log.h>

// cgltf 单 header IMPLEMENTATION 仅在本 TU 内 expand。MSVC noisy warning
// 关掉 —— cgltf 是 C99 风格代码，narrowing / unused / deprecated 全套都
// 会被 /W4 /WX 当 error。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4244)  // narrowing
#  pragma warning(disable: 4267)  // size_t → smaller int
#  pragma warning(disable: 4505)  // unreferenced local function
#  pragma warning(disable: 4996)  // deprecated CRT
#  pragma warning(disable: 4100)  // unreferenced formal parameter
#  pragma warning(disable: 4456)  // shadowed local
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Orange::Editor::Import
{

namespace
{
constexpr const char* kModelsDir = "assets/Models";

// 找 primitive 的某个 attribute（POSITION / NORMAL / TEXCOORD_0）；
// 不存在返回 nullptr。
const cgltf_accessor* FindAttribute(const cgltf_primitive& prim,
                                    cgltf_attribute_type wanted,
                                    int wantedIndex = 0)
{
    for (cgltf_size i = 0; i < prim.attributes_count; ++i)
    {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.type == wanted && a.index == wantedIndex)
        {
            return a.data;
        }
    }
    return nullptr;
}

const char* CgltfResultToString(cgltf_result r)
{
    switch (r)
    {
        case cgltf_result_success:         return "success";
        case cgltf_result_data_too_short:  return "data_too_short";
        case cgltf_result_unknown_format:  return "unknown_format";
        case cgltf_result_invalid_json:    return "invalid_json";
        case cgltf_result_invalid_gltf:    return "invalid_gltf";
        case cgltf_result_invalid_options: return "invalid_options";
        case cgltf_result_file_not_found:  return "file_not_found";
        case cgltf_result_io_error:        return "io_error";
        case cgltf_result_out_of_memory:   return "out_of_memory";
        case cgltf_result_legacy_gltf:     return "legacy_gltf";
        default:                           return "unknown";
    }
}
}  // namespace

ImportResult RunGltfImport(std::string_view srcPath, EditorHost& host)
{
    using ::Orange::Engine::Asset::AssetRegistry;
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::MeshLoader;
    using ::Orange::Engine::Asset::VertexNormal3;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    namespace fs = std::filesystem;

    ImportResult result{};
    if (host.assets.pAssets == nullptr)
    {
        result.status  = ImportStatus::AssetLoadFailed;
        result.message = "AssetRegistry not initialized";
        ORANGE_LOG_ERROR("GltfImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    fs::path src(srcPath.begin(), srcPath.end());
    std::error_code ec;
    if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec))
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "source .gltf/.glb missing or not a regular file";
        ORANGE_LOG_ERROR("GltfImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    // T5 hash 增量短路：先算源 hash + 目标 .mesh 路径，若 .meta sourceHash
    // 已匹配则跳过 cgltf 解析 + 合并 + Save + Load 全套。
    const auto earlyHashOpt = ComputeFileHashFnv1a(srcPath);
    if (earlyHashOpt.has_value())
    {
        const std::string earlyStem = src.stem().generic_string();
        const std::string earlyDestMesh =
            (fs::path(kModelsDir) / (earlyStem + ".mesh")).generic_string();
        if (MetaSourceHashMatches(earlyDestMesh, earlyHashOpt.value()))
        {
            result.status   = ImportStatus::Success;
            result.destPath = earlyDestMesh;
            result.message  = "gltf unchanged, skipped reimport";
            ORANGE_LOG_INFO("GltfImporter: '{}' unchanged (hash={}), skip",
                            srcPath, HashToHexString(earlyHashOpt.value()));
            return result;
        }
    }

    const std::string srcStr(srcPath);

    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result rc = cgltf_parse_file(&options, srcStr.c_str(), &data);
    if (rc != cgltf_result_success || data == nullptr)
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = std::string("cgltf_parse_file: ") + CgltfResultToString(rc);
        ORANGE_LOG_ERROR("GltfImporter: '{}': {}", srcPath, result.message);
        if (data != nullptr) { cgltf_free(data); }
        return result;
    }

    // 加载 buffer 数据（.glb 内嵌 + .gltf 外部 .bin 都走这一条）。失败时
    // attribute 仍可遍历但 accessor 读不出 float —— 必须先 ok。
    rc = cgltf_load_buffers(&options, data, srcStr.c_str());
    if (rc != cgltf_result_success)
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = std::string("cgltf_load_buffers: ") + CgltfResultToString(rc);
        ORANGE_LOG_ERROR("GltfImporter: '{}': {}", srcPath, result.message);
        cgltf_free(data);
        return result;
    }

    // unified arrays —— 所有 mesh / 所有 triangle primitive 的 attribute 顺序
    // 拼接；indices 同步 offset 调整。
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<VertexNormal3>   normals;
    std::vector<std::uint32_t>   indices;

    bool fileHasUVs     = false;
    bool fileHasNormals = false;
    std::size_t skippedPrimitives = 0;

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi)
    {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
            {
                ++skippedPrimitives;
                continue;
            }

            const cgltf_accessor* posAcc = FindAttribute(prim, cgltf_attribute_type_position);
            if (posAcc == nullptr || posAcc->count == 0)
            {
                ++skippedPrimitives;
                continue;
            }
            const cgltf_accessor* nrmAcc = FindAttribute(prim, cgltf_attribute_type_normal);
            const cgltf_accessor* uvAcc  = FindAttribute(prim, cgltf_attribute_type_texcoord, 0);

            const std::uint32_t baseIdx = static_cast<std::uint32_t>(positions.size());
            const cgltf_size vtxCount = posAcc->count;

            // 拷贝 positions（必备）。cgltf_accessor_read_float 自动处理
            // normalize / 类型转换；4-component 输出（POSITION 实际是 vec3，
            // 用 4 给 padding 容错）。
            positions.reserve(positions.size() + vtxCount);
            for (cgltf_size v = 0; v < vtxCount; ++v)
            {
                float buf[3] = {0, 0, 0};
                cgltf_accessor_read_float(posAcc, v, buf, 3);
                positions.push_back({buf[0], buf[1], buf[2]});
            }

            // normals（可选）—— 当前 primitive 没法线时填占位 (0,1,0)，但
            // 在 unified 阶段判 fileHasNormals 决定是否最终输出 normal 段。
            // 若有些 primitive 有法线、有些没，统一不输出 normal 段最稳，
            // 让 Load 端按整体 ComputeSmooth 重算。
            normals.reserve(normals.size() + vtxCount);
            if (nrmAcc != nullptr && nrmAcc->count == vtxCount)
            {
                for (cgltf_size v = 0; v < vtxCount; ++v)
                {
                    float buf[3] = {0, 1, 0};
                    cgltf_accessor_read_float(nrmAcc, v, buf, 3);
                    normals.push_back({buf[0], buf[1], buf[2]});
                }
                fileHasNormals = true;
            }
            else
            {
                normals.resize(normals.size() + vtxCount, VertexNormal3{0, 1, 0});
            }

            // uvs（可选）
            uvs.reserve(uvs.size() + vtxCount);
            if (uvAcc != nullptr && uvAcc->count == vtxCount)
            {
                for (cgltf_size v = 0; v < vtxCount; ++v)
                {
                    float buf[2] = {0, 0};
                    cgltf_accessor_read_float(uvAcc, v, buf, 2);
                    // glTF 2.0 spec: texcoord 原点左上（与 Vulkan 一致）；
                    // 不做 1-v 翻转（区别于 .obj）。
                    uvs.push_back({buf[0], buf[1]});
                }
                fileHasUVs = true;
            }
            else
            {
                uvs.resize(uvs.size() + vtxCount, VertexUV2{0, 0});
            }

            // indices：必备（cgltf 也支持非 indexed 但 glTF 2.0 标准 mesh
            // 几乎都有 indices；prim.indices == nullptr 时构造 0..N-1 默认
            // 序列让下游 MeshLoader 仍能正确处理）。
            if (prim.indices != nullptr)
            {
                const cgltf_accessor* idxAcc = prim.indices;
                const cgltf_size idxCount = idxAcc->count;
                indices.reserve(indices.size() + idxCount);
                for (cgltf_size i = 0; i < idxCount; ++i)
                {
                    const cgltf_uint v =
                        static_cast<cgltf_uint>(cgltf_accessor_read_index(idxAcc, i));
                    indices.push_back(baseIdx + v);
                }
            }
            else
            {
                indices.reserve(indices.size() + vtxCount);
                for (cgltf_size v = 0; v < vtxCount; ++v)
                {
                    indices.push_back(baseIdx + static_cast<std::uint32_t>(v));
                }
            }
        }
    }

    if (skippedPrimitives > 0)
    {
        ORANGE_LOG_WARN("GltfImporter: '{}' skipped {} non-triangle / no-position primitive(s)",
                        srcPath, skippedPrimitives);
    }
    if (positions.empty() || indices.empty())
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "glTF has no triangulated geometry with positions";
        ORANGE_LOG_ERROR("GltfImporter: '{}': {}", srcPath, result.message);
        cgltf_free(data);
        return result;
    }

    cgltf_free(data);  // 不再需要 cgltf 内部结构，data 已经拷出来

    // 整文件无 normal/uv 时清空数组让 MeshLoader::Save 写 has=0；混合情况
    // （部分 primitive 有法线、部分没）走 has=true 路径但缺失部分已被填充
    // (0,1,0)——Load 端不会重算，渲染端可能偏。v1.x 长尾如需改进则在
    // Importer 内对 missing-normal primitive 单独跑 ComputeSmoothNormals
    // local（当前 v1.1 不做）。
    if (!fileHasNormals) { normals.clear(); }
    if (!fileHasUVs)     { uvs.clear(); }

    // 目标路径：assets/Models/<basename>.mesh + 同目录 source copy。
    // .gltf 路径需要把外部 .bin / 贴图也 copy 一并进 assets/Models/ 才完整
    // —— 但 v1.1 范围内贴图独立 import + 没贴图字段，.bin 由 glb embedded
    // 模式承载较多，外部 .bin 的 .gltf 用 copy 后用户可手动同步 .bin。先
    // 做单文件 copy 起步。
    fs::path destDir = kModelsDir;
    fs::create_directories(destDir, ec);
    const std::string stem = src.stem().generic_string();
    fs::path destMesh = destDir / (stem + ".mesh");
    fs::path destGltf = destDir / src.filename();

    fs::copy_file(src, destGltf, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        result.status  = ImportStatus::CopyFailed;
        result.message = "copy_file source failed: " + ec.message();
        ORANGE_LOG_ERROR("GltfImporter: '{}' -> '{}': {}",
                         srcPath, destGltf.generic_string(), result.message);
        return result;
    }

    std::unique_ptr<MeshAsset> mesh;
    if (uvs.empty() && normals.empty())
    {
        mesh = std::make_unique<MeshAsset>(std::move(positions), std::move(indices));
    }
    else if (normals.empty())
    {
        mesh = std::make_unique<MeshAsset>(std::move(positions),
                                           std::move(uvs),
                                           std::move(indices));
    }
    else if (uvs.empty())
    {
        std::vector<VertexUV2> emptyUv;
        emptyUv.resize(positions.size());
        mesh = std::make_unique<MeshAsset>(std::move(positions),
                                           std::move(emptyUv),
                                           std::move(normals),
                                           std::move(indices));
    }
    else
    {
        mesh = std::make_unique<MeshAsset>(std::move(positions),
                                           std::move(uvs),
                                           std::move(normals),
                                           std::move(indices));
    }

    const std::string destMeshStr = destMesh.generic_string();
    auto saveRes = MeshLoader::Save(destMeshStr, *mesh);
    if (saveRes.IsErr())
    {
        result.status  = ImportStatus::AssetLoadFailed;
        result.message = "MeshLoader::Save failed";
        ORANGE_LOG_ERROR("GltfImporter: '{}' -> '{}': {}",
                         srcPath, destMeshStr, result.message);
        return result;
    }

    auto loadRes = host.assets.pAssets->Load<MeshAsset>(destMeshStr);
    if (loadRes.IsErr())
    {
        result.status  = ImportStatus::AssetLoadFailed;
        result.message = "AssetRegistry::Load<MeshAsset> failed";
        ORANGE_LOG_ERROR("GltfImporter: '{}' -> '{}': {}",
                         srcPath, destMeshStr, result.message);
        return result;
    }

    const auto hashOpt = ComputeFileHashFnv1a(srcPath);
    if (!hashOpt.has_value())
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "FNV-1a hash failed";
        return result;
    }
    TextureMetaV1 meta{};
    meta.sourcePath = src.generic_string();
    meta.sourceHash = hashOpt.value();
    meta.handleId   = 0;
    const std::string metaPath = MetaPathFor(destMeshStr);
    if (!WriteTextureMeta(metaPath, meta))
    {
        result.status  = ImportStatus::MetaWriteFailed;
        result.message = ".meta write failed";
        ORANGE_LOG_ERROR("GltfImporter: '{}' -> '{}': {}",
                         srcPath, metaPath, result.message);
        return result;
    }

    result.status   = ImportStatus::Success;
    result.destPath = destMeshStr;
    result.message  = "imported gltf mesh";
    ORANGE_LOG_INFO("GltfImporter: '{}' -> '{}' (vtx={} idx={} hash={})",
                    srcPath, destMeshStr,
                    mesh->Positions().size(), mesh->Indices().size(),
                    HashToHexString(meta.sourceHash));
    return result;
}

}  // namespace Orange::Editor::Import
