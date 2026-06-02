#include "GltfSceneImporter.h"

#include "MeshTangentGen.h"  // GenerateMikkTSpaceTangents（高质量切线）
#include "MetaSidecar.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

// glm 矩阵分解（has_matrix 的 node）走 gtx 实验扩展；仅本 TU 私有打开。
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// cgltf 仅取声明 —— CGLTF_IMPLEMENTATION 在 GltfImporter.cpp 一处 expand，
// 同一 lib / test exe 内链得到。
#include "cgltf.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace Orange::Editor::Import
{

namespace
{
using ::Orange::Engine::Entity;
using ::Orange::Engine::World;
using ::Orange::Engine::Asset::AssetRegistry;
using ::Orange::Engine::Asset::MeshAsset;
using ::Orange::Engine::Asset::MeshLoader;
using ::Orange::Engine::Asset::VertexNormal3;
using ::Orange::Engine::Asset::VertexPosition3;
using ::Orange::Engine::Asset::VertexTangent4;
using ::Orange::Engine::Asset::VertexUV2;
using ::Orange::Engine::Render::DirectionalLight;
using ::Orange::Engine::Render::PointLight;
using ::Orange::Engine::Render::RenderableComponent;
using ::Orange::Engine::Render::SpotLight;
using ::Orange::Engine::Scene::HierarchyComponent;
using ::Orange::Engine::Scene::NameComponent;
using ::Orange::Engine::Scene::TransformComponent;

namespace fs = std::filesystem;

constexpr const char* kModelsDir = "assets/Models";
constexpr const char* kScenesDir = "assets/scenes";

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

// 把字符串清洗成文件名安全片段（路径分隔符 / Windows 保留字符 → '_'）。
std::string SanitizeName(const std::string& in)
{
    std::string out = in;
    for (char& c : out)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
        {
            c = '_';
        }
    }
    return out;
}

// 把一个 cgltf mesh（其全部 triangle primitive 合并，不跨 mesh）抽成 MeshAsset。
// 与 GltfImporter 的整文件合并路径同款 attribute 读取，但单位是"一个 mesh"。
// G1 不消费 material —— 不写 sub-mesh（合并成单段，渲染走默认材质）。失败返回 nullptr。
std::unique_ptr<MeshAsset> BuildMeshAssetFromGltfMesh(const cgltf_mesh& mesh)
{
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<VertexNormal3>   normals;
    std::vector<std::uint32_t>   indices;

    bool fileHasUVs     = false;
    bool fileHasNormals = false;

    for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
    {
        const cgltf_primitive& prim = mesh.primitives[pi];
        if (prim.type != cgltf_primitive_type_triangles)
        {
            continue;
        }
        const cgltf_accessor* posAcc = FindAttribute(prim, cgltf_attribute_type_position);
        if (posAcc == nullptr || posAcc->count == 0)
        {
            continue;
        }
        const cgltf_accessor* nrmAcc = FindAttribute(prim, cgltf_attribute_type_normal);
        const cgltf_accessor* uvAcc  = FindAttribute(prim, cgltf_attribute_type_texcoord, 0);

        const std::uint32_t baseIdx = static_cast<std::uint32_t>(positions.size());
        const cgltf_size vtxCount = posAcc->count;

        positions.reserve(positions.size() + vtxCount);
        for (cgltf_size v = 0; v < vtxCount; ++v)
        {
            float buf[3] = {0, 0, 0};
            cgltf_accessor_read_float(posAcc, v, buf, 3);
            positions.push_back({buf[0], buf[1], buf[2]});
        }

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

        uvs.reserve(uvs.size() + vtxCount);
        if (uvAcc != nullptr && uvAcc->count == vtxCount)
        {
            for (cgltf_size v = 0; v < vtxCount; ++v)
            {
                float buf[2] = {0, 0};
                cgltf_accessor_read_float(uvAcc, v, buf, 2);
                uvs.push_back({buf[0], buf[1]});
            }
            fileHasUVs = true;
        }
        else
        {
            uvs.resize(uvs.size() + vtxCount, VertexUV2{0, 0});
        }

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

    if (positions.empty() || indices.empty())
    {
        return nullptr;
    }

    if (!fileHasNormals) { normals.clear(); }
    if (!fileHasUVs)     { uvs.clear(); }

    // MikkTSpace 切线 —— 需 UV + normal 齐备（re-weld 顶点，须在构造前算）。
    std::vector<VertexTangent4> tangents;
    bool haveTangents = false;
    if (!uvs.empty() && !normals.empty())
    {
        haveTangents = GenerateMikkTSpaceTangents(positions, uvs, normals, indices, tangents);
    }

    std::unique_ptr<MeshAsset> out;
    if (uvs.empty() && normals.empty())
    {
        out = std::make_unique<MeshAsset>(std::move(positions), std::move(indices));
    }
    else if (normals.empty())
    {
        out = std::make_unique<MeshAsset>(std::move(positions), std::move(uvs),
                                          std::move(indices));
    }
    else if (uvs.empty())
    {
        std::vector<VertexUV2> emptyUv;
        emptyUv.resize(positions.size());
        out = std::make_unique<MeshAsset>(std::move(positions), std::move(emptyUv),
                                          std::move(normals), std::move(indices));
    }
    else
    {
        out = std::make_unique<MeshAsset>(std::move(positions), std::move(uvs),
                                          std::move(normals), std::move(indices));
    }
    if (haveTangents)
    {
        out->SetTangents(std::move(tangents));
    }
    return out;
}

// 取 node 的 **local** 变换分解成 TRS，写进 TransformComponent（A1.1 step 2 /
// ADR-016）。引擎 RenderScene::Collect 现在跑 TransformSystem 沿 HierarchyComponent
// 累积 world matrix（parenting 真正生效），故导入只需写 local TRS——子节点的
// world 摆位由引擎累积，编辑器移动父节点也带动子。
//
// （历史：A1 之前引擎不累积 hierarchy，本导入曾 bake world flatten 进 local 绕过，
// A1 落地后回退为 local。）
//
// cgltf_node_transform_local 已处理 has_matrix / TRS 两种 node 形态，返回列主序
// 本地矩阵；glm::decompose 拆 TRS。
void NodeLocalTransform(const cgltf_node& node, glm::vec3& outPos, glm::quat& outRot,
                        glm::vec3& outScale)
{
    float local[16] = {0};
    cgltf_node_transform_local(&node, local);
    const glm::mat4 m = glm::make_mat4(local);
    glm::vec3 skew{};
    glm::vec4 perspective{};
    glm::decompose(m, outScale, outRot, outPos, skew, perspective);
}

// 取 node 的 **world** rotation（沿父累积）—— 仅光源方向编码用。引擎的光源
// 消费者（Pipeline 取光向）目前仍读 entity 自身 local rotation（不累积 hierarchy，
// A1.1 后续 increment 才切），所以要把 world 光向编码进 entity 的 rotation，光源
// 才指对世界方向。glTF 灯光通常是 scene-root 直接子（local==world），编码后即正确。
glm::quat NodeWorldRotation(const cgltf_node& node)
{
    float world[16] = {0};
    cgltf_node_transform_world(&node, world);
    const glm::mat4 m = glm::make_mat4(world);
    glm::vec3 scale{}, translation{}, skew{};
    glm::vec4 perspective{};
    glm::quat  rotation{};
    glm::decompose(m, scale, rotation, translation, skew, perspective);
    return rotation;
}

// 该光是否需要方向（directional / spot）—— 决定 ProcessNode 是否把 world 光向
// 编码进 entity rotation（point 光无方向，rotation 保留 node 自身姿态）。
bool LightNeedsDirection(const cgltf_light* light)
{
    return light != nullptr &&
           (light->type == cgltf_light_type_directional ||
            light->type == cgltf_light_type_spot);
}

// glTF KHR_lights_punctual 用**光度（photometric）单位**：directional = lux、
// point/spot = candela（lm/sr）。引擎 intensity 是无单位乘子（content-scale：
// 现有 DirectionalLight 内容用 1.2~2.5、PointLight ~40）。683 lm/W 是 555nm 的
// 标准光视效能（luminous efficacy）—— glTF 导出器（Blender 等）正是用它把
// W → lux/candela，÷683 即反推回内容尺度的乘子（radiant per-sr，与引擎 point/spot
// 的 I/d² 衰减模型一致）。实测 Blender sun 3 W/m² → glTF 2049 lux → ÷683 = 3.0，
// 正好落在引擎 DirectionalLight 1.2~2.5 的尺度 —— 故用单一 ÷683 而非 raw 透传
// （raw 会让 viewport 直接过曝纯白）。仍是近似（引擎 intensity 非物理标定），
// 导入后可微调，但已落在可用范围，不再是"必须手调否则全白"。
constexpr float kGltfLuminousEfficacy = 683.0f;

// KHR_lights_punctual → 引擎光源 component。color / range / cone 直接映射，
// intensity ÷683（见上）。方向（directional/spot）已由调用方编码进 entity
// rotation，本函数只填 component 的 color/intensity/range/cone 等"非几何"字段。
void AddGltfLight(World& world, Entity e, const cgltf_light& light)
{
    const glm::vec3 color(light.color[0], light.color[1], light.color[2]);
    const float intensity = light.intensity / kGltfLuminousEfficacy;
    switch (light.type)
    {
        case cgltf_light_type_directional:
        {
            DirectionalLight d;
            d.color     = color;
            d.intensity = intensity;
            world.AddComponent<DirectionalLight>(e, d);
            break;
        }
        case cgltf_light_type_point:
        {
            PointLight p;
            p.color     = color;
            p.intensity = intensity;
            // glTF range == 0 表示"无限 / 未指定"——退回引擎默认。
            p.range     = (light.range > 0.0f) ? light.range : 10.0f;
            world.AddComponent<PointLight>(e, p);
            break;
        }
        case cgltf_light_type_spot:
        {
            SpotLight s;
            s.color          = color;
            s.intensity      = intensity;
            s.range          = (light.range > 0.0f) ? light.range : 15.0f;
            s.innerConeAngle = light.spot_inner_cone_angle;
            s.outerConeAngle = light.spot_outer_cone_angle;
            world.AddComponent<SpotLight>(e, s);
            break;
        }
        default:
            ORANGE_LOG_WARN("GltfSceneImporter: 未知 glTF light 类型，跳过");
            break;
    }
}

// 递归把 node 子树建进 World，返回本 node 对应的 Entity。本 node 的 parent /
// 兄弟链由调用方填（调用方知道兄弟顺序）；本函数负责 firstChild + 各子节点的
// parent / 兄弟链 + 子树递归。
//
// 关键：不跨 AddComponent / CreateEntity 持有 component 指针（entt storage
// realloc 会悬空）—— firstChild / 子链的 patch 全部在"该 node 子树建完、不再
// 新增实体"之后用现取的指针写。
Entity ProcessNode(World& world, const cgltf_node& node,
                   const std::map<const cgltf_mesh*, ::Orange::Engine::Asset::AssetHandle<MeshAsset>>& meshHandles,
                   std::size_t& outEntityCount, std::size_t& outLightCount)
{
    Entity e = world.CreateEntity();
    ++outEntityCount;

    NameComponent nameC;
    nameC.name = (node.name != nullptr && node.name[0] != '\0')
                     ? std::string(node.name)
                     : ("Node_" + std::to_string(outEntityCount - 1));
    world.AddComponent<NameComponent>(e, std::move(nameC));

    glm::vec3 pos{}, scl{};
    glm::quat rot{};
    NodeLocalTransform(node, pos, rot, scl);

    // 光源（directional/spot）方向沿 glTF 本地 -Z；引擎 ComputeXxxWorldDir =
    // rotation*(0,-1,0)。光源消费者目前读 entity local rotation（未累积 hierarchy），
    // 故把 **world** 光向（从 node world rotation 算）编码进 entity rotation。
    // mesh node + point 光保留 local rotation（mesh 走 Collect 累积，point 无方向）。
    glm::quat finalRot = rot;
    if (LightNeedsDirection(node.light))
    {
        const glm::quat worldRot = NodeWorldRotation(node);
        const glm::vec3 worldLightDir =
            glm::normalize(worldRot * glm::vec3(0.0f, 0.0f, -1.0f));
        finalRot = ::Orange::Engine::Render::MakeDirectionalLightRotationFromDir(worldLightDir);
    }
    world.AddComponent<TransformComponent>(e, TransformComponent{pos, finalRot, scl});

    if (node.light != nullptr)
    {
        AddGltfLight(world, e, *node.light);
        ++outLightCount;
    }

    if (node.mesh != nullptr)
    {
        auto it = meshHandles.find(node.mesh);
        if (it != meshHandles.end() && it->second.IsValid())
        {
            RenderableComponent rc;
            rc.mesh = it->second;
            // G1：material 留空（默认材质）；G2 接 per-mesh PBR material。
            world.AddComponent<RenderableComponent>(e, rc);
        }
    }

    // 先挂一个全 Invalid 的 Hierarchy（parent / 兄弟由调用方 patch）。
    world.AddComponent<HierarchyComponent>(e, HierarchyComponent{});

    // 递归建子节点（会新增实体 → 期间不得持有任何 component 指针）。
    std::vector<Entity> childEntities;
    childEntities.reserve(node.children_count);
    for (cgltf_size ci = 0; ci < node.children_count; ++ci)
    {
        childEntities.push_back(
            ProcessNode(world, *node.children[ci], meshHandles, outEntityCount, outLightCount));
    }

    // 子树建完，不再新增实体 —— 现在 patch firstChild + 子节点 parent / 兄弟链。
    if (!childEntities.empty())
    {
        if (auto* h = world.GetComponent<HierarchyComponent>(e))
        {
            h->firstChild = childEntities.front();
        }
        const std::size_t n = childEntities.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            if (auto* ch = world.GetComponent<HierarchyComponent>(childEntities[i]))
            {
                ch->parent      = e;
                ch->prevSibling = (i > 0) ? childEntities[i - 1] : Entity::Invalid();
                ch->nextSibling = (i + 1 < n) ? childEntities[i + 1] : Entity::Invalid();
            }
        }
    }
    return e;
}
}  // namespace

ImportResult RunGltfSceneImportToRegistry(std::string_view srcPath,
                                          AssetRegistry& registry)
{
    ImportResult result{};

    fs::path src(srcPath.begin(), srcPath.end());
    std::error_code ec;
    if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec))
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "source .gltf/.glb missing or not a regular file";
        ORANGE_LOG_ERROR("GltfSceneImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    // 增量短路（与单 mesh importer 的 T5 hash 短路同款）：源 hash 与已存在 scene
    // .meta 的 sourceHash 匹配则跳过整套（cgltf 解析 + mesh 写盘 + Scene::Save）——
    // 既省功，也避免重导覆盖用户对 .scene.json 的手工编辑（G5 re-import override
    // 落地前，至少做到"没改源就不动产物"）。
    const std::string basename = SanitizeName(src.stem().generic_string());
    const std::string scenePath =
        (fs::path(kScenesDir) / (basename + ".scene.json")).generic_string();
    const auto earlyHashOpt = ComputeFileHashFnv1a(srcPath);
    if (earlyHashOpt.has_value() &&
        MetaSourceHashMatches(scenePath, earlyHashOpt.value()))
    {
        result.status   = ImportStatus::Success;
        result.destPath = scenePath;
        result.message  = "gltf scene unchanged, skipped reimport";
        ORANGE_LOG_INFO("GltfSceneImporter: '{}' unchanged (hash={}), skip",
                        srcPath, HashToHexString(earlyHashOpt.value()));
        return result;
    }

    const std::string srcStr(srcPath);
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result rc = cgltf_parse_file(&options, srcStr.c_str(), &data);
    if (rc != cgltf_result_success || data == nullptr)
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = std::string("cgltf_parse_file: ") + CgltfResultToString(rc);
        ORANGE_LOG_ERROR("GltfSceneImporter: '{}': {}", srcPath, result.message);
        if (data != nullptr) { cgltf_free(data); }
        return result;
    }
    rc = cgltf_load_buffers(&options, data, srcStr.c_str());
    if (rc != cgltf_result_success)
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = std::string("cgltf_load_buffers: ") + CgltfResultToString(rc);
        ORANGE_LOG_ERROR("GltfSceneImporter: '{}': {}", srcPath, result.message);
        cgltf_free(data);
        return result;
    }

    // 每模型一个子目录 assets/Models/<basename>/ —— 各 mesh + 源 copy co-locate。
    fs::path modelDir = fs::path(kModelsDir) / basename;
    fs::create_directories(modelDir, ec);

    // copy 源（ADR-008 4 件套之 copy 源）。失败不致命 —— 只是后续 reimport
    // 找不到原文件，几何已落盘。
    fs::path destSrc = modelDir / src.filename();
    fs::copy_file(src, destSrc, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        ORANGE_LOG_WARN("GltfSceneImporter: copy source '{}' -> '{}' failed: {}",
                        srcPath, destSrc.generic_string(), ec.message());
        ec.clear();
    }

    // 为每个被任何 node 引用的 cgltf mesh 写一个 .mesh + .meta，Load 进 registry。
    // 同一 mesh 被多 node 引用只写一次（按指针去重）。命名 <basename>_<meshname>.mesh，
    // 同名 / 匿名退化用 mesh 序号。
    std::map<const cgltf_mesh*, ::Orange::Engine::Asset::AssetHandle<MeshAsset>> meshHandles;
    std::set<std::string> usedMeshStems;
    std::size_t writtenMeshes = 0;

    const auto ensureMesh = [&](const cgltf_mesh* m) -> bool {
        if (m == nullptr) { return false; }
        if (meshHandles.count(m) != 0) { return true; }

        const std::size_t meshIdx =
            static_cast<std::size_t>(m - data->meshes);
        std::string stem = (m->name != nullptr && m->name[0] != '\0')
                               ? SanitizeName(m->name)
                               : ("mesh" + std::to_string(meshIdx));
        std::string fileStem = basename + "_" + stem;
        if (usedMeshStems.count(fileStem) != 0)
        {
            fileStem = basename + "_" + stem + "_" + std::to_string(meshIdx);
        }
        usedMeshStems.insert(fileStem);

        std::unique_ptr<MeshAsset> meshAsset = BuildMeshAssetFromGltfMesh(*m);
        if (meshAsset == nullptr)
        {
            ORANGE_LOG_WARN("GltfSceneImporter: mesh '{}' 无可用三角几何，跳过", fileStem);
            return false;
        }

        const std::string meshPath = (modelDir / (fileStem + ".mesh")).generic_string();
        auto saveRes = MeshLoader::Save(meshPath, *meshAsset);
        if (saveRes.IsErr())
        {
            ORANGE_LOG_ERROR("GltfSceneImporter: MeshLoader::Save '{}' 失败", meshPath);
            return false;
        }
        auto loadRes = registry.Load<MeshAsset>(meshPath);
        if (loadRes.IsErr())
        {
            ORANGE_LOG_ERROR("GltfSceneImporter: registry.Load<MeshAsset> '{}' 失败", meshPath);
            return false;
        }

        // mesh .meta（source hash 记源 gltf）。
        const auto hashOpt = ComputeFileHashFnv1a(srcPath);
        TextureMetaV1 meta{};
        meta.sourcePath = src.generic_string();
        meta.sourceHash = hashOpt.value_or(0);
        meta.handleId   = 0;
        WriteTextureMeta(MetaPathFor(meshPath), meta);

        meshHandles[m] = loadRes.Value();
        ++writtenMeshes;
        return true;
    };

    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        if (data->nodes[i].mesh != nullptr)
        {
            ensureMesh(data->nodes[i].mesh);
        }
    }

    // writtenMeshes == 0 不再致命 —— 纯灯光 / 纯空 group 场景仍可导入（产出
    // 只含 light / group entity 的 scene）。真正"无内容"由下方 entityCount == 0
    // 兜底。
    if (writtenMeshes == 0)
    {
        ORANGE_LOG_WARN("GltfSceneImporter: '{}' 无三角 mesh 几何（仅灯光 / 空 node？）",
                        srcPath);
    }

    // 选 scene：优先 data->scene；否则 scenes[0]；都没有则退回全部 nodes 当根。
    std::vector<const cgltf_node*> roots;
    const cgltf_scene* scene =
        (data->scene != nullptr) ? data->scene
                                 : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene != nullptr)
    {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
        {
            roots.push_back(scene->nodes[i]);
        }
    }
    else
    {
        // 无 scene 定义：把没有 parent 的 node 当根。
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            if (data->nodes[i].parent == nullptr)
            {
                roots.push_back(&data->nodes[i]);
            }
        }
    }

    // 建 World 镜像 node 树。
    World world;
    std::size_t entityCount = 0;
    std::size_t lightCount  = 0;
    for (std::size_t ri = 0; ri < roots.size(); ++ri)
    {
        Entity rootE = ProcessNode(world, *roots[ri], meshHandles, entityCount, lightCount);
        // 根：parent 留 Invalid，用 sortIndex 定根间顺序（HierarchyComponent 约定）。
        if (auto* h = world.GetComponent<HierarchyComponent>(rootE))
        {
            h->sortIndex = static_cast<int>(ri);
        }
    }

    if (entityCount == 0)
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "glTF scene has no importable nodes";
        ORANGE_LOG_ERROR("GltfSceneImporter: '{}': {}", srcPath, result.message);
        cgltf_free(data);
        return result;
    }

    cgltf_free(data);  // World 已持有几何 handle，cgltf 结构不再需要

    // Scene::Save —— assetRegistry 反查 mesh handle → 相对路径写进 scene.json。
    // scenePath / basename 已在函数顶部（hash 短路处）算好。
    fs::create_directories(kScenesDir, ec);

    ::Orange::Engine::Scene::SaveOptions saveOpts;
    saveOpts.assetRegistry = &registry;
    auto saveRes = ::Orange::Engine::Scene::Save(world, scenePath, saveOpts);
    if (saveRes.IsErr())
    {
        result.status  = ImportStatus::AssetLoadFailed;
        result.message = "Scene::Save failed";
        ORANGE_LOG_ERROR("GltfSceneImporter: Scene::Save '{}' 失败", scenePath);
        return result;
    }

    // scene .meta（source hash），与 mesh .meta 对称。
    const auto sceneHashOpt = ComputeFileHashFnv1a(srcPath);
    TextureMetaV1 sceneMeta{};
    sceneMeta.sourcePath = src.generic_string();
    sceneMeta.sourceHash = sceneHashOpt.value_or(0);
    sceneMeta.handleId   = 0;
    if (!WriteTextureMeta(MetaPathFor(scenePath), sceneMeta))
    {
        result.status  = ImportStatus::MetaWriteFailed;
        result.message = "scene .meta write failed";
        ORANGE_LOG_ERROR("GltfSceneImporter: scene .meta '{}' 写失败",
                         MetaPathFor(scenePath));
        return result;
    }

    result.status   = ImportStatus::Success;
    result.destPath = scenePath;
    result.message  = "imported gltf scene (entities=" + std::to_string(entityCount) +
                      " meshes=" + std::to_string(writtenMeshes) +
                      " lights=" + std::to_string(lightCount) + ")";
    ORANGE_LOG_INFO("GltfSceneImporter: '{}' -> '{}' (entities={} meshes={} lights={})",
                    srcPath, scenePath, entityCount, writtenMeshes, lightCount);
    return result;
}

}  // namespace Orange::Editor::Import
