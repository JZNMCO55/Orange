#include "GltfSceneImporter.h"

#include "GltfMaterialParse.h" // ExtractGltfMaterial / BuildMaterialFileData（material seam，与单 mesh importer 共用）
#include "ImportDispatcher.h"  // ImportTextureToRegistry（贴图 co-locate 导入）
#include "MeshTangentGen.h"    // GenerateMikkTSpaceTangents（高质量切线）
#include "MetaSidecar.h"
#include "../MaterialFileIO.h" // WriteMaterialFile（写 .material sidecar）

#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/SubMeshMaterialsComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

// glm 矩阵分解（has_matrix 的 node）走 gtx 实验扩展；仅本 TU 私有打开。
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// cgltf 仅取声明 —— CGLTF_IMPLEMENTATION 在 GltfImporter.cpp 一处 expand，
// 同一 lib / test exe 内链得到。
#include "cgltf.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
        using ::Orange::Engine::Animation::AnimationClip;
        using ::Orange::Engine::Animation::AnimationTrack;
        using ::Orange::Engine::Animation::AnimatorComponent;
        using ::Orange::Engine::Animation::ClipAnimator;
        using ::Orange::Engine::Animation::InterpMode;
        using ::Orange::Engine::Animation::Keyframe;
        using ::Orange::Engine::Animation::TrackValueType;
        using ::Orange::Engine::Asset::AssetRegistry;
        using ::Orange::Engine::Asset::MeshAsset;
        using ::Orange::Engine::Asset::MeshLoader;
        using ::Orange::Engine::Asset::VertexNormal3;
        using ::Orange::Engine::Asset::VertexPosition3;
        using ::Orange::Engine::Asset::VertexTangent4;
        using ::Orange::Engine::Asset::VertexUV2;
        using ::Orange::Engine::Render::Camera;
        using ::Orange::Engine::Render::DirectionalLight;
        using ::Orange::Engine::Render::MaterialInstance;
        using ::Orange::Engine::Render::PointLight;
        using ::Orange::Engine::Render::RenderableComponent;
        using ::Orange::Engine::Render::SpotLight;
        using ::Orange::Engine::Render::SubMeshMaterialsComponent;
        using ::Orange::Engine::Scene::HierarchyComponent;
        using ::Orange::Engine::Scene::NameComponent;
        using ::Orange::Engine::Scene::TransformComponent;

        namespace fs = std::filesystem;

        constexpr const char* kModelsDir = "assets/Models";
        constexpr const char* kScenesDir = "assets/scenes";

        // 一个 cgltf mesh 抽出的全部产物：几何 handle + 该 mesh 自身 slot 顺序的
        // material 指针列表。slot 顺序与 MeshAsset 的 SubMesh.materialSlot 一一对应
        // （第 i 段挂 slotMaterials[i]）；nullptr 表示该 slot 的 primitive 无 material
        // （落地端用引擎默认材质兜底）。单 primitive / 单 material 的 mesh 退化为
        // slotMaterials.size()==1 且不写 sub-mesh 段。
        struct MeshBuildResult
        {
            ::Orange::Engine::Asset::AssetHandle<MeshAsset> handle{};
            std::vector<const cgltf_material*>              slotMaterials;
        };

        const cgltf_accessor* FindAttribute(const cgltf_primitive& prim,
                                            cgltf_attribute_type   wanted,
                                            int                    wantedIndex = 0)
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
                case cgltf_result_success:
                    return "success";
                case cgltf_result_data_too_short:
                    return "data_too_short";
                case cgltf_result_unknown_format:
                    return "unknown_format";
                case cgltf_result_invalid_json:
                    return "invalid_json";
                case cgltf_result_invalid_gltf:
                    return "invalid_gltf";
                case cgltf_result_invalid_options:
                    return "invalid_options";
                case cgltf_result_file_not_found:
                    return "file_not_found";
                case cgltf_result_io_error:
                    return "io_error";
                case cgltf_result_out_of_memory:
                    return "out_of_memory";
                case cgltf_result_legacy_gltf:
                    return "legacy_gltf";
                default:
                    return "unknown";
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

        // 把一个 cgltf mesh（其全部 triangle primitive，不跨 mesh）抽成 MeshAsset，
        // 并按 primitive 切 sub-mesh、每段挂自己的 material slot —— 与单 mesh
        // importer（GltfImporter）的多 material 处理同款，只是单位是"一个 mesh"。
        //
        // material 划分（G2）：按"首次出现顺序"去重本 mesh 各 primitive 的
        // cgltf_material，orderedMats 下标即 materialSlot。nullptr（无 material 的
        // primitive）也占一个 slot —— 对应引擎默认材质，落地端不挂专属 instance。
        // 同一 cgltf_material* 在本 mesh 内复用同一 slot。单 material（含全无
        // material）退化回整 mesh 单段（不写 sub-mesh，渲染走整 mesh 单 material
        // 路径），与 G1 字节兼容。
        //
        // 返回的 slotMaterials 即 orderedMats（slot 顺序的 material 指针，含 nullptr）。
        // 失败返回空 handle 的 result。
        MeshBuildResult BuildMeshAssetFromGltfMesh(const cgltf_mesh&  mesh,
                                                   const std::string& meshPath,
                                                   AssetRegistry&     registry)
        {
            MeshBuildResult result;

            std::vector<VertexPosition3> positions;
            std::vector<VertexUV2>       uvs;
            std::vector<VertexNormal3>   normals;
            std::vector<std::uint32_t>   indices;

            bool fileHasUVs     = false;
            bool fileHasNormals = false;

            // material slot 去重（按指针，首次出现顺序）。nullptr 也占一个 slot。
            std::vector<const cgltf_material*>& orderedMats     = result.slotMaterials;
            auto                                slotForMaterial = [&orderedMats](const cgltf_material* m) -> std::uint32_t
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

            // per-primitive 索引区间 + 归属 slot；下面在 orderedMats.size() <= 1 时
            // 整段丢弃（退化回单段，向后兼容 G1 单材质路径）。
            std::vector<::Orange::Engine::Asset::SubMesh> subMeshes;

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
                // 本 primitive 在合并后 index buffer 里的起点 + 归属 slot。indexCount
                // 在 append 完索引后用差值回填（自动覆盖 indexed / 非 indexed 两路）。
                const std::uint32_t subMeshIndexOffset =
                    static_cast<std::uint32_t>(indices.size());
                const std::uint32_t subMeshSlot = slotForMaterial(prim.material);

                const cgltf_accessor* nrmAcc = FindAttribute(prim, cgltf_attribute_type_normal);
                const cgltf_accessor* uvAcc  = FindAttribute(prim, cgltf_attribute_type_texcoord, 0);

                const std::uint32_t baseIdx  = static_cast<std::uint32_t>(positions.size());
                const cgltf_size    vtxCount = posAcc->count;

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
                    const cgltf_accessor* idxAcc   = prim.indices;
                    const cgltf_size      idxCount = idxAcc->count;
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

                // 回填本 primitive 区间长度（覆盖 indexed / 非 indexed）。仅当确实
                // append 了索引才记录 sub-mesh。
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
                return result; // handle 留空 → caller 视作失败
            }

            if (!fileHasNormals)
            {
                normals.clear();
            }
            if (!fileHasUVs)
            {
                uvs.clear();
            }

            // MikkTSpace 切线 —— 需 UV + normal 齐备（re-weld 顶点，须在构造前算）。
            std::vector<VertexTangent4> tangents;
            bool                        haveTangents = false;
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

            // 多 material（orderedMats.size() > 1）才写 sub-mesh 段；单 material（含
            // 全无 material）退化回整 mesh 单段，subMeshes 留空 —— 与 G1 字节兼容、
            // 渲染端走整 mesh 单 material 路径。
            if (orderedMats.size() > 1)
            {
                out->SetSubMeshes(std::move(subMeshes));
            }

            auto saveRes = MeshLoader::Save(meshPath, *out);
            if (saveRes.IsErr())
            {
                ORANGE_LOG_ERROR("GltfSceneImporter: MeshLoader::Save '{}' 失败", meshPath);
                result.slotMaterials.clear();
                return result;
            }
            auto loadRes = registry.Load<MeshAsset>(meshPath);
            if (loadRes.IsErr())
            {
                ORANGE_LOG_ERROR("GltfSceneImporter: registry.Load<MeshAsset> '{}' 失败", meshPath);
                result.slotMaterials.clear();
                return result;
            }
            result.handle = loadRes.Value();
            return result;
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
            glm::vec3       skew{};
            glm::vec4       perspective{};
            glm::decompose(m, outScale, outRot, outPos, skew, perspective);
        }

        // 该光是否需要方向（directional / spot）—— 决定 ProcessNode 是否把 -Z→-Y 桥接
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
            const float     intensity = light.intensity / kGltfLuminousEfficacy;
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
                    p.range = (light.range > 0.0f) ? light.range : 10.0f;
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

        // glTF perspective camera 的 aspectRatio / zfar 是可选字段（aspect 缺省语义 =
        // "跟视口宽高比"、zfar 缺省 = 无限远）。本 importer 把投影**烘成** Camera.projection
        // （现有 Render::Camera component 只存 view/projection 矩阵对，不存参数化 fov/near/far），
        // 故对缺省值取保守默认：aspect 16/9、far 1000。烘焙的代价是 re-import / re-edit 按矩阵
        // 不按原始 fov 字段——Camera 当前无参数化字段是已知 MVP 限制（真要可重编 fov 需扩
        // Camera 加 fov/aspect/near/far + Inspector，属后续）。
        constexpr float kGltfDefaultCameraAspect = 16.0f / 9.0f;
        constexpr float kGltfDefaultCameraFar    = 1000.0f;

        // glTF camera → 引擎 Render::Camera component。透视用 yfov/aspect/znear/zfar 烘成
        // Camera::Perspective；正交的 xmag/ymag 是视图半宽 / 半高，映成 left/right/bottom/top
        // 调 Camera::Orthographic。相机位姿（eye/forward/up）不进 component —— view 留单位，
        // 由 entity Transform 决定（CameraFrustumGizmoPlugin 从 Transform 推 forward = rotation*-Z）。
        // glTF 相机同样看本地 -Z（与引擎约定一致），故 ProcessNode 不对相机 node 做灯光那种
        // -Z→-Y 桥接，直接写 node local rotation 即可（finalRot 默认 = node rotation）。
        void AddGltfCamera(World& world, Entity e, const cgltf_camera& camera)
        {
            switch (camera.type)
            {
                case cgltf_camera_type_perspective:
                {
                    const cgltf_camera_perspective& p      = camera.data.perspective;
                    const float                     aspect = (p.has_aspect_ratio && p.aspect_ratio > 0.0f)
                                                                 ? p.aspect_ratio
                                                                 : kGltfDefaultCameraAspect;
                    const float                     zfar   = (p.has_zfar && p.zfar > 0.0f)
                                                                 ? p.zfar
                                                                 : kGltfDefaultCameraFar;
                    world.AddComponent<Camera>(
                        e, Camera::Perspective(p.yfov, aspect, p.znear, zfar));
                    break;
                }
                case cgltf_camera_type_orthographic:
                {
                    const cgltf_camera_orthographic& o = camera.data.orthographic;
                    world.AddComponent<Camera>(
                        e, Camera::Orthographic(-o.xmag, o.xmag, -o.ymag, o.ymag,
                                                o.znear, o.zfar));
                    break;
                }
                default:
                    ORANGE_LOG_WARN("GltfSceneImporter: 未知 glTF camera 类型，跳过");
                    break;
            }
        }

        // 把一个 mesh 的 slot material 列表（cgltf_material* → sentinel MaterialInstance*）
        // 接到 entity 的 Renderable / SubMeshMaterialsComponent 上（G2）。
        //
        // 材质实例是 headless sentinel（见 RunGltfSceneImportToRegistry 顶部说明）：
        // Scene::Save 只需把它在 namedMaterialInstances 表里反查成 .material 路径写进
        // scene.json，运行期不解引用该指针。Load 端（编辑器）经 materialResolver
        // 从 .material 文件 lazy-create 真实 instance。
        //
        // 单 material（slotMaterials.size() <= 1）→ 设 Renderable.materialInstance
        // （与单材质模型 drop 行为对齐，scene 加载即带材质而非默认）。
        // 多 material → 挂 SubMeshMaterialsComponent（各 sub-mesh 段独立材质）+ slot 0
        // 当 Renderable.materialInstance 兜底（与 SyncSubMeshMaterialsForMesh 同款语义）。
        void AttachMeshMaterials(
            World& world, Entity e, RenderableComponent& rc,
            const std::vector<const cgltf_material*>&                           slotMaterials,
            const std::unordered_map<const cgltf_material*, MaterialInstance*>& matInstances)
        {
            if (slotMaterials.empty())
            {
                return; // 纯几何无 material → 默认材质
            }

            auto resolve = [&](const cgltf_material* m) -> MaterialInstance*
            {
                if (m == nullptr)
                {
                    return nullptr;
                }
                auto it = matInstances.find(m);
                return (it != matInstances.end()) ? it->second : nullptr;
            };

            if (slotMaterials.size() == 1)
            {
                // 单 material：直接设 Renderable.materialInstance（slot 0）。
                rc.materialInstance = resolve(slotMaterials[0]);
                return;
            }

            // 多 material：各 slot 解析成 instance，挂 SubMeshMaterialsComponent。
            SubMeshMaterialsComponent smc;
            smc.slots.reserve(slotMaterials.size());
            for (const cgltf_material* m : slotMaterials)
            {
                smc.slots.push_back(resolve(m));
            }
            // slot 0 兜底到 Renderable.materialInstance（与单 material 语义一致，渲染端
            // 越界 / 空 slot 回退到它）。
            if (!smc.slots.empty() && smc.slots[0] != nullptr)
            {
                rc.materialInstance = smc.slots[0];
            }
            world.AddComponent<SubMeshMaterialsComponent>(e, std::move(smc));
        }

        // glTF sampler interpolation → 引擎 InterpMode。CUBICSPLINE 降级 Linear（引擎曲线
        // 模型用 keyframe 切线表达 Bezier 时序缓动，与 glTF CUBICSPLINE 的"每帧带 in/out 切线
        // 向量"语义不同——MVP 不做 cubic-spline 重建，仅取关键帧值做线性插值）。降级在调用处
        // 发 WARN（这里纯映射，不发日志）。
        InterpMode GltfInterpToInterpMode(cgltf_interpolation_type interp)
        {
            switch (interp)
            {
                case cgltf_interpolation_type_step:
                    return InterpMode::Step;
                case cgltf_interpolation_type_linear:
                    return InterpMode::Linear;
                case cgltf_interpolation_type_cubic_spline:
                    return InterpMode::Linear; // 降级（见上）
                default:
                    return InterpMode::Linear;
            }
        }

        // 从一条 cgltf animation channel 抽出关键帧，append 进给定 track。sampler.input 是
        // 关键帧时间（标量 accessor），sampler.output 是值 accessor（translation/scale =
        // VEC3、rotation = VEC4 四元数）。compsPerKey = 每帧取几个分量（3 或 4）。
        // CUBICSPLINE 的 output 每帧有 3 组（in-tangent / value / out-tangent），降级时只取
        // 中间的 value（stride 3、偏移 1）。
        //
        // cgltf 生命周期：本函数读 accessor 字节，必须在 cgltf_free 之前调用。
        void AppendChannelKeys(const cgltf_animation_channel& channel, AnimationTrack& track,
                               int compsPerKey)
        {
            const cgltf_animation_sampler* sampler = channel.sampler;
            if (sampler == nullptr || sampler->input == nullptr || sampler->output == nullptr)
            {
                return;
            }
            const cgltf_accessor* timeAcc  = sampler->input;
            const cgltf_accessor* valAcc   = sampler->output;
            const cgltf_size      keyCount = timeAcc->count;
            if (keyCount == 0)
            {
                return;
            }

            const InterpMode interp = GltfInterpToInterpMode(sampler->interpolation);
            const bool       cubic  = (sampler->interpolation == cgltf_interpolation_type_cubic_spline);
            // CUBICSPLINE：output 每帧 3 组（tangent_in / value / tangent_out）→ stride 3、取
            // 中间组。非 cubic：每帧 1 组。
            const cgltf_size valuesPerKey = cubic ? 3 : 1;
            const cgltf_size valueSlot    = cubic ? 1 : 0; // 取 value 组（cubic 跳过 in-tangent）

            track.keys.reserve(track.keys.size() + keyCount);
            for (cgltf_size k = 0; k < keyCount; ++k)
            {
                float t = 0.0f;
                cgltf_accessor_read_float(timeAcc, k, &t, 1);

                Keyframe key;
                key.time   = t;
                key.interp = interp;
                // value accessor 的元素下标：cubic 时每帧跨 3 组，取中间 value 组。
                const cgltf_size valIndex = k * valuesPerKey + valueSlot;
                float            buf[4]   = {0, 0, 0, 0};
                cgltf_accessor_read_float(valAcc, valIndex, buf,
                                          static_cast<cgltf_size>(compsPerKey));
                key.value = glm::vec4(buf[0], buf[1], buf[2], buf[3]);
                track.keys.push_back(key);
            }
        }

        // 解析 cgltf_data 里所有 animation，构建"被驱动 node → 合并 AnimationClip"映射。
        // 一个 node 被多条 channel（含跨多个 glTF animation）驱动时，全部 channel 合并进该
        // node 的单个 clip（per-node 一个 ClipAnimator）。targetName 约定：
        //   translation → "position"（Vec3）、scale → "scale"（Vec3）、
        //   rotation    → "rotation.quat"（Quat，最短弧 slerp，避欧拉 gimbal）。
        // weights（morph target）跳过（引擎暂无 morph 通道）。
        //
        // MVP 限制：多个 glTF animation 被合并（不保留 animation 名 / 不分 clip 切换）；
        // 单 animation 的常见 DCC 导出完全正确。坐标系：node TRS 是 local（A1.2 后导入走
        // local），动画 key 同为 node-local TRS，直接进 local track，无需轴桥接（与 mesh
        // node transform 同款；不学灯光的 R-bridging）。
        //
        // cgltf 生命周期：必须在 cgltf_free 之前调用（读 accessor 字节 + node 指针）。
        // 返回的 clip 用裸 node 指针当 key（仅 map 查找，不解引用）。
        std::map<const cgltf_node*, AnimationClip> ParseAnimations(const cgltf_data& data)
        {
            std::map<const cgltf_node*, AnimationClip> nodeClips;
            bool                                       warnedCubic = false;

            for (cgltf_size ai = 0; ai < data.animations_count; ++ai)
            {
                const cgltf_animation& anim = data.animations[ai];
                for (cgltf_size ci = 0; ci < anim.channels_count; ++ci)
                {
                    const cgltf_animation_channel& channel    = anim.channels[ci];
                    const cgltf_node*              targetNode = channel.target_node;
                    if (targetNode == nullptr || channel.sampler == nullptr)
                    {
                        continue;
                    }
                    if (channel.target_path == cgltf_animation_path_type_weights ||
                        channel.target_path == cgltf_animation_path_type_invalid)
                    {
                        continue; // morph weights / 非法 path 跳过
                    }

                    if (channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline &&
                        !warnedCubic)
                    {
                        ORANGE_LOG_WARN("GltfSceneImporter: animation 含 CUBICSPLINE 插值，降级为 "
                                        "Linear（仅取关键帧值，不重建样条切线）");
                        warnedCubic = true;
                    }

                    AnimationClip& clip       = nodeClips[targetNode]; // 按需建该 node 的 clip
                    const char*    targetName = nullptr;
                    TrackValueType vt         = TrackValueType::Vec3;
                    int            comps      = 3;
                    switch (channel.target_path)
                    {
                        case cgltf_animation_path_type_translation:
                            targetName = "position";
                            vt         = TrackValueType::Vec3;
                            comps      = 3;
                            break;
                        case cgltf_animation_path_type_scale:
                            targetName = "scale";
                            vt         = TrackValueType::Vec3;
                            comps      = 3;
                            break;
                        case cgltf_animation_path_type_rotation:
                            targetName = "rotation.quat";
                            vt         = TrackValueType::Quat;
                            comps      = 4;
                            break;
                        default:
                            continue; // 已在上面过滤
                    }

                    // 同一 node 同一 path 多 channel（异常但 spec 不禁）→ 复用同名 track。
                    ::Orange::Engine::Animation::AnimationTrack* tr = FindTrack(clip, targetName);
                    if (tr == nullptr)
                    {
                        AnimationTrack newTrack;
                        newTrack.targetName = targetName;
                        newTrack.valueType  = vt;
                        clip.tracks.push_back(std::move(newTrack));
                        tr = &clip.tracks.back();
                    }
                    AppendChannelKeys(channel, *tr, comps);
                    ::Orange::Engine::Animation::SortTrackKeys(*tr); // 维持 SampleTrack 升序不变量
                }
            }

            // 每个 clip 命名 + 据关键帧推时长 + 默认 **play-once（loop=false）**。glTF 不定义
            // loop，loop 是播放策略而非数据——默认不循环（Unity 风格：用户在编辑器按需开 loop）
            // 更可预测：loop 时 Seek(duration) 会 fmod 回卷到 t0，导入动画到末帧应停在末值而非
            // 跳回起点。
            for (auto& [node, clip] : nodeClips)
            {
                clip.name = "imported_anim";
                clip.loop = false;
                ::Orange::Engine::Animation::RecomputeClipDuration(clip);
            }
            return nodeClips;
        }

        // 递归把 node 子树建进 World，返回本 node 对应的 Entity。本 node 的 parent /
        // 兄弟链由调用方填（调用方知道兄弟顺序）；本函数负责 firstChild + 各子节点的
        // parent / 兄弟链 + 子树递归。
        //
        // 关键：不跨 AddComponent / CreateEntity 持有 component 指针（entt storage
        // realloc 会悬空）—— firstChild / 子链的 patch 全部在"该 node 子树建完、不再
        // 新增实体"之后用现取的指针写。SubMeshMaterialsComponent 的 AddComponent 也
        // 在 mesh 那一步之后立即做（此时不再取 rc 指针，先把 rc 值填好再 Add）。
        Entity ProcessNode(
            World& world, const cgltf_node& node,
            const std::map<const cgltf_mesh*, MeshBuildResult>&                 meshResults,
            const std::unordered_map<const cgltf_material*, MaterialInstance*>& matInstances,
            const std::map<const cgltf_node*, AnimationClip>&                   nodeClips,
            std::vector<std::pair<Entity, ClipAnimator*>>&                      outAnimTargets,
            std::size_t& outEntityCount, std::size_t& outLightCount, std::size_t& outCameraCount,
            std::size_t& outAnimCount)
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

            // 光源（directional/spot）方向沿 glTF node 本地 -Z；引擎 ComputeXxxWorldDir =
            // rotation*(0,-1,0)（本地 -Y 为前向）。光源消费者现读 **world** rotation
            //（A1.1 step 2 已切，沿 hierarchy 累积），故只需把 node **local** rotation 桥接
            // -Z→-Y（绕 +X 转 90°，把 -Y 映到 -Z）写进 entity local rotation——引擎累积父
            // 变换后 consumer 得正确世界方向（root 灯 + 非 root〔含旋转父〕灯都对）。这比旧
            // 的"world 光向直接编码进 local"更对：旧法在非 root 灯上会被父旋转二次应用。
            // mesh node + point 光保留 local rotation（mesh 走 Collect 累积，point 无方向）。
            glm::quat finalRot = rot;
            if (LightNeedsDirection(node.light))
            {
                finalRot = rot * glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            }
            world.AddComponent<TransformComponent>(e, TransformComponent{pos, finalRot, scl});

            if (node.light != nullptr)
            {
                AddGltfLight(world, e, *node.light);
                ++outLightCount;
            }

            if (node.camera != nullptr)
            {
                AddGltfCamera(world, e, *node.camera);
                ++outCameraCount;
            }

            if (node.mesh != nullptr)
            {
                auto it = meshResults.find(node.mesh);
                if (it != meshResults.end() && it->second.handle.IsValid())
                {
                    RenderableComponent rc;
                    rc.mesh = it->second.handle;
                    // G2：接 per-mesh PBR material。单 material → Renderable.materialInstance；
                    // 多 material → SubMeshMaterialsComponent（先把 rc 值填好，AttachMeshMaterials
                    // 内再 AddComponent<SubMeshMaterialsComponent>，不持有 rc 指针）。
                    AttachMeshMaterials(world, e, rc, it->second.slotMaterials, matInstances);
                    world.AddComponent<RenderableComponent>(e, rc);
                }
            }

            // 动画：该 node 被任意 channel 驱动 → 挂 AnimatorComponent(ClipAnimator)。clip 在
            // ParseAnimations 阶段已据 channel 构建（position/scale Vec3 + rotation.quat Quat）。
            // 此处只设 target=nullptr 挂上；SetTarget 必须延后到**所有实体建完**——子节点的
            // CreateEntity 会让 TransformComponent storage realloc，跨建期持 Transform 指针会
            // 悬空（与 firstChild patch 同款约束）。收集 (entity, ClipAnimator*) 待最后统一接。
            // ClipAnimator* 取自 unique_ptr 指向的堆对象，跨 AnimatorComponent storage 搬动稳定
            //（move 只搬 unique_ptr 指针、堆对象不动），故可安全跨建期持有。
            {
                auto cit = nodeClips.find(&node);
                if (cit != nodeClips.end() && !cit->second.tracks.empty())
                {
                    auto              clipAnim = std::make_unique<ClipAnimator>(cit->second); // clip 拷入
                    ClipAnimator*     raw      = clipAnim.get();
                    AnimatorComponent ac;
                    ac.animator = std::move(clipAnim);
                    world.AddComponent<AnimatorComponent>(e, std::move(ac));
                    outAnimTargets.emplace_back(e, raw);
                    ++outAnimCount;
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
                    ProcessNode(world, *node.children[ci], meshResults, matInstances,
                                nodeClips, outAnimTargets, outEntityCount, outLightCount,
                                outCameraCount, outAnimCount));
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
    } // namespace

    ImportResult RunGltfSceneImportToRegistry(std::string_view srcPath,
                                              AssetRegistry&   registry)
    {
        ImportResult result{};

        fs::path        src(srcPath.begin(), srcPath.end());
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
        cgltf_options     options{};
        cgltf_data*       data = nullptr;
        cgltf_result      rc   = cgltf_parse_file(&options, srcStr.c_str(), &data);
        if (rc != cgltf_result_success || data == nullptr)
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = std::string("cgltf_parse_file: ") + CgltfResultToString(rc);
            ORANGE_LOG_ERROR("GltfSceneImporter: '{}': {}", srcPath, result.message);
            if (data != nullptr)
            {
                cgltf_free(data);
            }
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

        // 每模型一个子目录 assets/Models/<basename>/ —— 各 mesh + material + 贴图 +
        // 源 copy 全部 co-locate。
        fs::path modelDir = fs::path(kModelsDir) / basename;
        fs::create_directories(modelDir, ec);
        const std::string modelDirStr = modelDir.generic_string();

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

        // 为每个被任何 node 引用的 cgltf mesh 写一个 .mesh + .meta，Load 进 registry，
        // 同时拿到该 mesh 的 slot material 列表（按 primitive 切 sub-mesh，G2）。
        // 同一 mesh 被多 node 引用只建一次（按指针去重）。命名 <basename>_<meshname>.mesh，
        // 同名 / 匿名退化用 mesh 序号。
        std::map<const cgltf_mesh*, MeshBuildResult> meshResults;
        std::set<std::string>                        usedMeshStems;
        std::size_t                                  writtenMeshes = 0;

        const auto ensureMesh = [&](const cgltf_mesh* m) -> bool
        {
            if (m == nullptr)
            {
                return false;
            }
            if (meshResults.count(m) != 0)
            {
                return true;
            }

            const std::size_t meshIdx =
                static_cast<std::size_t>(m - data->meshes);
            std::string stem     = (m->name != nullptr && m->name[0] != '\0')
                                       ? SanitizeName(m->name)
                                       : ("mesh" + std::to_string(meshIdx));
            std::string fileStem = basename + "_" + stem;
            if (usedMeshStems.count(fileStem) != 0)
            {
                fileStem = basename + "_" + stem + "_" + std::to_string(meshIdx);
            }
            usedMeshStems.insert(fileStem);

            const std::string meshPath = (modelDir / (fileStem + ".mesh")).generic_string();
            MeshBuildResult   mb       = BuildMeshAssetFromGltfMesh(*m, meshPath, registry);
            if (!mb.handle.IsValid())
            {
                ORANGE_LOG_WARN("GltfSceneImporter: mesh '{}' 无可用三角几何 / 写盘失败，跳过",
                                fileStem);
                return false;
            }

            // mesh .meta（source hash 记源 gltf）。
            const auto    hashOpt = ComputeFileHashFnv1a(srcPath);
            TextureMetaV1 meta{};
            meta.sourcePath = src.generic_string();
            meta.sourceHash = hashOpt.value_or(0);
            meta.handleId   = 0;
            WriteTextureMeta(MetaPathFor(meshPath), meta);

            meshResults[m] = std::move(mb);
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

        // ---- per-mesh PBR material（G2）：为每个被引用 mesh 的每个 slot material 写
        //      .material + import 贴图，全局按 cgltf_material* 去重（同一 material 被
        //      多 mesh / 多 primitive 引用只写一个 .material + 一个 instance）。
        //
        // 关键 cgltf 生命周期：ExtractGltfMaterial 必须在 cgltf_free 之前做（读
        //      cgltf_material / image / buffer_view 字节）；material name 也在这里 copy
        //      出来。下面所有解引用 cgltf_material* 的逻辑都在 cgltf_free 之前完成，
        //      之后 matInstances 只用裸指针当 key（不解引用，仅做 map 查找）。
        //
        // 内嵌贴图（.glb buffer_view / data: URI）提取复用单 mesh importer 的落盘 +
        //      co-locate 流程：先把 image 字节写到 modelDir，再回填到 GltfMatInfo 的
        //      *Src，之后与外部贴图走同一条 ImportTextureToRegistry。同一 image 被多
        //      material 引用只写盘一次（embeddedCache 跨 material 共享）。
        std::vector<const cgltf_material*> orderedGlobalMats; // 全局首次出现顺序去重
        {
            std::set<const cgltf_material*> seen;
            for (const auto& [meshPtr, mb] : meshResults)
            {
                for (const cgltf_material* m : mb.slotMaterials)
                {
                    if (m != nullptr && seen.insert(m).second)
                    {
                        orderedGlobalMats.push_back(m);
                    }
                }
            }
        }

        // 内嵌 image 提取走单 mesh importer 同款 ExtractEmbeddedImageToDisk —— 它是
        // GltfImporter.cpp 的 file-static helper，本 TU 取不到。改用最简等价：仅外部
        // uri 贴图经 ResolveTextureSource 解析（ExtractGltfMaterial 已做）；内嵌 image
        // 在本 scene importer 暂走"按 images[] 下标取字节落盘"的本地实现（与单 mesh
        // importer 行为对齐：内嵌 baseColor 等被提取成 modelDir 下真实文件）。
        auto extractEmbeddedToDisk = [&](int imageIndex) -> std::string
        {
            if (imageIndex < 0 ||
                static_cast<cgltf_size>(imageIndex) >= data->images_count)
            {
                return {};
            }
            const cgltf_image& image = data->images[static_cast<cgltf_size>(imageIndex)];

            // 取字节：优先 GLB buffer_view，其次 data: URI（base64）。
            std::vector<unsigned char> bytes;
            if (image.buffer_view != nullptr)
            {
                const cgltf_size    size = image.buffer_view->size;
                const std::uint8_t* p    = cgltf_buffer_view_data(image.buffer_view);
                if (p != nullptr && size > 0)
                {
                    bytes.assign(p, p + size);
                }
            }
            else if (image.uri != nullptr && std::strncmp(image.uri, "data:", 5) == 0)
            {
                const char* comma = std::strchr(image.uri, ',');
                if (comma != nullptr)
                {
                    // base64 解码（与单 mesh importer 同款最小实现）。
                    const auto charValue = [](char c) -> int
                    {
                        if (c >= 'A' && c <= 'Z')
                        {
                            return c - 'A';
                        }
                        if (c >= 'a' && c <= 'z')
                        {
                            return c - 'a' + 26;
                        }
                        if (c >= '0' && c <= '9')
                        {
                            return c - '0' + 52;
                        }
                        if (c == '+')
                        {
                            return 62;
                        }
                        if (c == '/')
                        {
                            return 63;
                        }
                        return -1;
                    };
                    int accum = 0, bits = 0;
                    for (const char* q = comma + 1; *q != '\0'; ++q)
                    {
                        const int v = charValue(*q);
                        if (v < 0)
                        {
                            continue;
                        }
                        accum = (accum << 6) | v;
                        bits += 6;
                        if (bits >= 8)
                        {
                            bits -= 8;
                            bytes.push_back(static_cast<unsigned char>((accum >> bits) & 0xFF));
                        }
                    }
                }
            }
            if (bytes.empty())
            {
                return {};
            }

            // 扩展名：mime_type / magic（JPEG FF D8 FF）→ jpg，否则 png。
            std::string ext = "png";
            if (image.mime_type != nullptr &&
                (std::strstr(image.mime_type, "jpeg") != nullptr ||
                 std::strstr(image.mime_type, "jpg") != nullptr))
            {
                ext = "jpg";
            }
            else if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 &&
                     bytes[2] == 0xFF)
            {
                ext = "jpg";
            }

            std::string baseName = (image.name != nullptr && image.name[0] != '\0')
                                       ? std::string(image.name)
                                       : ("image_" + std::to_string(imageIndex));
            baseName             = SanitizeName(baseName);

            const fs::path dst = modelDir / (baseName + "." + ext);
            std::ofstream  ofs(dst, std::ios::binary);
            if (!ofs)
            {
                ORANGE_LOG_WARN("GltfSceneImporter: 内嵌贴图写盘失败 '{}'",
                                dst.generic_string());
                return {};
            }
            ofs.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            ofs.close();
            return dst.generic_string();
        };

        std::map<int, std::string> embeddedCache; // images[] 下标 → 落盘路径
        auto                       resolveEmbedded = [&](int imageIndex) -> std::string
        {
            if (imageIndex < 0)
            {
                return {};
            }
            auto it = embeddedCache.find(imageIndex);
            if (it != embeddedCache.end())
            {
                return it->second;
            }
            const std::string path    = extractEmbeddedToDisk(imageIndex);
            embeddedCache[imageIndex] = path;
            return path;
        };

        // 贴图 resolver：源路径 → 经 ImportTextureToRegistry co-locate 到 modelDir 后
        // 的落盘 path（与单 mesh importer 同款）。失败返回空 → 该槽不写。
        auto texResolver = [&](const std::string& srcTexPath) -> std::string
        {
            ImportResult tr = ImportTextureToRegistry(srcTexPath, registry, modelDirStr);
            if (tr.status == ImportStatus::Success && !tr.destPath.empty())
            {
                return tr.destPath;
            }
            ORANGE_LOG_WARN("GltfSceneImporter: material 贴图 '{}' import 失败，跳过该槽",
                            srcTexPath);
            return {};
        };

        // cgltf_material* → .material 落盘路径（写盘 + dedup 都在 cgltf_free 之前）。
        std::map<const cgltf_material*, std::string> matPaths;
        std::set<std::string>                        usedMatFileNames;
        for (std::size_t mi = 0; mi < orderedGlobalMats.size(); ++mi)
        {
            const cgltf_material* m    = orderedGlobalMats[mi];
            GltfMatInfo           info = ExtractGltfMaterial(m, src.parent_path(), data);
            if (!info.present)
            {
                continue; // 理论上不会（orderedGlobalMats 已过滤 nullptr）
            }
            // 内嵌图像（外部 uri 解析为空时）回填 *Src。
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

            // material name → 文件名安全片段；空 / 同名退化用全局序号。命名
            // <basename>_<materialName 或 mat 序号>.material（与单 mesh importer
            // 的 slot>=1 命名同风格；scene importer 无"slot 0 = <stem>.material"
            // 的历史约束，统一带 material 名后缀，多个 mesh 的材质也不冲突）。
            std::string matName  = (m->name != nullptr && m->name[0] != '\0')
                                       ? SanitizeName(m->name)
                                       : ("mat" + std::to_string(mi));
            std::string fileStem = basename + "_" + matName;
            if (usedMatFileNames.count(fileStem) != 0)
            {
                fileStem = basename + "_" + matName + "_" + std::to_string(mi);
            }
            usedMatFileNames.insert(fileStem);

            ::Orange::Editor::Material::MaterialFileData mdata =
                BuildMaterialFileData(info, texResolver);
            const std::string matPath =
                (modelDir / (fileStem + ".material")).generic_string();
            if (::Orange::Editor::Material::WriteMaterialFile(matPath, mdata))
            {
                ORANGE_LOG_INFO("GltfSceneImporter: wrote material '{}' (textures={})",
                                matPath, mdata.textures.size());
                matPaths[m] = matPath;
            }
            else
            {
                ORANGE_LOG_WARN("GltfSceneImporter: WriteMaterialFile '{}' 失败", matPath);
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
        // roots 在 cgltf_free 之前收集（存裸 node 指针，下面 World 建完才 free）。
        std::vector<const cgltf_node*> roots;
        const cgltf_scene*             scene =
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

        // ---- 建 headless sentinel MaterialInstance：一个 .material 路径一个实例。
        //      Scene::Save 只需把 entity 上的 materialInstance* 在 namedMaterialInstances
        //      表里反查成 .material 路径写进 scene.json（运行期不解引用该指针，故
        //      sentinel 绑 null Material 即可，构造廉价、无 Vulkan 依赖）。Load 端
        //      （编辑器）经 materialResolver / EnsureMaterialInstance 从 .material 文件
        //      lazy-create 真实 instance，与 mesh 的磁盘加载对称。
        //
        //      ownedInstances 持有实例生命周期，须覆盖 Scene::Save 调用（World 在它
        //      之前析构 / 先用完）。matInstances 给 ProcessNode 按 cgltf_material* 查
        //      sentinel；named 给 Scene::Save 按指针反查路径。
        std::vector<std::unique_ptr<MaterialInstance>>               ownedInstances;
        std::unordered_map<const cgltf_material*, MaterialInstance*> matInstances;
        std::unordered_map<std::string, MaterialInstance*>           named;
        ownedInstances.reserve(matPaths.size());
        for (const auto& [matPtr, matPath] : matPaths)
        {
            auto              inst = std::make_unique<MaterialInstance>(nullptr);
            MaterialInstance* raw  = inst.get();
            ownedInstances.push_back(std::move(inst));
            matInstances[matPtr] = raw;
            named[matPath]       = raw;
        }

        // ---- 解析 node TRS 动画 → per-node AnimationClip（必须在 cgltf_free 之前，读
        //      sampler accessor 字节 + target node 指针）。被驱动 node 的实体在 ProcessNode
        //      挂 AnimatorComponent(ClipAnimator)；rotation 走 Quat 轨道（最短弧 slerp，
        //      避欧拉 gimbal）。
        const std::map<const cgltf_node*, AnimationClip> nodeClips = ParseAnimations(*data);

        // 建 World 镜像 node 树。
        World       world;
        std::size_t entityCount = 0;
        std::size_t lightCount  = 0;
        std::size_t cameraCount = 0;
        std::size_t animCount   = 0;
        // 被动画驱动实体的 (entity, ClipAnimator*)：建完全部实体后统一 SetTarget——建期
        // CreateEntity 会 realloc TransformComponent storage，跨建期持 Transform 指针悬空。
        std::vector<std::pair<Entity, ClipAnimator*>> animTargets;
        for (std::size_t ri = 0; ri < roots.size(); ++ri)
        {
            Entity rootE = ProcessNode(world, *roots[ri], meshResults, matInstances,
                                       nodeClips, animTargets, entityCount, lightCount,
                                       cameraCount, animCount);
            // 根：parent 留 Invalid，用 sortIndex 定根间顺序（HierarchyComponent 约定）。
            if (auto* h = world.GetComponent<HierarchyComponent>(rootE))
            {
                h->sortIndex = static_cast<int>(ri);
            }
        }

        // 全部实体建完（storage 不再增长）→ 把各 ClipAnimator 的 target 接到本 entity 的
        // TransformComponent（与 Scene::Load 重建路径同款"attach 后 SetTarget"心智）。
        for (const auto& [animEntity, clipAnim] : animTargets)
        {
            clipAnim->SetTarget(world.GetComponent<TransformComponent>(animEntity));
        }

        if (entityCount == 0)
        {
            result.status  = ImportStatus::SourceReadFailed;
            result.message = "glTF scene has no importable nodes";
            ORANGE_LOG_ERROR("GltfSceneImporter: '{}': {}", srcPath, result.message);
            cgltf_free(data);
            return result;
        }

        cgltf_free(data); // World 已持有几何 handle + sentinel + clip 数据；cgltf 结构不再需要

        // Scene::Save —— assetRegistry 反查 mesh handle → 相对路径；namedMaterialInstances
        // 反查 materialInstance* → .material 路径写进 scene.json（materialInstanceId /
        // subMeshMaterials slots）。scenePath / basename 已在函数顶部（hash 短路处）算好。
        fs::create_directories(kScenesDir, ec);

        ::Orange::Engine::Scene::SaveOptions saveOpts;
        saveOpts.assetRegistry          = &registry;
        saveOpts.namedMaterialInstances = &named;
        auto saveRes                    = ::Orange::Engine::Scene::Save(world, scenePath, saveOpts);
        if (saveRes.IsErr())
        {
            result.status  = ImportStatus::AssetLoadFailed;
            result.message = "Scene::Save failed";
            ORANGE_LOG_ERROR("GltfSceneImporter: Scene::Save '{}' 失败", scenePath);
            return result;
        }

        // scene .meta（source hash），与 mesh .meta 对称。
        const auto    sceneHashOpt = ComputeFileHashFnv1a(srcPath);
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
                         " materials=" + std::to_string(matPaths.size()) +
                         " lights=" + std::to_string(lightCount) +
                         " cameras=" + std::to_string(cameraCount) +
                         " animations=" + std::to_string(animCount) + ")";
        ORANGE_LOG_INFO("GltfSceneImporter: '{}' -> '{}' (entities={} meshes={} materials={} lights={} cameras={} animations={})",
                        srcPath, scenePath, entityCount, writtenMeshes, matPaths.size(),
                        lightCount, cameraCount, animCount);
        return result;
    }

} // namespace Orange::Editor::Import
