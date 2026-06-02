#include "GltfImporter.h"

#include "GltfMaterialParse.h"  // ExtractGltfMaterial / BuildMaterialFileData（可独立测试的 material seam）
#include "ImportDispatcher.h"   // ImportTextureToRegistry（复用纹理导入路径）
#include "MeshTangentGen.h"     // GenerateMikkTSpaceTangents（高质量切线）
#include "MetaSidecar.h"
#include "../MaterialFileIO.h"  // WriteMaterialFile（写 .material sidecar）

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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
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

// GltfMatInfo / ResolveTextureSource / ExtractGltfMaterial / BuildMaterialFileData
// 已抽到 import/GltfMaterialParse.{h,cpp}（可独立 headless 测试的 material seam）。

// 解码 data: URI 的 base64 负载（payload 指向逗号后的 base64 串）。失败返回空。
std::vector<unsigned char> DecodeBase64(const char* payload)
{
    const auto charValue = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') { return c - 'A'; }
        if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
        if (c >= '0' && c <= '9') { return c - '0' + 52; }
        if (c == '+') { return 62; }
        if (c == '/') { return 63; }
        return -1;  // '=' padding / 空白 / 非法字符
    };

    std::vector<unsigned char> out;
    int accum = 0;
    int bits = 0;
    for (const char* p = payload; *p != '\0'; ++p)
    {
        const int v = charValue(*p);
        if (v < 0) { continue; }  // 跳过 padding / 空白
        accum = (accum << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((accum >> bits) & 0xFF));
        }
    }
    return out;
}

// 由 mime_type / magic 推断内嵌图像扩展名（不含点）。默认 png。
std::string EmbeddedImageExtension(const cgltf_image& image,
                                   const unsigned char* bytes, std::size_t size)
{
    if (image.mime_type != nullptr)
    {
        if (std::strstr(image.mime_type, "jpeg") != nullptr ||
            std::strstr(image.mime_type, "jpg") != nullptr)
        {
            return "jpg";
        }
        if (std::strstr(image.mime_type, "png") != nullptr)
        {
            return "png";
        }
    }
    // 退化：JPEG magic FF D8 FF，其余按 PNG。
    if (size >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
    {
        return "jpg";
    }
    return "png";
}

// 取出内嵌图像原始字节：优先 GLB buffer_view，其次 data: URI。拿不到返回空。
std::vector<unsigned char> ReadEmbeddedImageBytes(const cgltf_image& image)
{
    if (image.buffer_view != nullptr)
    {
        const cgltf_size size = image.buffer_view->size;
        const std::uint8_t* p = cgltf_buffer_view_data(image.buffer_view);
        if (p != nullptr && size > 0)
        {
            return std::vector<unsigned char>(p, p + size);
        }
    }
    if (image.uri != nullptr && std::strncmp(image.uri, "data:", 5) == 0)
    {
        const char* comma = std::strchr(image.uri, ',');
        if (comma != nullptr)
        {
            return DecodeBase64(comma + 1);
        }
    }
    return {};
}

// 把内嵌图像写到 destDirStr（assets/Models/<stem>/），返回写出的磁盘路径。
// 命名：优先 image->name，空则 image_<idx>；清洗路径分隔符防写到目录外。失败返回空。
// 注意：仅写盘，不入 registry —— 调用方拿到路径后与外部贴图走同一条
// ImportTextureToRegistry co-locate 流程（copy 到同目录覆盖自身 + ORTX + .meta +
// Load），避免二次实现解码 / 注册逻辑。
std::string ExtractEmbeddedImageToDisk(const cgltf_image& image, cgltf_size imageIndex,
                                       const std::string& destDirStr)
{
    namespace fs = std::filesystem;

    const std::vector<unsigned char> bytes = ReadEmbeddedImageBytes(image);
    if (bytes.empty())
    {
        ORANGE_LOG_WARN("GltfImporter: embedded image [{}] has no bytes, skip", imageIndex);
        return {};
    }

    const std::string ext = EmbeddedImageExtension(image, bytes.data(), bytes.size());

    std::string baseName =
        (image.name != nullptr && image.name[0] != '\0')
            ? std::string(image.name)
            : ("image_" + std::to_string(imageIndex));
    for (char& c : baseName)
    {
        if (c == '/' || c == '\\') { c = '_'; }
    }

    std::error_code ec;
    fs::path destDir(destDirStr);
    fs::create_directories(destDir, ec);
    const fs::path dst = destDir / (baseName + "." + ext);

    std::ofstream ofs(dst, std::ios::binary);
    if (!ofs)
    {
        ORANGE_LOG_WARN("GltfImporter: open embedded texture for write failed: {}",
                        dst.generic_string());
        return {};
    }
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    ofs.close();
    return dst.generic_string();
}
}  // namespace

ImportResult RunGltfImportToRegistry(std::string_view srcPath,
                                     ::Orange::Engine::Asset::AssetRegistry& registry,
                                     const MaterialRegisterFn& onMaterialWritten)
{
    using ::Orange::Engine::Asset::AssetRegistry;
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::MeshLoader;
    using ::Orange::Engine::Asset::VertexNormal3;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    namespace fs = std::filesystem;

    ImportResult result{};

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
        // 每模型一个子目录 assets/Models/<stem>/<stem>.mesh（与主路径一致）。
        const std::string earlyDestMesh =
            (fs::path(kModelsDir) / earlyStem / (earlyStem + ".mesh")).generic_string();
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
    // 第一个带 material 的三角 primitive 的 cgltf_material —— 取它解析 slot 0
    // 的 PBR 通道，并作为 RenderableComponent.materialInstance 兜底。
    const cgltf_material* firstMat = nullptr;

    // 多 material 收集：按"首次出现顺序"去重每个 primitive 的 cgltf_material；
    // orderedMats 的下标即 materialSlot。nullptr（无 material 的 primitive）也
    // 收进来占一个 slot —— 它对应引擎默认材质，落地端不写专属 .material。
    // 同一 cgltf_material* 复用同一 slot（去重，避免重复 .material 文件）。
    std::vector<const cgltf_material*> orderedMats;
    auto slotForMaterial = [&orderedMats](const cgltf_material* m) -> std::uint32_t {
        for (std::size_t i = 0; i < orderedMats.size(); ++i)
        {
            if (orderedMats[i] == m) { return static_cast<std::uint32_t>(i); }
        }
        orderedMats.push_back(m);
        return static_cast<std::uint32_t>(orderedMats.size() - 1);
    };

    // per-primitive 边界（统一 index buffer 全局区间）+ 归属 slot。下面在
    // orderedMats.size() <= 1 时整段丢弃（退化回单 mesh 单 material 路径，
    // 向后兼容现有所有单材质导入，零行为变化）。
    std::vector<::Orange::Engine::Asset::SubMesh> subMeshes;

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
            if (firstMat == nullptr && prim.material != nullptr)
            {
                firstMat = prim.material;
            }

            // 本 primitive 在合并后 index buffer 里的起点 + 归属 slot。indexCount
            // 在下面 append 完索引后回填（用 indices.size() 差值，自动覆盖 indexed
            // / 非 indexed 两条路径）。
            const std::uint32_t subMeshIndexOffset =
                static_cast<std::uint32_t>(indices.size());
            const std::uint32_t subMeshSlot = slotForMaterial(prim.material);

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

            // 回填本 primitive 在合并后 index buffer 的区间长度（覆盖 indexed /
            // 非 indexed 两条路径）。仅当确实 append 了索引才记录 sub-mesh。
            const std::uint32_t subMeshIndexCount =
                static_cast<std::uint32_t>(indices.size()) - subMeshIndexOffset;
            if (subMeshIndexCount > 0)
            {
                subMeshes.push_back(
                    {subMeshIndexOffset, subMeshIndexCount, subMeshSlot});
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

    // 在 cgltf_free 之前从每个 material（orderedMats，按 slot 顺序）抽出 PBR
    // 通道（外部贴图源路径 + 内嵌 image 下标 + 标量 factor + occlusionStrength）。
    // 外部贴图源路径相对 gltf 所在目录解析；内嵌图像（.glb buffer_view /
    // data: URI）先解析出 images[] 下标，在 cgltf_free 之前取字节落盘并回填到
    // matInfo 的 *Src。逻辑在 GltfMaterialParse.cpp（与测试共用同一份）。
    // slotMatInfos[i] 对应 materialSlot i；nullptr material（无 material 的
    // primitive 占位 slot）的 info.present==false，落地端用引擎默认材质兜底。
    //
    // 内嵌贴图提取必须在 cgltf_free 之前做（要读 buffer_view / data: 字节）。把
    // 内嵌 image 写到模型自己的 assets/Models/<stem>/ 目录，回填 *Src 为磁盘
    // 路径，之后与外部贴图走同一条 ImportTextureToRegistry co-locate 流程。同一
    // image 被多个槽 / 多个 material 引用时只写盘一次（embeddedCache 跨 material
    // 共享）。
    const std::string embeddedDir =
        (fs::path(kModelsDir) / src.stem().generic_string()).generic_string();
    std::map<int, std::string> embeddedCache;  // images[] 下标 → 落盘路径
    auto resolveEmbedded = [&](int imageIndex) -> std::string {
        if (imageIndex < 0 ||
            static_cast<cgltf_size>(imageIndex) >= data->images_count)
        {
            return {};
        }
        auto it = embeddedCache.find(imageIndex);
        if (it != embeddedCache.end())
        {
            return it->second;  // 已写盘，复用
        }
        const std::string path = ExtractEmbeddedImageToDisk(
            data->images[imageIndex], static_cast<cgltf_size>(imageIndex),
            embeddedDir);
        embeddedCache[imageIndex] = path;
        return path;
    };

    // 单 material（含全无 material）退化路径下 orderedMats 仍至少有一项；用
    // firstMat 当 slot 0 兜底保证即便 orderedMats[0] 是 nullptr（首 primitive
    // 无 material 但后续 primitive 有）时 slot 0 仍能拿到一个真实 material。
    std::vector<GltfMatInfo> slotMatInfos;
    slotMatInfos.reserve(orderedMats.empty() ? 1 : orderedMats.size());
    // 与 slotMatInfos 同序抽出每个 slot 的 material name —— 必须在 cgltf_free
    // 之前做：orderedMats 存的是 cgltf_material* 裸指针，cgltf_free(data) 后全部
    // 悬空，写 .material 循环里（slot>=1 的 sanitizedMaterialName）不能再解引用
    // orderedMats[slot]->name（use-after-free → SEGFAULT）。空 name 留空串，
    // sanitizedMaterialName 退化用 slot 序号。
    std::vector<std::string> slotMatNames;
    slotMatNames.reserve(orderedMats.empty() ? 1 : orderedMats.size());
    if (orderedMats.empty())
    {
        // 理论上不会发生（前面已 return 空几何），保险起见给一个 firstMat slot。
        orderedMats.push_back(firstMat);
    }
    for (const cgltf_material* m : orderedMats)
    {
        slotMatNames.push_back(
            (m != nullptr && m->name != nullptr && m->name[0] != '\0')
                ? std::string(m->name)
                : std::string{});
        GltfMatInfo info = ExtractGltfMaterial(m, src.parent_path(), data);
        if (info.present)
        {
            // 仅当外部 uri 解析为空（即该槽不是外部文件）才尝试内嵌提取。
            if (info.baseColorSrc.empty())
            {
                info.baseColorSrc = resolveEmbedded(info.baseColorImageIndex);
            }
            if (info.normalSrc.empty())
            {
                info.normalSrc = resolveEmbedded(info.normalImageIndex);
            }
            if (info.metalRoughSrc.empty())
            {
                info.metalRoughSrc = resolveEmbedded(info.metalRoughImageIndex);
            }
            if (info.aoSrc.empty())
            {
                info.aoSrc = resolveEmbedded(info.aoImageIndex);
            }
            if (info.emissiveSrc.empty())
            {
                info.emissiveSrc = resolveEmbedded(info.emissiveImageIndex);
            }
        }
        slotMatInfos.push_back(std::move(info));
    }

    cgltf_free(data);  // 不再需要 cgltf 内部结构，data 已经拷出来

    // 整文件无 normal/uv 时清空数组让 MeshLoader::Save 写 has=0；混合情况
    // （部分 primitive 有法线、部分没）走 has=true 路径但缺失部分已被填充
    // (0,1,0)——Load 端不会重算，渲染端可能偏。v1.x 长尾如需改进则在
    // Importer 内对 missing-normal primitive 单独跑 ComputeSmoothNormals
    // local（当前 v1.1 不做）。
    if (!fileHasNormals) { normals.clear(); }
    if (!fileHasUVs)     { uvs.clear(); }

    // 每模型一个子目录 assets/Models/<stem>/ —— mesh / material / source copy /
    // 该模型的贴图全部 co-locate 进去，避免贴图被甩到 assets/Textures/ 后跨
    // 目录找（用户反馈）。下面 material 段的贴图 resolver 把贴图也写进同一 modelDir。
    // .gltf 的外部 .bin 仍只做单文件 copy 起步，用户可手动同步 .bin。
    const std::string stem = src.stem().generic_string();
    fs::path destDir = fs::path(kModelsDir) / stem;
    fs::create_directories(destDir, ec);
    const std::string modelDirStr = destDir.generic_string();
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

    // MikkTSpace 高质量切线 —— 仅当 UV + normal 都在时可算（glTF 大多两者
    // 齐备）。就地 re-weld positions/uvs/normals/indices（顶点数可能增），故
    // 必须在下面 std::move 进构造函数之前调；结果构造后 SetTangents 注入。
    // 缺 UV/normal → false，留给引擎 Load 端 Lengyel fallback。
    std::vector<::Orange::Engine::Asset::VertexTangent4> tangents;
    bool haveTangents = false;
    if (!uvs.empty() && !normals.empty())
    {
        haveTangents = Orange::Editor::Import::GenerateMikkTSpaceTangents(
            positions, uvs, normals, indices, tangents);
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

    // MikkTSpace 切线注入 → .mesh v4 写出 tangent 段；Load 读回即有切线。
    if (haveTangents)
    {
        mesh->SetTangents(std::move(tangents));
    }

    // 多 material（orderedMats.size() > 1）才写 sub-mesh 段；单 material（含
    // 全无 material）退化回整 mesh 单段 / slot 0，subMeshes 留空 ——
    // .mesh 仍是 v5 但 HasSubMeshes()==false，与单材质导入路径字节兼容、
    // 渲染端走整 mesh 单 material 路径，现有所有单材质导入零行为变化。
    const bool multiMaterial = orderedMats.size() > 1;
    if (multiMaterial)
    {
        mesh->SetSubMeshes(std::move(subMeshes));
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

    auto loadRes = registry.Load<MeshAsset>(destMeshStr);
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
    // .meta 实际落盘推迟到 material 段之后 —— 多 material 时要把按 slot 排列的
    // .material 路径写进 subMeshMaterials 段（drop .mesh 到 entity 时回读它挂
    // SubMeshMaterialsComponent）。单 material 时该段为空，.meta 字节与历史一致。

    // 解析出 material 通道时 import 各贴图 + 写 .material sidecar，按 slot 顺序
    // 逐个落盘。.material 落 .mesh 同目录（assets/Models/<stem>/），templateName=pbr，
    // texture 槽 binding 与 pbr set 1 对齐（0 baseColor / 1 normal / 2 metalRough /
    // 3 ao），uniform 覆盖 uBaseColor=baseColorFactor、
    // uMRA=(metallic, roughness, occlusionStrength, 0)。
    //
    // 命名规约（向后兼容）：slot 0 仍写 <stem>.material（与历史单材质导入完全
    // 一致，单 material 模型字节 / 路径不变）；slot >= 1 写
    // <stem>_<materialName 或 slot 序号>.material（清洗路径分隔符防写到目录外，
    // 同名冲突时退化用序号）。某 slot 的 material info.present==false（无
    // material 的 primitive 占位 slot）→ 不写 .material，materialPaths 该项留空，
    // 落地端用引擎默认材质兜底。
    auto resolver = [&](const std::string& srcTexPath) -> std::string {
        ImportResult tr = ImportTextureToRegistry(srcTexPath, registry, modelDirStr);
        if (tr.status == ImportStatus::Success && !tr.destPath.empty())
        {
            return tr.destPath;
        }
        ORANGE_LOG_WARN("GltfImporter: material 贴图 '{}' import 失败，跳过该槽",
                        srcTexPath);
        return {};
    };

    // 清洗 material name 成文件名安全片段；空则退化用 slot 序号。name 取自
    // cgltf_free 之前缓存的 slotMatNames（不能解引用已悬空的 orderedMats）。
    auto sanitizedMaterialName = [&](std::size_t slot) -> std::string {
        std::string name = (slot < slotMatNames.size() && !slotMatNames[slot].empty())
                               ? slotMatNames[slot]
                               : ("mat" + std::to_string(slot));
        for (char& c : name)
        {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|')
            {
                c = '_';
            }
        }
        return name;
    };

    result.materialPaths.assign(slotMatInfos.size(), std::string{});
    std::set<std::string> usedMatFileNames;  // 同名 material 去重，退化用序号
    for (std::size_t slot = 0; slot < slotMatInfos.size(); ++slot)
    {
        const GltfMatInfo& info = slotMatInfos[slot];
        if (!info.present)
        {
            continue;  // 无 material 的 slot：留空，落地端走默认材质
        }

        // slot 0 用 <stem>.material（历史命名）；其余 <stem>_<name>.material。
        std::string fileStem;
        if (slot == 0)
        {
            fileStem = stem;
        }
        else
        {
            std::string nm = sanitizedMaterialName(slot);
            fileStem = stem + "_" + nm;
            // 同名冲突（两个 material 同名）→ 退化拼 slot 序号，保证唯一。
            if (usedMatFileNames.count(fileStem) != 0)
            {
                fileStem = stem + "_" + nm + "_" + std::to_string(slot);
            }
        }
        usedMatFileNames.insert(fileStem);

        Material::MaterialFileData mdata = BuildMaterialFileData(info, resolver);
        const std::string matPath =
            (destDir / (fileStem + ".material")).generic_string();
        if (Material::WriteMaterialFile(matPath, mdata))
        {
            ORANGE_LOG_INFO("GltfImporter: wrote material '{}' (slot={} textures={})",
                            matPath, slot, mdata.textures.size());
            result.materialPaths[slot] = matPath;
            // 写出 .material 后回调注册（仅 GUI 路径注入；headless 传空跳过）。
            // 否则刚导入的 .material 不在编辑器 namedMaterialInstances 表里，
            // Renderable 的 Material 字段(materialSet 只查表不 lazy load)选不到、
            // 赋不上。回调内（GUI = EnsureMaterialInstance）做 ReadMaterialFile +
            // CreateInstance + ApplyDataToInstance + own 到 userMaterials + 写
            // namedMaterialInstances。headless 路径不需要编辑器缓存，材质文件已
            // 照常落盘。
            if (onMaterialWritten)
            {
                onMaterialWritten(matPath);
            }
        }
        else
        {
            ORANGE_LOG_WARN("GltfImporter: WriteMaterialFile '{}' 失败", matPath);
        }
    }

    // 多 material（mesh 真带 sub-mesh 分段）才把按 slot 排列的 .material 路径写
    // 进 .meta 的 subMeshMaterials 段，供 drop .mesh 到 entity 时挂
    // SubMeshMaterialsComponent。单 material（materialPaths.size() <= 1）留空，
    // .meta 维持 v1.0 字节（向后兼容，零行为变化）。
    if (multiMaterial)
    {
        meta.subMeshMaterials = result.materialPaths;
    }
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

// GUI 包装 RunGltfImport(host)（注入 EnsureMaterialInstance 注册回调）在
// ImportHostBridge.cpp —— 把所有引用 EditorHost / EnsureMaterialInstance 的薄壳
// 集中到那个单独 TU，让本 TU（含 cgltf IMPLEMENTATION）保持 headless 可链。

}  // namespace Orange::Editor::Import
