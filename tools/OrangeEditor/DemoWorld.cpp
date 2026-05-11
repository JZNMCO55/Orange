// DemoWorld 实现 —— 见 DemoWorld.h 的注释。

#include "DemoWorld.h"

#include "EditorHierarchy.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdio>
#include <utility>
#include <vector>

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakePlaneMesh(float halfSize)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    std::vector<VertexPosition3> positions = {
        {-halfSize, 0.0f, -halfSize},
        { halfSize, 0.0f, -halfSize},
        { halfSize, 0.0f,  halfSize},
        {-halfSize, 0.0f,  halfSize},
    };
    std::vector<VertexUV2> uvs = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// 内置 cube mesh（6 面 × 4 顶点，共 24 vertices / 12 triangles）。每面单独
// 一组顶点是为了让 UV 在 face 边界不连续 —— textured material 在 face 间
// 看起来才正常（共享 8 顶点的方案 UV 必然拉伸 / 接缝错位）。
std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeCubeMesh(float halfSize)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    const float h = halfSize;
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;
    positions.reserve(24);
    uvs.reserve(24);
    indices.reserve(36);

    auto addFace = [&](VertexPosition3 a, VertexPosition3 b,
                       VertexPosition3 c, VertexPosition3 d) {
        const std::uint32_t base = static_cast<std::uint32_t>(positions.size());
        positions.push_back(a); positions.push_back(b);
        positions.push_back(c); positions.push_back(d);
        uvs.push_back({0.0f, 0.0f}); uvs.push_back({1.0f, 0.0f});
        uvs.push_back({1.0f, 1.0f}); uvs.push_back({0.0f, 1.0f});
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
        indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
    };

    // +X / -X / +Y / -Y / +Z / -Z；winding 与既有 sample 的 plane 同顺
    // （CCW 朝外），避免与 shadow caster / 主 pass 的 CullMode 假设打架。
    addFace({ h,-h, h}, { h,-h,-h}, { h, h,-h}, { h, h, h});  // +X
    addFace({-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h});  // -X
    addFace({-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h});  // +Y (top)
    addFace({-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h});  // -Y (bottom)
    addFace({-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h});  // +Z
    addFace({ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h});  // -Z

    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// 编辑器侧的"AssetRegistry / MaterialSystem 一次性建好"——main() 在
// SeedDemoWorld 首次调用之前调一次。失败会让 SeedDemoWorld 仍能工作
// （RenderableComponent 退化到无 mesh / nullptr material 状态），只是 Scene
// 面板视口看不到几何 —— 编辑器本身仍正常运转。
void InitializeEditorAssets(EditorState& state)
{
    using Orange::Engine::Asset::AssetRegistry;
    using Orange::Engine::Asset::MeshAsset;
    using Orange::Engine::Asset::ShaderAsset;
    using Orange::Engine::Asset::ShaderLoader;
    using Orange::Engine::Render::MaterialSystem;

    state.pAssets = std::make_unique<AssetRegistry>();
    if (auto reg = state.pAssets->RegisterLoader<ShaderAsset>(
            std::make_unique<ShaderLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<ShaderAsset> 失败 "
                     "(code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
    }

    if (auto h = state.pAssets->Insert<MeshAsset>("editor/cube", MakeCubeMesh(0.5f));
        h.IsOk())
    {
        state.cubeMeshHandle = h.Value();
    }
    if (auto h = state.pAssets->Insert<MeshAsset>("editor/plane", MakePlaneMesh(2.5f));
        h.IsOk())
    {
        state.planeMeshHandle = h.Value();
    }

    state.pMaterials = std::make_unique<MaterialSystem>(*state.pAssets);
    if (auto rb = state.pMaterials->RegisterBuiltins(); rb.IsErr())
    {
        // 通常意味着 shaders/orange_engine/*.spv 不在 .exe 同目录 —— in-tree
        // build 由 CMake 把 SPV 拷到 build/bin/$<CONFIG>/shaders/orange_engine/，
        // standalone install 还没有官方流程时这里会报，但不阻止编辑器启动。
        std::fprintf(stderr,
                     "[OrangeEditor] MaterialSystem::RegisterBuiltins 失败 "
                     "(code=%u) —— Scene 视口稍后可能不显示几何\n",
                     static_cast<unsigned>(rb.Error()));
    }

    state.pFloorMaterial = state.pMaterials->CreateInstance("textured");
    state.pWallMaterial  = state.pMaterials->CreateInstance("textured");
    // "+ Add Component → Renderable" 默认材质（textured）+ Light Object
    // 发光材质（emissive）。CreateInstance 失败时回退 nullptr，调用方按
    // nullptr 自然降级（Pipeline 跳过该 drawable）。
    state.pDefaultRenderableMaterial = state.pMaterials->CreateInstance("textured");
    state.pLightObjectMaterial       = state.pMaterials->CreateInstance("emissive");
}

// 种子 demo 世界 —— 拓扑：
//   Root
//   ├── Camera
//   ├── Light
//   └── Geometry
//       ├── Floor
//       └── Wall
//   Misc Sibling      （第二棵根，验证多根显示）
void SeedDemoWorld(EditorState& state)
{
    using ::Orange::Engine::Entity;
    using ::Orange::Engine::Scene::NameComponent;
    using ::Orange::Engine::Scene::TransformComponent;
    using ::Orange::Engine::Render::Camera;
    using ::Orange::Engine::Render::DirectionalLight;
    using ::Orange::Engine::Render::RenderableComponent;
    using ::Orange::Engine::Physics::BodyType;
    using ::Orange::Engine::Physics::ColliderComponent;
    using ::Orange::Engine::Physics::BoxDesc;
    using ::Orange::Engine::Physics::RigidBodyComponent;

    auto& world = *state.pWorld;
    auto make = [&](const char* name) {
        Entity e = world.CreateEntity();
        world.AddComponent<NameComponent>(e, NameComponent{name});
        world.AddComponent<TransformComponent>(e, TransformComponent{});
        return e;
    };

    Entity root     = make("Root");
    Entity camera   = make("Camera");
    Entity light    = make("Light");
    Entity geometry = make("Geometry");
    Entity floor    = make("Floor");
    Entity wall     = make("Wall");
    Entity misc     = make("Misc Sibling");

    // Camera：透视投影 + lookAt 从前上方看向原点，让 Floor / Wall 都在视
    // 野里。aspect 用 1:1（Scene 视口默认尺寸先按方形），S3 / S4 接通
    // viewport resize 后由编辑器相机系统按实际尺寸覆盖 projection。
    {
        Camera cam = Camera::Perspective(glm::radians(50.0f),
                                         /*aspect=*/1.0f,
                                         /*zNear=*/0.1f,
                                         /*zFar=*/100.0f);
        cam.view = glm::lookAt(glm::vec3(3.0f, 2.5f, 5.0f),
                               glm::vec3(0.0f, 0.5f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent<Camera>(camera, cam);
    }

    // Light：默认朝下方略偏前的方向 —— 让 Wall 在 Floor 上投出可见阴影。
    {
        DirectionalLight dl{};
        world.AddComponent<DirectionalLight>(light, dl);
    }

    // Floor：plane mesh + textured material；地面通常不投自己阴影。
    {
        // 调整 Floor transform：略下移让 cube 站在地面上（cube 中心在 y=0
        // 时 -Y 面落在 y=-0.5；地面 y=-0.5 与 cube 底齐）。
        auto* floorT = world.GetComponent<TransformComponent>(floor);
        if (floorT != nullptr)
        {
            floorT->position.y = -0.5f;
        }
        RenderableComponent rcFloor{};
        rcFloor.mesh             = state.planeMeshHandle;
        rcFloor.materialInstance = state.pFloorMaterial.get();
        rcFloor.visible          = true;
        rcFloor.castsShadow      = false;
        world.AddComponent<RenderableComponent>(floor, rcFloor);

        RigidBodyComponent rbFloor{};
        rbFloor.type            = BodyType::Static;
        rbFloor.fixedRotation   = true;
        rbFloor.gravityScale    = 0.0f;
        world.AddComponent<RigidBodyComponent>(floor, rbFloor);

        ColliderComponent ccFloor{};
        ccFloor.shape       = BoxDesc{glm::vec2{5.0f, 0.5f}};  // 半宽 / 半高
        ccFloor.density     = 0.0f;
        ccFloor.friction    = 0.5f;
        world.AddComponent<ColliderComponent>(floor, ccFloor);
    }

    // Wall：cube mesh + textured material；偏左一点站在 Floor 上方。
    {
        auto* wallT = world.GetComponent<TransformComponent>(wall);
        if (wallT != nullptr)
        {
            wallT->position = glm::vec3(-1.0f, 0.0f, 0.0f);  // cube 底面正好坐在 Floor 上
        }
        RenderableComponent rcWall{};
        rcWall.mesh             = state.cubeMeshHandle;
        rcWall.materialInstance = state.pWallMaterial.get();
        rcWall.visible          = true;
        rcWall.castsShadow      = true;
        world.AddComponent<RenderableComponent>(wall, rcWall);
    }

    EditorHierarchy::LinkAsLastChild(world, root,     camera);
    EditorHierarchy::LinkAsLastChild(world, root,     light);
    EditorHierarchy::LinkAsLastChild(world, root,     geometry);
    EditorHierarchy::LinkAsLastChild(world, geometry, floor);
    EditorHierarchy::LinkAsLastChild(world, geometry, wall);
    // root 和 misc 自身是 root level —— 不挂任何 parent，HierarchyComponent
    // 也可以不加（树视图按"无 HC 或 parent invalid 视为 root"处理）。
    (void)misc;
}
