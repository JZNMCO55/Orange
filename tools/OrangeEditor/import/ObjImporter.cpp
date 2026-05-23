#include "ObjImporter.h"

#include "MetaSidecar.h"
#include "../EditorHost.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/core/Log.h>

// tinyobjloader 单 header IMPLEMENTATION 仅在本 TU 内 expand —— 与
// stb_image / stb_image_write 等单 header 库同款做法（避多 TU 重定义）。
// 关掉 MSVC noisy warning（C4244 narrowing / C4267 size_t conversion /
// C4996 deprecation / C4505 unreferenced local function 等），与
// TextureLoader.cpp / Pipeline.cpp 的 stb 风格一致。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4244)
#  pragma warning(disable: 4267)
#  pragma warning(disable: 4505)
#  pragma warning(disable: 4996)
#endif
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace Orange::Editor::Import
{

namespace
{
constexpr const char* kModelsDir = "assets/Models";

// face-vertex 三元组键 —— tinyobj::index_t 是 (vertex, normal, texcoord)
// 三个 int 索引；我们 dedup 这个三元组 = 一个 unified vertex。负数（-1）
// 表示该 attribute 缺失，处理时按 0 / (0,0) 填空（消费方 Has* 判存）。
struct FaceVertexKey
{
    std::int32_t v;
    std::int32_t n;
    std::int32_t t;

    bool operator==(const FaceVertexKey& o) const noexcept
    {
        return v == o.v && n == o.n && t == o.t;
    }
};

struct FaceVertexKeyHash
{
    std::size_t operator()(const FaceVertexKey& k) const noexcept
    {
        // 简单 mix —— FNV-1a style 起手 + 三段 int 混。importer 一次性
        // 跑完即丢，hash 性能不是 bottleneck，可读优先。
        std::uint64_t h = 14695981039346656037ULL;
        auto mix = [&](std::int32_t x) {
            const auto u = static_cast<std::uint32_t>(x);
            h ^= static_cast<std::uint64_t>(u);
            h *= 1099511628211ULL;
        };
        mix(k.v);
        mix(k.n);
        mix(k.t);
        return static_cast<std::size_t>(h);
    }
};
}  // namespace

ImportResult RunObjImport(std::string_view srcPath, EditorHost& host)
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
        ORANGE_LOG_ERROR("ObjImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    fs::path src(srcPath.begin(), srcPath.end());
    std::error_code ec;
    if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec))
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "source .obj missing or not a regular file";
        ORANGE_LOG_ERROR("ObjImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    // tinyobjloader v1.0.6 legacy API（v2.x 的 ObjReader OO API 与 fast_float
    // 内部依赖在 MSVC /permissive- + /WX 下撞 constexpr 严格检查 C3615，本
    // 仓选 v1.0.6 stable header ~2K 行无 fast_float 干净通过）。triangulate=
    // true 强制 N-gon 三角化，保证我们拿到的 indices 是三角网格。mtl_basedir
    // 走源文件所在目录，确保 .mtl 引用（虽 T3 阶段不消费 material）能正确
    // 解析，避免假报错。v1.0.6 的 err 字段同时承载 warning + error。
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;
    const std::string srcStr(srcPath);
    const std::string baseDir = src.parent_path().generic_string();
    const bool ok = tinyobj::LoadObj(
        &attrib, &shapes, &materials, &err,
        srcStr.c_str(),
        baseDir.empty() ? nullptr : baseDir.c_str(),
        /*triangulate=*/true);
    if (!err.empty())
    {
        // v1 接口把 warn / fatal err 合到同一 string；ok=true 时按 warning
        // 处理（log 但继续），ok=false 时按 fatal 走 error 返回。
        if (ok)
        {
            ORANGE_LOG_WARN("ObjImporter: '{}' tinyobj msg: {}", srcPath, err);
        }
    }
    if (!ok)
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "tinyobj LoadObj failed: " + err;
        ORANGE_LOG_ERROR("ObjImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    // dedup 后的 unified vertex array + index array
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<VertexNormal3>   normals;
    std::vector<std::uint32_t>   indices;

    // 整文件统计：是否任一 face vertex 提供了 normal / uv —— 决定输出
    // mesh 是否 has normals/uvs。tinyobj 把缺失值用 -1 标。
    bool fileHasNormals = false;
    bool fileHasUVs     = false;

    std::unordered_map<FaceVertexKey, std::uint32_t, FaceVertexKeyHash> dedup;
    // 粗略 reserve：典型 obj face/vertex 比例 ≈ 2:1（每个 vertex 共享 ~6 个
    // 三角形）。先按总 face-vertex 数 / 4 reserve dedup，避免 rehash。
    std::size_t totalFaceVerts = 0;
    for (const auto& sh : shapes) { totalFaceVerts += sh.mesh.indices.size(); }
    if (totalFaceVerts > 0)
    {
        dedup.reserve(totalFaceVerts / 4 + 1);
        indices.reserve(totalFaceVerts);
    }

    for (const auto& sh : shapes)
    {
        const auto& meshIndices = sh.mesh.indices;
        for (std::size_t fvi = 0; fvi < meshIndices.size(); ++fvi)
        {
            const auto& fv = meshIndices[fvi];
            const FaceVertexKey key{fv.vertex_index, fv.normal_index, fv.texcoord_index};

            auto it = dedup.find(key);
            if (it == dedup.end())
            {
                const auto newIdx = static_cast<std::uint32_t>(positions.size());
                dedup.emplace(key, newIdx);

                // position：vertex_index 必须 >= 0（tinyobj 保证 face vertex
                // 一定带 pos）。attrib.vertices 是 float[3] interleaved。
                VertexPosition3 p{};
                if (fv.vertex_index >= 0
                    && static_cast<std::size_t>(fv.vertex_index) * 3 + 2 < attrib.vertices.size())
                {
                    p.x = attrib.vertices[fv.vertex_index * 3 + 0];
                    p.y = attrib.vertices[fv.vertex_index * 3 + 1];
                    p.z = attrib.vertices[fv.vertex_index * 3 + 2];
                }
                positions.push_back(p);

                VertexUV2 uv{};
                if (fv.texcoord_index >= 0
                    && static_cast<std::size_t>(fv.texcoord_index) * 2 + 1 < attrib.texcoords.size())
                {
                    uv.u = attrib.texcoords[fv.texcoord_index * 2 + 0];
                    // .obj texcoord origin 在左下；引擎纹理坐标默认左上为 (0,0)
                    // （与 Vulkan 一致）。1 - v 翻转避免贴图上下颠倒。
                    uv.v = 1.0f - attrib.texcoords[fv.texcoord_index * 2 + 1];
                    fileHasUVs = true;
                }
                uvs.push_back(uv);

                VertexNormal3 nrm{};
                if (fv.normal_index >= 0
                    && static_cast<std::size_t>(fv.normal_index) * 3 + 2 < attrib.normals.size())
                {
                    nrm.x = attrib.normals[fv.normal_index * 3 + 0];
                    nrm.y = attrib.normals[fv.normal_index * 3 + 1];
                    nrm.z = attrib.normals[fv.normal_index * 3 + 2];
                    fileHasNormals = true;
                }
                normals.push_back(nrm);

                indices.push_back(newIdx);
            }
            else
            {
                indices.push_back(it->second);
            }
        }
    }

    if (positions.empty() || indices.empty())
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "obj has no triangulated geometry";
        ORANGE_LOG_ERROR("ObjImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    // 没 normals 时清空数组让 MeshLoader::Save 写 hasNormals=0；MeshLoader::
    // Load 会现场 ComputeSmoothNormalsFromTriangles 补算。同理 uvs：没贴图
    // 坐标的 .obj 写 hasUVs=0 而非全 0 占空。
    if (!fileHasNormals) { normals.clear(); }
    if (!fileHasUVs)     { uvs.clear(); }

    // 目标路径：assets/Models/<basename>.mesh + 同目录 source copy 同 basename
    // 保留扩展名。已存在文件 overwrite（reimport 语义）。
    fs::path destDir = kModelsDir;
    fs::create_directories(destDir, ec);
    const std::string stem = src.stem().generic_string();
    fs::path destMesh = destDir / (stem + ".mesh");
    fs::path destObj  = destDir / src.filename();

    fs::copy_file(src, destObj, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        result.status  = ImportStatus::CopyFailed;
        result.message = "copy_file source failed: " + ec.message();
        ORANGE_LOG_ERROR("ObjImporter: '{}' -> '{}': {}",
                         srcPath, destObj.generic_string(), result.message);
        return result;
    }

    // 构造 MeshAsset；统一走 4 参构造，空 UV / 空 normal 由 Save 端写 has=0。
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
        ORANGE_LOG_ERROR("ObjImporter: '{}' -> '{}': {}",
                         srcPath, destMeshStr, result.message);
        return result;
    }

    // AssetRegistry::Load 走 MeshLoader::Load 路径读回 + dedup by path。
    auto loadRes = host.assets.pAssets->Load<MeshAsset>(destMeshStr);
    if (loadRes.IsErr())
    {
        result.status  = ImportStatus::AssetLoadFailed;
        result.message = "AssetRegistry::Load<MeshAsset> failed";
        ORANGE_LOG_ERROR("ObjImporter: '{}' -> '{}': {}",
                         srcPath, destMeshStr, result.message);
        return result;
    }

    // 写 .meta sidecar —— hash 源 .obj 内容（不是转出来的 .mesh，因为
    // .meta 的语义是"源文件指纹"，T5 reimport 时按它判源是否改）。复用
    // TextureMetaV1：v1 阶段 texture / mesh schema 字段完全一致；T5 接通
    // 阶段如果需要拆分（mesh 加额外字段如 import-time mikktspace flag），
    // 再引入 MeshMetaV1 + 共享 helper。
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
        ORANGE_LOG_ERROR("ObjImporter: '{}' -> '{}': {}",
                         srcPath, metaPath, result.message);
        return result;
    }

    result.status   = ImportStatus::Success;
    result.destPath = destMeshStr;
    result.message  = "imported obj mesh";
    ORANGE_LOG_INFO("ObjImporter: '{}' -> '{}' (vtx={} idx={} hash={})",
                    srcPath, destMeshStr,
                    mesh->Positions().size(), mesh->Indices().size(),
                    HashToHexString(meta.sourceHash));
    return result;
}

}  // namespace Orange::Editor::Import
