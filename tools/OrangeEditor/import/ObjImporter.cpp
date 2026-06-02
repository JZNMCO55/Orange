#include "ObjImporter.h"

#include "../MaterialFileIO.h"  // .mtl → .material（OBJ 材质导入）
#include "ImportDispatcher.h"   // ImportTextureToRegistry（map_Kd / map_bump 贴图）
#include "MeshTangentGen.h"
#include "MetaSidecar.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/render/MaterialTypes.h>

#include <glm/vec4.hpp>

#include <cmath>

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

// 把一个 .mtl material（tinyobj::material_t）翻成 pbr 模板的 MaterialFileData。
// 纯标量映射（Phong/PBR ext → uBaseColor / uMRA / uEmissive）；贴图（map_Kd 等）
// 留后续（需接 ImportTextureToRegistry co-locate，本 commit 先做 scalar）。
//   * uBaseColor = (Kd.rgb, dissolve d)
//   * uMRA       = (metallic Pm, roughness, ao=1, 0)；roughness 优先 PBR ext Pr，
//                  缺省（Pr==0）按 Phong 高光指数 Ns 推导 sqrt(2/(Ns+2))（Ns 越大
//                  越光滑），clamp[0.04,1]
//   * uEmissive  = (Ke.rgb, 0)，仅 Ke 非零时写（pbr emissive 通道，HDR>1 进 bloom）
Orange::Editor::Material::MaterialFileData
BuildObjMaterialFileData(const tinyobj::material_t& m)
{
    using ::Orange::Engine::Render::MaterialUniformType;
    Orange::Editor::Material::MaterialFileData mdata;
    mdata.templateName = "pbr";

    const float d = (m.dissolve > 0.0f) ? m.dissolve : 1.0f;
    Orange::Editor::Material::UniformOverrideValue uBase;
    uBase.name  = "uBaseColor";
    uBase.type  = MaterialUniformType::Vec4;
    uBase.value = glm::vec4(m.diffuse[0], m.diffuse[1], m.diffuse[2], d);
    mdata.uniforms.push_back(uBase);

    // roughness：PBR ext Pr（m.roughness）非 0 直接用；否则 Phong Ns 推导。
    float roughness = m.roughness;
    if (roughness <= 0.0f)
    {
        const float ns = (m.shininess > 0.0f) ? m.shininess : 0.0f;
        roughness = std::sqrt(2.0f / (ns + 2.0f));
    }
    if (roughness < 0.04f) { roughness = 0.04f; }
    if (roughness > 1.0f)  { roughness = 1.0f; }
    float metallic = m.metallic;  // Pm；缺省 0 = 非金属（合理）
    if (metallic < 0.0f) { metallic = 0.0f; }
    if (metallic > 1.0f) { metallic = 1.0f; }
    Orange::Editor::Material::UniformOverrideValue uMra;
    uMra.name  = "uMRA";
    uMra.type  = MaterialUniformType::Vec4;
    uMra.value = glm::vec4(metallic, roughness, 1.0f, 0.0f);
    mdata.uniforms.push_back(uMra);

    if (m.emission[0] > 0.0f || m.emission[1] > 0.0f || m.emission[2] > 0.0f)
    {
        Orange::Editor::Material::UniformOverrideValue uEmis;
        uEmis.name  = "uEmissive";
        uEmis.type  = MaterialUniformType::Vec4;
        uEmis.value = glm::vec4(m.emission[0], m.emission[1], m.emission[2], 0.0f);
        mdata.uniforms.push_back(uEmis);
    }
    return mdata;
}

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

ImportResult RunObjImportToRegistry(std::string_view srcPath,
                                    ::Orange::Engine::Asset::AssetRegistry& registry)
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
        result.message = "source .obj missing or not a regular file";
        ORANGE_LOG_ERROR("ObjImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    // T5 hash 增量短路：先算源 hash + 算目标 .mesh 路径，若 .meta sourceHash
    // 已匹配则直接跳过 tinyobj 解析 + dedup + Save + Load 全套（重复拖同一
    // 文件的快路径）。失败仍走完整 import 路径。
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
            result.message  = "obj unchanged, skipped reimport";
            ORANGE_LOG_INFO("ObjImporter: '{}' unchanged (hash={}), skip",
                            srcPath, HashToHexString(earlyHashOpt.value()));
            return result;
        }
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
    // mtl_basedir 必须以 '/' 结尾：tinyobj v1.0.6 MaterialFileReader 是
    // `m_mtlBaseDir + matId` 直接拼接、不插分隔符，缺尾斜杠会拼成
    // `.../dirmtl.mtl` 加载失败（"Failed to load material file(s)"）。
    std::string baseDir = src.parent_path().generic_string();
    if (!baseDir.empty() && baseDir.back() != '/') { baseDir += '/'; }
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

    // 多 material per mesh：先把 dedup 后的索引按**面（三角）顺序**收进 faceVertIdx
    // （3 个 / 三角）+ 记每个三角的 material slot（triSlot）；orderedObjMat 按
    // 首次出现顺序去重 tinyobj material id（slot 下标 = orderedObjMat 下标，-1 =
    // 无 material 的面）。单 material / 无 material（orderedObjMat.size()<=1）后续
    // 退化回整 mesh 单段 face-order 路径，零回归。
    std::vector<std::uint32_t> faceVertIdx;
    std::vector<std::uint32_t> triSlot;
    std::vector<int>           orderedObjMat;
    auto slotForObjMat = [&orderedObjMat](int objMatId) -> std::uint32_t {
        for (std::size_t i = 0; i < orderedObjMat.size(); ++i)
        {
            if (orderedObjMat[i] == objMatId)
            {
                return static_cast<std::uint32_t>(i);
            }
        }
        orderedObjMat.push_back(objMatId);
        return static_cast<std::uint32_t>(orderedObjMat.size() - 1);
    };
    if (totalFaceVerts > 0)
    {
        dedup.reserve(totalFaceVerts / 4 + 1);
        faceVertIdx.reserve(totalFaceVerts);
        triSlot.reserve(totalFaceVerts / 3 + 1);
    }

    for (const auto& sh : shapes)
    {
        const auto& meshIndices = sh.mesh.indices;
        const auto& matIds      = sh.mesh.material_ids;  // 每三角一个（triangulate）
        const std::size_t triCount = meshIndices.size() / 3;
        for (std::size_t tri = 0; tri < triCount; ++tri)
        {
            const int objMat = (tri < matIds.size()) ? matIds[tri] : -1;
            triSlot.push_back(slotForObjMat(objMat));
            for (int k = 0; k < 3; ++k)
            {
                const auto& fv = meshIndices[tri * 3 + static_cast<std::size_t>(k)];
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

                    faceVertIdx.push_back(newIdx);
                }
                else
                {
                    faceVertIdx.push_back(it->second);
                }
            }
        }
    }

    // 索引装配：多 material → 按 slot 分组重排 indices + 建 SubMesh 段；单 /
    // 无 material → 直接 face-order（与历史一致，零回归）。
    std::vector<::Orange::Engine::Asset::SubMesh> subMeshes;
    const bool multiMaterial = orderedObjMat.size() > 1;
    if (multiMaterial)
    {
        indices.reserve(faceVertIdx.size());
        for (std::uint32_t slot = 0; slot < orderedObjMat.size(); ++slot)
        {
            const std::uint32_t offset = static_cast<std::uint32_t>(indices.size());
            for (std::size_t tri = 0; tri < triSlot.size(); ++tri)
            {
                if (triSlot[tri] == slot)
                {
                    indices.push_back(faceVertIdx[tri * 3 + 0]);
                    indices.push_back(faceVertIdx[tri * 3 + 1]);
                    indices.push_back(faceVertIdx[tri * 3 + 2]);
                }
            }
            const std::uint32_t count =
                static_cast<std::uint32_t>(indices.size()) - offset;
            if (count > 0) { subMeshes.push_back({offset, count, slot}); }
        }
    }
    else
    {
        indices = std::move(faceVertIdx);
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

    // 每模型一个子目录 assets/Models/<stem>/ —— mesh + source copy co-locate
    // 进去（与 gltf importer 一致），避免和别的模型的文件混在 Models 根下。
    // 已存在文件 overwrite（reimport 语义）。
    const std::string stem = src.stem().generic_string();
    fs::path destDir = fs::path(kModelsDir) / stem;
    fs::create_directories(destDir, ec);
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

    // MikkTSpace 高质量切线 —— 仅当 UV + normal 都在时可算（缺任一则切线
    // 无定义，留给引擎 Load 端 Lengyel fallback）。会就地 re-weld
    // positions/uvs/normals/indices（顶点数可能增），故必须在下面 std::move
    // 进构造函数之前调。结果在构造后用 SetTangents 注入。
    std::vector<::Orange::Engine::Asset::VertexTangent4> tangents;
    bool haveTangents = false;
    if (!uvs.empty() && !normals.empty())
    {
        haveTangents = Orange::Editor::Import::GenerateMikkTSpaceTangents(
            positions, uvs, normals, indices, tangents);
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

    // MikkTSpace 算出的切线注入 mesh → MeshLoader::Save 写成 .mesh v4
    // （含 tangent 段）；Load 读回时即有切线，不触发 Lengyel fallback。
    if (haveTangents)
    {
        mesh->SetTangents(std::move(tangents));
    }

    // 多 material OBJ：写 sub-mesh 段（各 slot 一段连续索引区间）。索引装配时
    // 已按 slot 分组重排 + 建好 subMeshes；mikktspace re-weld 保留三角形顺序
    // （与 gltf 同款依赖），故区间在 re-weld 后仍有效。单 / 无 material 时
    // subMeshes 为空，HasSubMeshes()==false，渲染端走整 mesh 单 material 路径。
    if (multiMaterial && !subMeshes.empty())
    {
        mesh->SetSubMeshes(std::move(subMeshes));
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
    auto loadRes = registry.Load<MeshAsset>(destMeshStr);
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

    // .mtl 材质导入：按 slot（orderedObjMat 顺序）逐个生成 .material（pbr 模板，
    // scalar 通道）+ 写进 .meta subMeshMaterials —— drop 时自动应用（单材质设
    // Renderable.materialInstance / 多材质挂 SubMeshMaterialsComponent，与 gltf
    // 路径一致，对齐 Lumix/Unity 导 OBJ 带材质）。命名：slot 0 = <stem>.material
    // （单材质字节/路径与历史一致），slot>=1 = <stem>_<matname>.material（清洗
    // 路径分隔符）。无 material 的 slot（objMat<0，纯几何）该项留空、不写。
    auto sanitizeObjMatName = [&](int objMat, std::uint32_t slot) -> std::string {
        std::string name = (objMat >= 0
                            && static_cast<std::size_t>(objMat) < materials.size()
                            && !materials[objMat].name.empty())
                               ? materials[objMat].name
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
    std::vector<std::string> objMaterialPaths(orderedObjMat.size());
    for (std::uint32_t slot = 0; slot < orderedObjMat.size(); ++slot)
    {
        const int objMat = orderedObjMat[slot];
        if (objMat < 0 || static_cast<std::size_t>(objMat) >= materials.size())
        {
            continue;  // 无 material 的 slot（纯几何面）—— 该项留空
        }
        const std::string fileName = (slot == 0)
            ? (stem + ".material")
            : (stem + "_" + sanitizeObjMatName(objMat, slot) + ".material");
        const std::string matPath = (destDir / fileName).generic_string();
        auto mdata = BuildObjMaterialFileData(materials[objMat]);

        // 贴图：map_Kd → binding 0（baseColor）、map_bump → binding 1（normal）。
        // 路径相对 mtl_basedir（baseDir 已带尾斜杠）解析 + 经
        // ImportTextureToRegistry co-locate 到模型目录（与 gltf 同一条纹理导入
        // 路径）。缺失 / import 失败该槽跳过（不写 texture，渲染端喂 default）。
        // 其它 map_Ks / map_Ns / map_Ka 不在 pbr set 1 通道里，暂不导。
        const std::string objDestDir = destDir.generic_string();
        auto addObjTexture = [&](const std::string& texName,
                                 std::uint32_t       binding) {
            if (texName.empty()) { return; }
            const std::string texSrc = baseDir + texName;
            std::error_code   tec;
            if (!fs::exists(texSrc, tec))
            {
                ORANGE_LOG_WARN("ObjImporter: material 贴图 '{}' 不存在，跳过该槽",
                                texSrc);
                return;
            }
            ImportResult tr =
                ImportTextureToRegistry(texSrc, registry, objDestDir);
            if (tr.status == ImportStatus::Success && !tr.destPath.empty())
            {
                mdata.textures.push_back({binding, tr.destPath});
            }
            else
            {
                ORANGE_LOG_WARN("ObjImporter: 贴图 '{}' import 失败，跳过该槽",
                                texSrc);
            }
        };
        addObjTexture(materials[objMat].diffuse_texname, 0u);
        addObjTexture(materials[objMat].bump_texname,    1u);

        if (::Orange::Editor::Material::WriteMaterialFile(matPath, mdata))
        {
            objMaterialPaths[slot] = matPath;
            ORANGE_LOG_INFO("ObjImporter: wrote material '{}' (slot={})",
                            matPath, slot);
        }
        else
        {
            ORANGE_LOG_WARN("ObjImporter: WriteMaterialFile '{}' 失败", matPath);
        }
    }
    bool anyObjMaterial = false;
    for (const auto& m : objMaterialPaths)
    {
        if (!m.empty()) { anyObjMaterial = true; break; }
    }
    if (anyObjMaterial)
    {
        meta.subMeshMaterials = objMaterialPaths;
        result.materialPaths  = objMaterialPaths;
    }

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

// GUI 包装 RunObjImport(host) 在 ImportHostBridge.cpp —— 把所有引用 EditorHost
// 的薄壳集中到那个单独 TU，让本 TU（含 tinyobjloader IMPLEMENTATION）保持
// headless 可链（不引 EditorHost / EnsureMaterialInstance）。

}  // namespace Orange::Editor::Import
