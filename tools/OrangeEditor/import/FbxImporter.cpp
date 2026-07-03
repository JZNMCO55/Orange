#include "FbxImporter.h"

#include "FbxAxisConverter.h" // AxisConverter / MakeAxisConverter（与 scene importer 共用）
#include "FbxMaterialParse.h" // BuildFbxMaterialFileData / StageFbxTextureSources（共用）
#include "ImportDispatcher.h" // ImportTextureToRegistry（复用纹理导入路径）
#include "MeshTangentGen.h"   // GenerateMikkTSpaceTangents（高质量切线）
#include "MetaSidecar.h"
#include "../MaterialFileIO.h" // WriteMaterialFile（写 .material sidecar）

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/core/Log.h>

// OpenFBX vendor 头 —— 仅取声明（ofbx.cpp / libdeflate.c 作为独立 TU 编译，由
// CMake 接进 OrangeEditor / 测试 target，并 per-TU 压 warning，不在本 TU expand）。
// 与 cgltf 不同：OpenFBX 不是单 header，没有 IMPLEMENTATION 宏。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244) // narrowing
#pragma warning(disable : 4267) // size_t → smaller int
#endif
#include "ofbx.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Orange::Editor::Import
{

    namespace
    {
        constexpr const char* kModelsDir = "assets/Models";

        // 引擎侧顶点类型别名（与 Gltf/Obj importer 一致）。
        using ::Orange::Engine::Asset::SubMesh;
        using ::Orange::Engine::Asset::VertexNormal3;
        using ::Orange::Engine::Asset::VertexPosition3;
        using ::Orange::Engine::Asset::VertexTangent4;
        using ::Orange::Engine::Asset::VertexUV2;

        // AxisConverter / MakeAxisConverter 已迁 FbxAxisConverter.{h,cpp}（与
        // FbxSceneImporter 共用，保证两条导入路径换轴一致）。

        // 清洗成文件名安全片段（去路径分隔符 / 非法字符）。空 → 退化用 fallback。
        std::string SanitizeName(const std::string& raw, const std::string& fallback)
        {
            std::string name = raw.empty() ? fallback : raw;
            for (char& c : name)
            {
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                    c == '"' || c == '<' || c == '>' || c == '|')
                {
                    c = '_';
                }
            }
            return name;
        }

        // ResolveFbxTexture / BuildFbxMaterialFileData / StageFbxTextureSources 已迁
        // FbxMaterialParse.{h,cpp}（与 FbxSceneImporter 共用 material 提取）。
    } // namespace

    ImportResult RunFbxImportToRegistry(std::string_view                        srcPath,
                                        ::Orange::Engine::Asset::AssetRegistry& registry,
                                        const MaterialRegisterFn&               onMaterialWritten,
                                        float                                   importScale)
    {
        using ::Orange::Engine::Asset::AssetRegistry;
        using ::Orange::Engine::Asset::MeshAsset;
        using ::Orange::Engine::Asset::MeshLoader;

        namespace fs = std::filesystem;

        ImportResult result{};

        fs::path        src(srcPath.begin(), srcPath.end());
        std::error_code ec;
        if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec))
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = "source .fbx missing or not a regular file";
            ORANGE_LOG_ERROR("FbxImporter: '{}': {}", srcPath, result.message);
            return result;
        }

        // hash 增量短路：源 hash 与目标 .meta sourceHash 匹配则跳过全套（与 gltf/obj
        // importer 一致）。
        const auto earlyHashOpt = ComputeFileHashFnv1a(srcPath);
        if (earlyHashOpt.has_value())
        {
            const std::string earlyStem = src.stem().generic_string();
            const std::string earlyDestMesh =
                (fs::path(kModelsDir) / earlyStem / (earlyStem + ".mesh")).generic_string();
            if (MetaSourceHashMatches(earlyDestMesh, earlyHashOpt.value()))
            {
                result.status   = ImportStatus::Success;
                result.destPath = earlyDestMesh;
                result.message  = "fbx unchanged, skipped reimport";
                ORANGE_LOG_INFO("FbxImporter: '{}' unchanged (hash={}), skip",
                                srcPath, HashToHexString(earlyHashOpt.value()));
                return result;
            }
        }

        // 读整个 .fbx 到内存（OpenFBX load 接 buffer + size，不接路径）。
        std::ifstream ifs(src, std::ios::binary);
        if (!ifs)
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = "open .fbx for read failed";
            ORANGE_LOG_ERROR("FbxImporter: '{}': {}", srcPath, result.message);
            return result;
        }
        std::vector<ofbx::u8> bytes((std::istreambuf_iterator<char>(ifs)),
                                    std::istreambuf_iterator<char>());
        ifs.close();
        if (bytes.empty())
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = ".fbx is empty";
            ORANGE_LOG_ERROR("FbxImporter: '{}': {}", srcPath, result.message);
            return result;
        }

        // 解析：忽略一切动画 / skin / 灯光 / 相机 —— MVP 只取静态几何 + 材质 + 贴图。
        // KEEP geometry / materials / textures；其余 IGNORE 省内存 + 加速。
        const ofbx::LoadFlags flags =
            ofbx::LoadFlags::IGNORE_BLEND_SHAPES |
            ofbx::LoadFlags::IGNORE_CAMERAS |
            ofbx::LoadFlags::IGNORE_LIGHTS |
            ofbx::LoadFlags::IGNORE_SKIN |
            ofbx::LoadFlags::IGNORE_BONES |
            ofbx::LoadFlags::IGNORE_ANIMATIONS |
            ofbx::LoadFlags::IGNORE_POSES |
            ofbx::LoadFlags::IGNORE_LIMBS;

        ofbx::IScene* scene = ofbx::load(bytes.data(), bytes.size(),
                                         static_cast<ofbx::u16>(flags));
        if (scene == nullptr)
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = std::string("ofbx::load failed: ") + ofbx::getError();
            ORANGE_LOG_ERROR("FbxImporter: '{}': {}", srcPath, result.message);
            return result;
        }

        const AxisConverter conv = MakeAxisConverter(scene->getGlobalSettings(), importScale);

        // unified arrays —— 所有 mesh / 所有 material partition 的顶点拼接；indices
        // 同步 offset 调整。FBX geometry 是 per-face-vertex（VecNAttributes），无原生
        // index dedup —— 我们直接展开成 unindexed 顶点（每三角 3 个独立顶点），index
        // 即 0..N-1。dedup 留给下游（MikkTSpace re-weld + 渲染端）；MVP 优先正确性。
        std::vector<VertexPosition3> positions;
        std::vector<VertexUV2>       uvs;
        std::vector<VertexNormal3>   normals;
        std::vector<std::uint32_t>   indices;

        bool fileHasUVs     = false;
        bool fileHasNormals = false;

        // 多 material 收集：跨所有 mesh 用 (Material* 指针) 首次出现顺序去重成全局
        // slot。orderedMats 的下标即 materialSlot。nullptr（无 material 的 partition）
        // 也占一个 slot → 引擎默认材质。
        std::vector<const ofbx::Material*> orderedMats;
        auto                               slotForMaterial = [&orderedMats](const ofbx::Material* m) -> std::uint32_t
        {
            for (std::size_t i = 0; i < orderedMats.size(); ++i)
            {
                if (orderedMats[i] == m)
                {
                    return static_cast<std::uint32_t>(i);
                }
            }
            orderedMats.push_back(m);
            return static_cast<std::uint32_t>(orderedMats.size() - 1);
        };

        // per-partition 边界（统一 index buffer 全局区间）+ 归属 slot。多 material 时
        // 据此建 sub-mesh；单 / 无 material 时整段丢弃退化回整 mesh 单段。
        std::vector<SubMesh> subMeshes;

        // 三角化临时缓冲（ofbx::triangulate 对凹多边形需要）。
        std::vector<int> triBuf;

        const int meshCount = scene->getMeshCount();
        for (int mi = 0; mi < meshCount; ++mi)
        {
            const ofbx::Mesh* mesh = scene->getMesh(mi);
            if (mesh == nullptr)
            {
                continue;
            }

            const ofbx::GeometryData&  geom    = mesh->getGeometryData();
            const ofbx::Vec3Attributes posAttr = geom.getPositions();
            if (posAttr.values == nullptr || posAttr.count == 0)
            {
                continue; // 无几何（可能纯 transform node）
            }
            const ofbx::Vec3Attributes nrmAttr = geom.getNormals();
            const ofbx::Vec2Attributes uvAttr  = geom.getUVs(0);
            const bool                 hasN    = (nrmAttr.values != nullptr && nrmAttr.count == posAttr.count);
            const bool                 hasUV   = (uvAttr.values != nullptr && uvAttr.count == posAttr.count);

            const int partitionCount = geom.getPartitionCount();
            for (int pi = 0; pi < partitionCount; ++pi)
            {
                // partition 下标 == 该 mesh 的 material slot（OpenFBX 把每个 polygon 按
                // material_index 放进 partitions[material_index]）。映射到 mesh 的
                // getMaterial(pi)（可能为 null）。
                const ofbx::Material* mat =
                    (pi < mesh->getMaterialCount()) ? mesh->getMaterial(pi) : nullptr;
                const std::uint32_t subMeshSlot = slotForMaterial(mat);
                const std::uint32_t subMeshIndexOffset =
                    static_cast<std::uint32_t>(indices.size());

                const ofbx::GeometryPartition partition = geom.getPartition(pi);
                for (int poly = 0; poly < partition.polygon_count; ++poly)
                {
                    const ofbx::GeometryPartition::Polygon& polygon =
                        partition.polygons[poly];
                    if (polygon.vertex_count < 3)
                    {
                        continue;
                    }

                    // 三角化：tri_indices 装的是 VecNAttributes::indices 空间下标
                    // （per-face-vertex），用 posAttr.get(idx) 解析实际顶点。
                    triBuf.resize(static_cast<std::size_t>(
                        (polygon.vertex_count - 2) * 3 + 8));
                    std::vector<int> tmp(
                        static_cast<std::size_t>(polygon.vertex_count));
                    const ofbx::u32 triIdxCount = ofbx::triangulate(
                        geom, polygon, triBuf.data(), tmp.data());

                    for (ofbx::u32 t = 0; t < triIdxCount; ++t)
                    {
                        const int        fvIndex = triBuf[t]; // face-vertex 空间下标
                        const ofbx::Vec3 p       = posAttr.get(fvIndex);
                        positions.push_back(conv.Position(p.x, p.y, p.z));

                        if (hasN)
                        {
                            const ofbx::Vec3 n = nrmAttr.get(fvIndex);
                            normals.push_back(conv.Normal(n.x, n.y, n.z));
                            fileHasNormals = true;
                        }
                        else
                        {
                            normals.push_back(VertexNormal3{0.0f, 1.0f, 0.0f});
                        }

                        if (hasUV)
                        {
                            const ofbx::Vec2 uv = uvAttr.get(fvIndex);
                            // FBX UV 原点左下；引擎纹理坐标默认左上为 (0,0)（与
                            // Vulkan 一致）。1-v 翻转避免贴图上下颠倒（同 .obj）。
                            uvs.push_back({static_cast<float>(uv.x),
                                           1.0f - static_cast<float>(uv.y)});
                            fileHasUVs = true;
                        }
                        else
                        {
                            uvs.push_back(VertexUV2{0.0f, 0.0f});
                        }

                        // unindexed：index 即顺序递增。
                        indices.push_back(static_cast<std::uint32_t>(positions.size() - 1));
                    }
                }

                const std::uint32_t subMeshIndexCount =
                    static_cast<std::uint32_t>(indices.size()) - subMeshIndexOffset;
                if (subMeshIndexCount > 0)
                {
                    subMeshes.push_back({subMeshIndexOffset, subMeshIndexCount, subMeshSlot});
                }
            }
        }

        if (positions.empty() || indices.empty())
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = "fbx has no triangulated geometry with positions";
            ORANGE_LOG_ERROR("FbxImporter: '{}': {}", srcPath, result.message);
            scene->destroy();
            return result;
        }

        // 在 scene->destroy() 之前抽出每个 slot 的 material name + PBR 通道。FBX
        // Material* 在 destroy 后全部悬空，名字 / 贴图必须现在缓存。先存指针/名字，
        // 真正写 .material 在 destroy 之后（用缓存的 name + 现在抽好的 MaterialFileData
        // 蓝本——但贴图 resolver 要在 import 阶段才跑，故这里只缓存 name + Material*
        // 不可，改为：现在就把 textures 源路径解析出来不依赖 registry，留 import 后落盘）。
        //
        // 简化：在 destroy 之前把每个 slot 的"material 名 + scalar uniform + 贴图源
        // 磁盘路径列表"全抽进一个轻量结构，destroy 后只做 ImportTexture + WriteMaterialFile。
        struct FbxMatStaged
        {
            bool        present{false};
            std::string name;
            // 已解析好的 scalar uniform + 贴图源磁盘路径（binding → src path）。
            Material::MaterialFileData                         scalarData;
            std::vector<std::pair<std::uint32_t, std::string>> textureSrc;
        };
        const fs::path            fbxDir = src.parent_path();
        std::vector<FbxMatStaged> staged;
        staged.reserve(orderedMats.empty() ? 1 : orderedMats.size());
        if (orderedMats.empty())
        {
            orderedMats.push_back(nullptr); // 保底一个默认材质 slot
        }
        for (const ofbx::Material* m : orderedMats)
        {
            FbxMatStaged s;
            if (m != nullptr)
            {
                s.present = true;
                // OpenFBX 把 FBX object 名填进 Object::name（char[128]）。
                if (m->name[0] != '\0')
                {
                    s.name = m->name;
                }
                // scalar uniform（不含贴图，贴图 resolver 留 import 后）。传 null
                // resolver → BuildFbxMaterialFileData 只填 scalar，贴图源单独抽。
                s.scalarData = BuildFbxMaterialFileData(m, fbxDir, nullptr);
                // 贴图源路径（不入 registry，仅磁盘路径），destroy 后落盘。
                s.textureSrc = StageFbxTextureSources(m, fbxDir);
            }
            staged.push_back(std::move(s));
        }

        scene->destroy(); // FBX 内部结构不再需要，几何 / 材质蓝本已拷出

        // 整文件无 normal/uv 时清空数组让 MeshLoader::Save 写 has=0（loader 端
        // ComputeSmooth 重算）；混合（部分有/部分无）走 has=true 路径，缺失部分已
        // 填占位。FBX 一般法线齐备（DCC 导出默认带 normal）。
        if (!fileHasNormals)
        {
            normals.clear();
        }
        if (!fileHasUVs)
        {
            uvs.clear();
        }

        // 每模型一个子目录 assets/Models/<stem>/。
        const std::string stem    = src.stem().generic_string();
        fs::path          destDir = fs::path(kModelsDir) / stem;
        fs::create_directories(destDir, ec);
        const std::string modelDirStr = destDir.generic_string();
        fs::path          destMesh    = destDir / (stem + ".mesh");
        fs::path          destFbx     = destDir / src.filename();

        fs::copy_file(src, destFbx, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            result.status  = ImportStatus::CopyFailed;
            result.message = "copy_file source failed: " + ec.message();
            ORANGE_LOG_ERROR("FbxImporter: '{}' -> '{}': {}",
                             srcPath, destFbx.generic_string(), result.message);
            return result;
        }

        // MikkTSpace 高质量切线 —— 仅当 UV + normal 都在时可算。会就地 re-weld
        // positions/uvs/normals/indices（顶点数可能变），故在 std::move 进构造前调。
        std::vector<VertexTangent4> tangents;
        bool                        haveTangents = false;
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

        if (haveTangents)
        {
            mesh->SetTangents(std::move(tangents));
        }

        // 多 material（orderedMats.size() > 1）才写 sub-mesh 段；单 / 全无 material
        // 退化整 mesh 单段（与单材质导入字节兼容）。
        const bool multiMaterial = orderedMats.size() > 1;
        if (multiMaterial)
        {
            mesh->SetSubMeshes(std::move(subMeshes));
        }

        const std::string destMeshStr = destMesh.generic_string();
        auto              saveRes     = MeshLoader::Save(destMeshStr, *mesh);
        if (saveRes.IsErr())
        {
            result.status  = ImportStatus::AssetLoadFailed;
            result.message = "MeshLoader::Save failed";
            ORANGE_LOG_ERROR("FbxImporter: '{}' -> '{}': {}",
                             srcPath, destMeshStr, result.message);
            return result;
        }

        auto loadRes = registry.Load<MeshAsset>(destMeshStr);
        if (loadRes.IsErr())
        {
            result.status  = ImportStatus::AssetLoadFailed;
            result.message = "AssetRegistry::Load<MeshAsset> failed";
            ORANGE_LOG_ERROR("FbxImporter: '{}' -> '{}': {}",
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
        meta.sourcePath            = src.generic_string();
        meta.sourceHash            = hashOpt.value();
        meta.handleId              = 0;
        const std::string metaPath = MetaPathFor(destMeshStr);

        // 贴图 resolver：ImportTexture co-locate 到模型目录（与 gltf/obj 同款）。
        auto resolver = [&](const std::string& srcTexPath) -> std::string
        {
            ImportResult tr = ImportTextureToRegistry(srcTexPath, registry, modelDirStr);
            if (tr.status == ImportStatus::Success && !tr.destPath.empty())
            {
                return tr.destPath;
            }
            ORANGE_LOG_WARN("FbxImporter: material 贴图 '{}' import 失败，跳过该槽",
                            srcTexPath);
            return {};
        };

        // 按 slot 写 .material（命名规约同 gltf/obj：slot 0 = <stem>.material，
        // slot>=1 = <stem>_<matname>.material，同名冲突拼序号）。
        result.materialPaths.assign(staged.size(), std::string{});
        std::set<std::string> usedMatFileNames;
        for (std::size_t slot = 0; slot < staged.size(); ++slot)
        {
            const FbxMatStaged& s = staged[slot];
            if (!s.present)
            {
                continue; // 无 material 的 slot：留空，落地端走默认材质
            }

            std::string fileStem;
            if (slot == 0)
            {
                fileStem = stem;
            }
            else
            {
                const std::string nm = SanitizeName(s.name, "mat" + std::to_string(slot));
                fileStem             = stem + "_" + nm;
                if (usedMatFileNames.count(fileStem) != 0)
                {
                    fileStem = stem + "_" + nm + "_" + std::to_string(slot);
                }
            }
            usedMatFileNames.insert(fileStem);

            // scalar 蓝本已抽好；贴图源在此经 resolver 落盘后入 textures 数组。
            Material::MaterialFileData mdata = s.scalarData;
            for (const auto& [binding, texSrc] : s.textureSrc)
            {
                const std::string dest = resolver(texSrc);
                if (!dest.empty())
                {
                    mdata.textures.push_back({binding, dest});
                }
            }

            const std::string matPath =
                (destDir / (fileStem + ".material")).generic_string();
            if (Material::WriteMaterialFile(matPath, mdata))
            {
                ORANGE_LOG_INFO("FbxImporter: wrote material '{}' (slot={} textures={})",
                                matPath, slot, mdata.textures.size());
                result.materialPaths[slot] = matPath;
                if (onMaterialWritten)
                {
                    onMaterialWritten(matPath);
                }
            }
            else
            {
                ORANGE_LOG_WARN("FbxImporter: WriteMaterialFile '{}' 失败", matPath);
            }
        }

        // 把按 slot 排列的 .material 路径写进 .meta subMeshMaterials（单 + 多 material
        // 都写，drop 时单材质设 Renderable.materialInstance / 多材质挂 SubMeshMaterials；
        // 与 gltf/obj 一致）。全无 material 留空。
        bool anyMaterial = false;
        for (const auto& m : result.materialPaths)
        {
            if (!m.empty())
            {
                anyMaterial = true;
                break;
            }
        }
        if (anyMaterial)
        {
            meta.subMeshMaterials = result.materialPaths;
        }
        if (!WriteTextureMeta(metaPath, meta))
        {
            result.status  = ImportStatus::MetaWriteFailed;
            result.message = ".meta write failed";
            ORANGE_LOG_ERROR("FbxImporter: '{}' -> '{}': {}",
                             srcPath, metaPath, result.message);
            return result;
        }

        result.status   = ImportStatus::Success;
        result.destPath = destMeshStr;
        result.message  = "imported fbx mesh";
        ORANGE_LOG_INFO("FbxImporter: '{}' -> '{}' (vtx={} idx={} hash={})",
                        srcPath, destMeshStr,
                        mesh->Positions().size(), mesh->Indices().size(),
                        HashToHexString(meta.sourceHash));
        return result;
    }

    // GUI 包装 RunFbxImport(host)（注入 EnsureMaterialInstance 注册回调）在
    // ImportHostBridge.cpp —— 把引用 EditorHost / EnsureMaterialInstance 的薄壳集中
    // 到那个单独 TU，让本 TU（含 ofbx 依赖）的链接边界清晰。

} // namespace Orange::Editor::Import
