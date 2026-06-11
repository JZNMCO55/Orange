#include "FbxSceneImporter.h"

#include "FbxAxisConverter.h"   // AxisConverter / MakeAxisConverter（顶点 + 共轭取 R）
#include "FbxMaterialParse.h"   // BuildFbxMaterialFileData / StageFbxTextureSources
#include "ImportDispatcher.h"   // ImportTextureToRegistry（贴图 co-locate 导入）
#include "MeshTangentGen.h"     // GenerateMikkTSpaceTangents（高质量切线）
#include "MetaSidecar.h"
#include "../MaterialFileIO.h"  // WriteMaterialFile（写 .material sidecar）

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/SubMeshMaterialsComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

// glm 矩阵分解（node local 共轭后拆 TRS）走 gtx 实验扩展；仅本 TU 私有打开。
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>  // glm::radians（相机桥接 / FOV 退默认）

// OpenFBX vendor 头 —— 仅取声明（ofbx.cpp / libdeflate.c 作为独立 TU 编译）。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4244)  // narrowing
#  pragma warning(disable: 4267)  // size_t → smaller int
#endif
#include "ofbx.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <cmath>  // std::atan / std::tan（相机 FOV 计算）
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
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
using ::Orange::Engine::Asset::SubMesh;
using ::Orange::Engine::Asset::VertexNormal3;
using ::Orange::Engine::Asset::VertexPosition3;
using ::Orange::Engine::Asset::VertexTangent4;
using ::Orange::Engine::Asset::VertexUV2;
using ::Orange::Engine::Render::Camera;
using ::Orange::Engine::Render::MaterialInstance;
using ::Orange::Engine::Render::RenderableComponent;
using ::Orange::Engine::Render::SubMeshMaterialsComponent;
using ::Orange::Engine::Scene::HierarchyComponent;
using ::Orange::Engine::Scene::NameComponent;
using ::Orange::Engine::Scene::TransformComponent;

namespace fs = std::filesystem;

constexpr const char* kModelsDir = "assets/Models";
constexpr const char* kScenesDir = "assets/scenes";

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

// 一个 mesh node 抽出的全部产物：几何 handle + 该 mesh 自身 slot 顺序的
// material 指针列表（slot i 挂 slotMaterials[i]；nullptr = 该 slot 无 material）。
struct MeshBuildResult
{
    ::Orange::Engine::Asset::AssetHandle<MeshAsset> handle{};
    std::vector<const ofbx::Material*>              slotMaterials;
};

// 把一个 FBX mesh node 的 GeometryData 抽成 MeshAsset，并按 material partition
// 切 sub-mesh、每段挂自己的 material slot —— 与 FbxImporter 的多 material 处理
// 同款，只是单位是"一个 mesh node"（不跨 node 合并）。
//
// 顶点 / 法线经 AxisConverter 换轴（与 FbxImporter 一致）；UV 1-v 翻转。
// MikkTSpace 切线在 UV+normal 齐备时算。返回的 slotMaterials 即 orderedMats。
// 失败返回空 handle 的 result。
MeshBuildResult BuildMeshAssetFromFbxMesh(const ofbx::Mesh& mesh,
                                          const AxisConverter& conv,
                                          const std::string& meshPath,
                                          AssetRegistry& registry)
{
    MeshBuildResult result;

    const ofbx::GeometryData& geom = mesh.getGeometryData();
    const ofbx::Vec3Attributes posAttr = geom.getPositions();
    if (posAttr.values == nullptr || posAttr.count == 0)
    {
        return result;  // 无几何（纯 transform / group node）
    }
    const ofbx::Vec3Attributes nrmAttr = geom.getNormals();
    const ofbx::Vec2Attributes uvAttr  = geom.getUVs(0);
    const bool hasN = (nrmAttr.values != nullptr && nrmAttr.count == posAttr.count);
    const bool hasUV = (uvAttr.values != nullptr && uvAttr.count == posAttr.count);

    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<VertexNormal3>   normals;
    std::vector<std::uint32_t>   indices;

    bool fileHasUVs     = false;
    bool fileHasNormals = false;

    // material slot 去重（按指针，首次出现顺序，仅本 mesh 内）。nullptr 也占 slot。
    std::vector<const ofbx::Material*>& orderedMats = result.slotMaterials;
    auto slotForMaterial = [&orderedMats](const ofbx::Material* m) -> std::uint32_t {
        for (std::size_t i = 0; i < orderedMats.size(); ++i)
        {
            if (orderedMats[i] == m) { return static_cast<std::uint32_t>(i); }
        }
        orderedMats.push_back(m);
        return static_cast<std::uint32_t>(orderedMats.size() - 1);
    };

    std::vector<SubMesh> subMeshes;
    std::vector<int> triBuf;

    const int partitionCount = geom.getPartitionCount();
    for (int pi = 0; pi < partitionCount; ++pi)
    {
        // partition 下标 == 该 mesh 的 material slot（OpenFBX 把每个 polygon 按
        // material_index 放进 partitions[material_index]）。
        const ofbx::Material* mat =
            (pi < mesh.getMaterialCount()) ? mesh.getMaterial(pi) : nullptr;
        const std::uint32_t subMeshSlot = slotForMaterial(mat);
        const std::uint32_t subMeshIndexOffset =
            static_cast<std::uint32_t>(indices.size());

        const ofbx::GeometryPartition partition = geom.getPartition(pi);
        for (int poly = 0; poly < partition.polygon_count; ++poly)
        {
            const ofbx::GeometryPartition::Polygon& polygon = partition.polygons[poly];
            if (polygon.vertex_count < 3) { continue; }

            triBuf.resize(static_cast<std::size_t>(
                (polygon.vertex_count - 2) * 3 + 8));
            std::vector<int> tmp(static_cast<std::size_t>(polygon.vertex_count));
            const ofbx::u32 triIdxCount =
                ofbx::triangulate(geom, polygon, triBuf.data(), tmp.data());

            for (ofbx::u32 t = 0; t < triIdxCount; ++t)
            {
                const int fvIndex = triBuf[t];
                const ofbx::Vec3 p = posAttr.get(fvIndex);
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
                    // FBX UV 原点左下；引擎左上为 (0,0)（Vulkan）。1-v 翻转（同 .obj）。
                    uvs.push_back({static_cast<float>(uv.x),
                                   1.0f - static_cast<float>(uv.y)});
                    fileHasUVs = true;
                }
                else
                {
                    uvs.push_back(VertexUV2{0.0f, 0.0f});
                }

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

    if (positions.empty() || indices.empty())
    {
        result.slotMaterials.clear();
        return result;  // handle 留空 → caller 视作失败
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

    // 多 material 才写 sub-mesh 段；单 / 全无 material 退化整 mesh 单段。
    if (orderedMats.size() > 1)
    {
        out->SetSubMeshes(std::move(subMeshes));
    }

    auto saveRes = MeshLoader::Save(meshPath, *out);
    if (saveRes.IsErr())
    {
        ORANGE_LOG_ERROR("FbxSceneImporter: MeshLoader::Save '{}' 失败", meshPath);
        result.slotMaterials.clear();
        return result;
    }
    auto loadRes = registry.Load<MeshAsset>(meshPath);
    if (loadRes.IsErr())
    {
        ORANGE_LOG_ERROR("FbxSceneImporter: registry.Load<MeshAsset> '{}' 失败", meshPath);
        result.slotMaterials.clear();
        return result;
    }
    result.handle = loadRes.Value();
    return result;
}

// node local transform 的换轴：基变换（相似变换）共轭 M_engine = R · M_fbx · R⁻¹。
//
// 为什么是共轭而非 R·M：M_fbx 是在 **FBX 基** 下表达的一个线性 + 平移变换（"把
// 点 p_fbx 映成 M·p_fbx"）。引擎里的点是 p_engine = R·p_fbx。要在引擎基下表达
// **同一个几何变换**，需让作用在 p_engine 上的矩阵 M_engine 满足
//   M_engine · p_engine = R · (M_fbx · p_fbx)
//                       = R · M_fbx · R⁻¹ · (R · p_fbx)
//                       = (R · M_fbx · R⁻¹) · p_engine
// 故 M_engine = R · M_fbx · R⁻¹。R 是纯旋转（正交），R⁻¹ = Rᵀ。
//
// 直接 R·M 是错的：那只旋转了"输出"不旋转"输入基"，会让旋转轴 / 非均匀缩放方向
// 在换轴后指向错误（对纯平移恰好碰巧对，但带旋转 / 各向异缩放的子节点会歪）。
//
// 平移分量随之被 R 正确旋转（共轭对 4x4 齐次矩阵的平移列也成立）；unitScale 这里
// 不参与共轭（共轭是纯旋转相似变换），但 FBX local 平移本就以文件单位记，需额外
// 乘 unitScale 把平移折算到米 —— MVP unitScale=1（信任已烘米），故平移直接沿用。
glm::mat4 ConjugateNodeLocal(const glm::mat4& fbxLocal, const AxisConverter& conv)
{
    const glm::mat4 R = conv.RotationMat4();
    const glm::mat4 Rinv = glm::transpose(R);  // 正交旋转：逆 = 转置
    glm::mat4 m = R * fbxLocal * Rinv;
    // 平移按 unitScale 折算到米（MVP unitScale=1，乘 1 无副作用，保留以备扩展）。
    m[3][0] *= conv.unitScale;
    m[3][1] *= conv.unitScale;
    m[3][2] *= conv.unitScale;
    return m;
}

// OpenFBX DMatrix（列主序，m[12..14]=平移）→ glm::mat4（同列主序，double→float）。
glm::mat4 FbxMatrixToGlm(const ofbx::DMatrix& dm)
{
    glm::mat4 m(1.0f);
    for (int i = 0; i < 16; ++i)
    {
        m[i / 4][i % 4] = static_cast<float>(dm.m[i]);
    }
    return m;
}

// FBX camera → 引擎 Render::Camera component（投影部分）。朝向不在此处——FBX 相机
// 看 node 本地 +X，由 ProcessNode 对挂相机的 node 的 rotation 做 -Z→+X 桥接（见那里）；
// view 留单位，位姿由 entity Transform 决定（CameraFrustumGizmoPlugin 取 rotation*-Z）。
//
// 投影：
//   * perspective：film aperture 是**英寸**（OpenFBX getFilmWidth/Height，实测
//     Blender 36mm sensor → 1.4173 inch）→ ×25.4 转 mm；水平 FOV =
//     2·atan(filmW_mm/(2·focal_mm))，垂直 FOV = 2·atan(tan(hFOV/2)/aspect)
//     （水平拟合，对标 Blender sensor_fit HORIZONTAL）→ Camera::Perspective。
//   * orthographic：orthoZoom ≈ 视图较大维度世界尺寸（Blender ortho_scale）→
//     半宽 = orthoZoom/2，半高 = 半宽/aspect → Camera::Orthographic。
//   * aspect 取 aspectWidth/Height（渲染分辨率比，实测 1920/1080）；缺失退 16:9。
//   * near/far 是 FBX 单位距离，按 conv.unitScale（importScale）折算到米。
// 已知 MVP 限制：gate-fit（film 比 vs 渲染比不一致时的裁切）按水平拟合近似；
// Render::Camera 只存矩阵故为烘焙导入（re-edit 按矩阵不按 fov，同 glTF 相机）。
void AddFbxCamera(World& world, Entity e, const ofbx::Camera& camera,
                  const AxisConverter& conv)
{
    const float nearP = static_cast<float>(camera.getNearPlane()) * conv.unitScale;
    const float farP  = static_cast<float>(camera.getFarPlane()) * conv.unitScale;
    const float nearZ = (nearP > 1e-4f) ? nearP : 0.1f;
    const float farZ  = (farP > nearZ) ? farP : (nearZ + 1000.0f);

    const double aspW = camera.getAspectWidth();
    const double aspH = camera.getAspectHeight();
    const float aspect = (aspW > 0.0 && aspH > 0.0)
                             ? static_cast<float>(aspW / aspH) : (16.0f / 9.0f);

    if (camera.getProjectionType() == ofbx::Camera::ProjectionType::ORTHOGRAPHIC)
    {
        const float halfW =
            static_cast<float>(camera.getOrthoZoom()) * 0.5f * conv.unitScale;
        const float halfH = (aspect > 1e-4f) ? (halfW / aspect) : halfW;
        world.AddComponent<Camera>(
            e, Camera::Orthographic(-halfW, halfW, -halfH, halfH, nearZ, farZ));
        return;
    }

    constexpr float kInchToMm = 25.4f;
    const float focal   = static_cast<float>(camera.getFocalLength());
    const float filmWmm = static_cast<float>(camera.getFilmWidth()) * kInchToMm;
    float vfov;
    if (focal > 1e-4f && filmWmm > 1e-4f && aspect > 1e-4f)
    {
        const float hfov = 2.0f * std::atan(filmWmm / (2.0f * focal));
        vfov = 2.0f * std::atan(std::tan(hfov * 0.5f) / aspect);
    }
    else
    {
        vfov = glm::radians(50.0f);  // 参数缺失退默认垂直 FOV
    }
    world.AddComponent<Camera>(e, Camera::Perspective(vfov, aspect, nearZ, farZ));
}

// 把一个 mesh 的 slot material 列表（ofbx::Material* → sentinel MaterialInstance*）
// 接到 entity 的 Renderable / SubMeshMaterialsComponent 上。
//
// 单 material → 设 Renderable.materialInstance；多 material → 挂
// SubMeshMaterialsComponent（各 sub-mesh 段独立材质）+ slot 0 当 Renderable
// 兜底。与 GltfSceneImporter::AttachMeshMaterials 同款语义。
void AttachMeshMaterials(
    World& world, Entity e, RenderableComponent& rc,
    const std::vector<const ofbx::Material*>& slotMaterials,
    const std::unordered_map<const ofbx::Material*, MaterialInstance*>& matInstances)
{
    if (slotMaterials.empty())
    {
        return;  // 纯几何无 material → 默认材质
    }

    auto resolve = [&](const ofbx::Material* m) -> MaterialInstance* {
        if (m == nullptr) { return nullptr; }
        auto it = matInstances.find(m);
        return (it != matInstances.end()) ? it->second : nullptr;
    };

    if (slotMaterials.size() == 1)
    {
        rc.materialInstance = resolve(slotMaterials[0]);
        return;
    }

    SubMeshMaterialsComponent smc;
    smc.slots.reserve(slotMaterials.size());
    for (const ofbx::Material* m : slotMaterials)
    {
        smc.slots.push_back(resolve(m));
    }
    if (!smc.slots.empty() && smc.slots[0] != nullptr)
    {
        rc.materialInstance = smc.slots[0];
    }
    world.AddComponent<SubMeshMaterialsComponent>(e, std::move(smc));
}

// 取一个 node 的全部**子 node**（按 connection 图）。OpenFBX 的
// resolveObjectLink(idx) 返回连到本 node 的对象，含非 node（geometry /
// material）—— 过滤 isNode() 且 parent==本 node（finalize 已回填 parent）。
std::vector<const ofbx::Object*> ChildNodes(const ofbx::Object& node)
{
    std::vector<const ofbx::Object*> children;
    for (int i = 0;; ++i)
    {
        const ofbx::Object* child = node.resolveObjectLink(i);
        if (child == nullptr) { break; }
        if (!child->isNode()) { continue; }
        // 防御：只收真正以本 node 为 parent 的（resolveObjectLink 可能含
        // attribute / 跨链对象；finalize 已把 node→node 的 parent 回填）。
        if (child->getParent() != &node) { continue; }
        children.push_back(child);
    }
    return children;
}

// 递归把 node 子树建进 World，返回本 node 对应的 Entity。本 node 的 parent /
// 兄弟链由调用方填；本函数负责 firstChild + 各子节点的 parent / 兄弟链 + 递归。
//
// 关键：不跨 AddComponent / CreateEntity 持有 component 指针（entt storage
// realloc 会悬空）—— firstChild / 子链的 patch 全部在"该 node 子树建完"之后做。
Entity ProcessNode(
    World& world, const ofbx::Object& node, const AxisConverter& conv,
    const std::map<const ofbx::Mesh*, MeshBuildResult>& meshResults,
    const std::unordered_map<const ofbx::Material*, MaterialInstance*>& matInstances,
    const std::map<const ofbx::Object*, const ofbx::Camera*>& nodeCameras,
    std::size_t& outEntityCount, std::size_t& outCameraCount)
{
    Entity e = world.CreateEntity();
    ++outEntityCount;

    NameComponent nameC;
    nameC.name = (node.name[0] != '\0')
                     ? std::string(node.name)
                     : ("Node_" + std::to_string(outEntityCount - 1));
    world.AddComponent<NameComponent>(e, std::move(nameC));

    // node local transform → 共轭换轴 → decompose 成引擎 TRS。
    const glm::mat4 fbxLocal = FbxMatrixToGlm(node.getLocalTransform());
    const glm::mat4 engLocal = ConjugateNodeLocal(fbxLocal, conv);
    glm::vec3 pos{}, scl{};
    glm::quat rot{};
    glm::vec3 skew{};
    glm::vec4 perspective{};
    glm::decompose(engLocal, scl, rot, pos, skew, perspective);

    // FBX 相机看 node 本地 +X（FBX 约定，实测 Blender 导出的 camera node rotation
    // 经共轭后 rot*(+X) 落到正确世界 aim）；引擎 / CameraFrustumGizmoPlugin 看 -Z。
    // 若本 node 挂 camera attribute，给 rotation 后乘桥接 B（绕 +Y 转 -90°，把引擎
    // 本地 -Z 映到 +X、固定 up +Y）→ q_final*(-Z) = q_node*(+X) = 正确世界方向。
    // 注：桥接进 entity rotation，camera node 的子节点（罕见）会随之转——与 glTF 灯光
    // 桥接同款取舍（相机 / 灯几乎不带 mesh 子）。
    const auto camIt = nodeCameras.find(&node);
    const bool isCameraNode = (camIt != nodeCameras.end());
    if (isCameraNode)
    {
        rot = rot * glm::angleAxis(glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    world.AddComponent<TransformComponent>(e, TransformComponent{pos, rot, scl});

    if (isCameraNode)
    {
        AddFbxCamera(world, e, *camIt->second, conv);
        ++outCameraCount;
    }

    // mesh node：挂 Renderable + per-mesh material。OpenFBX Mesh 继承 Object，
    // 用 getType()==MESH 判，再向下取指针查 meshResults。
    if (node.getType() == ofbx::Object::Type::MESH)
    {
        const auto* meshPtr = static_cast<const ofbx::Mesh*>(&node);
        auto it = meshResults.find(meshPtr);
        if (it != meshResults.end() && it->second.handle.IsValid())
        {
            RenderableComponent rc;
            rc.mesh = it->second.handle;
            AttachMeshMaterials(world, e, rc, it->second.slotMaterials, matInstances);
            world.AddComponent<RenderableComponent>(e, rc);
        }
    }

    // 先挂全 Invalid 的 Hierarchy（parent / 兄弟由调用方 patch）。
    world.AddComponent<HierarchyComponent>(e, HierarchyComponent{});

    // 递归建子节点（会新增实体 → 期间不得持有任何 component 指针）。
    const std::vector<const ofbx::Object*> children = ChildNodes(node);
    std::vector<Entity> childEntities;
    childEntities.reserve(children.size());
    for (const ofbx::Object* child : children)
    {
        childEntities.push_back(
            ProcessNode(world, *child, conv, meshResults, matInstances, nodeCameras,
                        outEntityCount, outCameraCount));
    }

    // 子树建完，不再新增实体 —— patch firstChild + 子节点 parent / 兄弟链。
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

ImportResult RunFbxSceneImportToRegistry(std::string_view srcPath,
                                         AssetRegistry& registry,
                                         float importScale)
{
    ImportResult result{};

    fs::path src(srcPath.begin(), srcPath.end());
    std::error_code ec;
    if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec))
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "source .fbx missing or not a regular file";
        ORANGE_LOG_ERROR("FbxSceneImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    const std::string basename = SanitizeName(src.stem().generic_string(), "fbx");
    const std::string scenePath =
        (fs::path(kScenesDir) / (basename + ".scene.json")).generic_string();

    // 增量短路（与 gltf scene importer 同款）：源 hash 与已存在 scene .meta 匹配
    // 则跳过整套，避免重导覆盖用户对 .scene.json 的手工编辑。
    const auto earlyHashOpt = ComputeFileHashFnv1a(srcPath);
    if (earlyHashOpt.has_value() &&
        MetaSourceHashMatches(scenePath, earlyHashOpt.value()))
    {
        result.status   = ImportStatus::Success;
        result.destPath = scenePath;
        result.message  = "fbx scene unchanged, skipped reimport";
        ORANGE_LOG_INFO("FbxSceneImporter: '{}' unchanged (hash={}), skip",
                        srcPath, HashToHexString(earlyHashOpt.value()));
        return result;
    }

    // 读整个 .fbx 到内存（OpenFBX load 接 buffer + size）。
    std::ifstream ifs(src, std::ios::binary);
    if (!ifs)
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "open .fbx for read failed";
        ORANGE_LOG_ERROR("FbxSceneImporter: '{}': {}", srcPath, result.message);
        return result;
    }
    std::vector<ofbx::u8> bytes((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
    ifs.close();
    if (bytes.empty())
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = ".fbx is empty";
        ORANGE_LOG_ERROR("FbxSceneImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    // 解析：忽略动画 / skin / 灯光（MVP）；**保留 cameras**（scene import 导相机 →
    // Render::Camera）。只取静态几何 + 材质 + node 层级 + 相机。注意**不**设
    // IGNORE_MODELS —— scene import 需要 node（相机也挂在 NULL_NODE 上）。
    const ofbx::LoadFlags flags =
        ofbx::LoadFlags::IGNORE_BLEND_SHAPES |
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
        ORANGE_LOG_ERROR("FbxSceneImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    const AxisConverter conv = MakeAxisConverter(scene->getGlobalSettings(), importScale);

    // FBX camera 是 NodeAttribute（isNode==false），挂在一个 NULL_NODE 的 model node
    // 下（camera.getParent() == 该 node）。建 node→camera 索引，ProcessNode 遇到该
    // node 时挂 Render::Camera。必须在 scene->destroy() 之前建（Camera* destroy 后悬空）。
    std::map<const ofbx::Object*, const ofbx::Camera*> nodeCameras;
    {
        const int camCount = scene->getCameraCount();
        for (int ci = 0; ci < camCount; ++ci)
        {
            const ofbx::Camera* cam = scene->getCamera(ci);
            if (cam != nullptr && cam->getParent() != nullptr)
            {
                nodeCameras[cam->getParent()] = cam;
            }
        }
    }

    // 每模型一个子目录 assets/Models/<basename>/ —— mesh + material + 贴图 + 源
    // copy 全部 co-locate。
    fs::path modelDir = fs::path(kModelsDir) / basename;
    fs::create_directories(modelDir, ec);
    const std::string modelDirStr = modelDir.generic_string();

    // copy 源（ADR-008 4 件套之 copy 源）。失败不致命。
    fs::path destSrc = modelDir / src.filename();
    fs::copy_file(src, destSrc, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        ORANGE_LOG_WARN("FbxSceneImporter: copy source '{}' -> '{}' failed: {}",
                        srcPath, destSrc.generic_string(), ec.message());
        ec.clear();
    }

    // 为每个 mesh node 写一个独立 .mesh + .meta，Load 进 registry。OpenFBX 的
    // getMesh(i) 枚举所有 mesh node；按 Mesh* 指针去重（同一 mesh 被多 node
    // 引用——FBX 实例化——只建一次）。命名 <basename>_<meshname>.mesh。
    std::map<const ofbx::Mesh*, MeshBuildResult> meshResults;
    std::set<std::string> usedMeshStems;
    std::size_t writtenMeshes = 0;

    const auto hashOpt = ComputeFileHashFnv1a(srcPath);

    const int meshCount = scene->getMeshCount();
    for (int mi = 0; mi < meshCount; ++mi)
    {
        const ofbx::Mesh* m = scene->getMesh(mi);
        if (m == nullptr || meshResults.count(m) != 0) { continue; }

        std::string stem = (m->name[0] != '\0')
                               ? SanitizeName(m->name, "mesh" + std::to_string(mi))
                               : ("mesh" + std::to_string(mi));
        std::string fileStem = basename + "_" + stem;
        if (usedMeshStems.count(fileStem) != 0)
        {
            fileStem = basename + "_" + stem + "_" + std::to_string(mi);
        }
        usedMeshStems.insert(fileStem);

        const std::string meshPath = (modelDir / (fileStem + ".mesh")).generic_string();
        MeshBuildResult mb = BuildMeshAssetFromFbxMesh(*m, conv, meshPath, registry);
        if (!mb.handle.IsValid())
        {
            ORANGE_LOG_WARN("FbxSceneImporter: mesh '{}' 无可用三角几何 / 写盘失败，跳过",
                            fileStem);
            continue;
        }

        TextureMetaV1 meta{};
        meta.sourcePath = src.generic_string();
        meta.sourceHash = hashOpt.value_or(0);
        meta.handleId   = 0;
        WriteTextureMeta(MetaPathFor(meshPath), meta);

        meshResults[m] = std::move(mb);
        ++writtenMeshes;
    }

    // ---- per-mesh PBR material：为每个被引用 mesh 的每个 slot material 写
    //      .material + import 贴图，全局按 ofbx::Material* 去重。material name +
    //      贴图源路径必须在 scene->destroy() 之前抽出（Material* destroy 后悬空）。
    std::vector<const ofbx::Material*> orderedGlobalMats;  // 全局首次出现去重
    {
        std::set<const ofbx::Material*> seen;
        for (const auto& [meshPtr, mb] : meshResults)
        {
            for (const ofbx::Material* m : mb.slotMaterials)
            {
                if (m != nullptr && seen.insert(m).second)
                {
                    orderedGlobalMats.push_back(m);
                }
            }
        }
    }

    // 贴图 resolver：源路径 → ImportTextureToRegistry co-locate 到 modelDir 后的
    // 落盘 path。失败返回空 → 该槽不写。
    auto texResolver = [&](const std::string& srcTexPath) -> std::string {
        ImportResult tr = ImportTextureToRegistry(srcTexPath, registry, modelDirStr);
        if (tr.status == ImportStatus::Success && !tr.destPath.empty())
        {
            return tr.destPath;
        }
        ORANGE_LOG_WARN("FbxSceneImporter: material 贴图 '{}' import 失败，跳过该槽",
                        srcTexPath);
        return {};
    };

    // 在 destroy 之前把每个 material 的 scalar 蓝本 + 贴图源 + 名字抽出（Material*
    // destroy 后悬空）。destroy 后只做 ImportTexture + WriteMaterialFile。
    const fs::path fbxDir = src.parent_path();
    struct MatStaged
    {
        const ofbx::Material*                              key{nullptr};
        std::string                                        name;
        Material::MaterialFileData                         scalarData;
        std::vector<std::pair<std::uint32_t, std::string>> textureSrc;
    };
    std::vector<MatStaged> staged;
    staged.reserve(orderedGlobalMats.size());
    for (const ofbx::Material* m : orderedGlobalMats)
    {
        MatStaged s;
        s.key = m;
        if (m->name[0] != '\0') { s.name = m->name; }
        s.scalarData  = BuildFbxMaterialFileData(m, fbxDir, nullptr);
        s.textureSrc  = StageFbxTextureSources(m, fbxDir);
        staged.push_back(std::move(s));
    }

    // 收集 root node 在 scene->destroy() 之前（存裸 Object 指针，World 建完才 free）。
    const ofbx::Object* root = scene->getRoot();
    std::vector<const ofbx::Object*> roots;
    if (root != nullptr)
    {
        roots = ChildNodes(*root);  // root 自身是合成根，其直接子 node 是场景的真根
    }

    if (writtenMeshes == 0)
    {
        ORANGE_LOG_WARN("FbxSceneImporter: '{}' 无三角 mesh 几何（仅空 node / group？）",
                        srcPath);
    }

    // material 写盘（destroy 之后；scalar 蓝本已抽好，贴图经 resolver 落盘后入数组）。
    // ofbx::Material* → .material 路径。命名 <basename>_<materialName 或 mat 序号>。
    std::map<const ofbx::Material*, std::string> matPaths;
    std::set<std::string> usedMatFileNames;
    {
        std::size_t mi = 0;
        for (const MatStaged& s : staged)
        {
            std::string matName =
                SanitizeName(s.name, "mat" + std::to_string(mi));
            std::string fileStem = basename + "_" + matName;
            if (usedMatFileNames.count(fileStem) != 0)
            {
                fileStem = basename + "_" + matName + "_" + std::to_string(mi);
            }
            usedMatFileNames.insert(fileStem);

            Material::MaterialFileData mdata = s.scalarData;
            for (const auto& [binding, texSrc] : s.textureSrc)
            {
                const std::string dest = texResolver(texSrc);
                if (!dest.empty())
                {
                    mdata.textures.push_back({binding, dest});
                }
            }

            const std::string matPath =
                (modelDir / (fileStem + ".material")).generic_string();
            if (::Orange::Editor::Material::WriteMaterialFile(matPath, mdata))
            {
                ORANGE_LOG_INFO("FbxSceneImporter: wrote material '{}' (textures={})",
                                matPath, mdata.textures.size());
                matPaths[s.key] = matPath;
            }
            else
            {
                ORANGE_LOG_WARN("FbxSceneImporter: WriteMaterialFile '{}' 失败", matPath);
            }
            ++mi;
        }
    }

    // ---- headless sentinel MaterialInstance：一个 .material 路径一个实例。
    //      Scene::Save 把 entity 上的 materialInstance* 在 namedMaterialInstances
    //      表反查成 .material 路径写进 scene.json（运行期不解引用该指针）。
    std::vector<std::unique_ptr<MaterialInstance>> ownedInstances;
    std::unordered_map<const ofbx::Material*, MaterialInstance*> matInstances;
    std::unordered_map<std::string, MaterialInstance*> named;
    ownedInstances.reserve(matPaths.size());
    for (const auto& [matPtr, matPath] : matPaths)
    {
        auto inst = std::make_unique<MaterialInstance>(nullptr);
        MaterialInstance* raw = inst.get();
        ownedInstances.push_back(std::move(inst));
        matInstances[matPtr] = raw;
        named[matPath] = raw;
    }

    // 建 World 镜像 node 树。roots / meshResults 在 destroy 之前已收集（meshResults
    // 的 Mesh* / Material* 仅作 map key 不解引用）；ProcessNode 仍读 node->name /
    // getLocalTransform / getType / resolveObjectLink，故必须在 destroy **之前**建完。
    World world;
    std::size_t entityCount = 0;
    std::size_t cameraCount = 0;
    for (std::size_t ri = 0; ri < roots.size(); ++ri)
    {
        Entity rootE =
            ProcessNode(world, *roots[ri], conv, meshResults, matInstances, nodeCameras,
                        entityCount, cameraCount);
        if (auto* h = world.GetComponent<HierarchyComponent>(rootE))
        {
            h->sortIndex = static_cast<int>(ri);
        }
    }

    scene->destroy();  // World 已建完 + 几何 handle 已持有；FBX 结构不再需要

    if (entityCount == 0)
    {
        result.status  = ImportStatus::SourceReadFailed;
        result.message = "fbx scene has no importable nodes";
        ORANGE_LOG_ERROR("FbxSceneImporter: '{}': {}", srcPath, result.message);
        return result;
    }

    // Scene::Save —— assetRegistry 反查 mesh handle → 相对路径；
    // namedMaterialInstances 反查 materialInstance* → .material 路径。
    fs::create_directories(kScenesDir, ec);
    ::Orange::Engine::Scene::SaveOptions saveOpts;
    saveOpts.assetRegistry          = &registry;
    saveOpts.namedMaterialInstances = &named;
    auto saveRes = ::Orange::Engine::Scene::Save(world, scenePath, saveOpts);
    if (saveRes.IsErr())
    {
        result.status  = ImportStatus::AssetLoadFailed;
        result.message = "Scene::Save failed";
        ORANGE_LOG_ERROR("FbxSceneImporter: Scene::Save '{}' 失败", scenePath);
        return result;
    }

    // scene .meta（source hash），与 mesh .meta 对称。
    TextureMetaV1 sceneMeta{};
    sceneMeta.sourcePath = src.generic_string();
    sceneMeta.sourceHash = hashOpt.value_or(0);
    sceneMeta.handleId   = 0;
    if (!WriteTextureMeta(MetaPathFor(scenePath), sceneMeta))
    {
        result.status  = ImportStatus::MetaWriteFailed;
        result.message = "scene .meta write failed";
        ORANGE_LOG_ERROR("FbxSceneImporter: scene .meta '{}' 写失败",
                         MetaPathFor(scenePath));
        return result;
    }

    result.status   = ImportStatus::Success;
    result.destPath = scenePath;
    result.message  = "imported fbx scene (entities=" + std::to_string(entityCount) +
                      " meshes=" + std::to_string(writtenMeshes) +
                      " materials=" + std::to_string(matPaths.size()) +
                      " cameras=" + std::to_string(cameraCount) + ")";
    ORANGE_LOG_INFO("FbxSceneImporter: '{}' -> '{}' (entities={} meshes={} materials={} cameras={})",
                    srcPath, scenePath, entityCount, writtenMeshes, matPaths.size(),
                    cameraCount);
    return result;
}

}  // namespace Orange::Editor::Import
