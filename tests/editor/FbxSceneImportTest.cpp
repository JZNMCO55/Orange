// FBX scene-level 导入端到端测试（headless）—— 锁住 FbxSceneImporter 的整条链：
// .fbx → OpenFBX 解析（保留 node 层级）→ 每 mesh node 单独 .mesh（不塌平）→
// node local transform 经 R·M·R⁻¹ 共轭轴转换（Z-up→Y-up）→ Hierarchy 父子链 →
// per-mesh material → 产出可 Scene::Load 的 .scene.json。
//
// 区别于 FbxImportTest（asset import，整文件塌平合并成单 mesh），本测试验 scene
// import（保留 hierarchy 不塌平 + node local transform 共轭轴转换）。
//
// 为什么能 headless：走 RunFbxSceneImportToRegistry 的 registry-only seam，只依赖
// Orange::Engine::Asset::AssetRegistry&，不出现 EditorHost / Vulkan / ImGui /
// GLFW。直接编译真实 importer 源 + vendor（OpenFBX / mikktspace），只链 orange_engine。
//
// fixture：tests/fixtures/cube_hierarchy.fbx（由 gen_cube_hierarchy_fbx.py 经
// Blender headless 生成，自有几何可自由分发）。已知层级（Blender Z-up）：
//   Parent（cube，世界 location (2,0,0)，无旋转）
//     └─ Child（cube，**相对父的 local**：location (0,3,0) Blender +Y +
//        rotation 90° 绕 Blender 局部 X 轴）
// 经 Z-up→Y-up 换轴 (x,y,z)→(x,z,-y) 的共轭后：
//   * Child local position 应 ≈ (0,0,-3)（Blender +Y → 引擎 -Z，平移被 R 旋转）
//   * Child local rotation 应 ≈ 90° 绕引擎 X 轴（X 是换轴不动轴，共轭后同轴同角）
// 这两条是验"共轭做对没"的 load-bearing 断言（误用 R·M 不共轭会让旋转 / 平移歪）。
// fixture 路径经 ORANGE_ENGINE_FBX_SCENE_FIXTURE 注入；CMake if(EXISTS) 门控。

#include "FbxSceneImporter.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/TransformSystem.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace ImportNS = ::Orange::Editor::Import;
namespace AssetNS  = ::Orange::Engine::Asset;
namespace SceneNS  = ::Orange::Engine::Scene;
using ::Orange::Engine::Entity;
using ::Orange::Engine::World;
namespace fs = std::filesystem;

namespace
{

std::unique_ptr<AssetNS::AssetRegistry> MakeImportRegistry()
{
    auto registry = std::make_unique<AssetNS::AssetRegistry>();
    auto rm = registry->RegisterLoader<AssetNS::MeshAsset>(
        std::make_unique<AssetNS::MeshLoader>());
    assert(rm.IsOk() && "RegisterLoader<MeshAsset> 应成功");
    return registry;
}

// 在 Load 回来的 World 里按名字找实体（名字唯一）。找不到返回 Invalid。
Entity FindByName(World& world, const std::string& name)
{
    Entity found = Entity::Invalid();
    auto view = world.Registry().view<SceneNS::NameComponent>();
    for (auto e : view)
    {
        const Entity ent = World::FromEntt(e);
        const auto* nc = world.GetComponent<SceneNS::NameComponent>(ent);
        if (nc != nullptr && nc->name == name)
        {
            found = ent;
            break;
        }
    }
    return found;
}

}  // namespace

int main()
{
    const fs::path testRoot =
        fs::temp_directory_path() / "orange_fbx_scene_import_test";
    std::error_code ec;
    fs::remove_all(testRoot, ec);
    fs::create_directories(testRoot, ec);
    assert(!ec && "建临时测试根目录应成功");
    fs::current_path(testRoot, ec);
    assert(!ec && "切 cwd 到临时目录应成功");

    std::fprintf(stdout, "[FbxSceneImportTest] cwd=%s\n",
                 fs::current_path().string().c_str());

    // ===== 1. 缺失文件 → SourceReadFailed（不崩）=====
    {
        auto registry = MakeImportRegistry();
        const ImportNS::ImportResult r = ImportNS::RunFbxSceneImportToRegistry(
            (testRoot / "does_not_exist.fbx").generic_string(), *registry);
        assert(r.status == ImportNS::ImportStatus::SourceReadFailed &&
               "不存在的 .fbx 应返回 SourceReadFailed（不崩）");
        std::fprintf(stdout, "  [PASS] 缺失 .fbx → SourceReadFailed\n");
    }

#ifdef ORANGE_ENGINE_FBX_SCENE_FIXTURE
    const std::string fixturePath = ORANGE_ENGINE_FBX_SCENE_FIXTURE;
    if (!fs::exists(fixturePath))
    {
        std::fprintf(stdout, "  [SKIP] FBX scene fixture 不存在: %s\n",
                     fixturePath.c_str());
        std::fprintf(stdout, "[FbxSceneImportTest] (fixture-gated tests skipped)\n");
        fs::current_path(fs::temp_directory_path(), ec);
        fs::remove_all(testRoot, ec);
        return 0;
    }

    // ===== 2. 导入层级 FBX → .scene.json + 2 .mesh（每 mesh node 单独不塌平）=====
    auto registry = MakeImportRegistry();
    const ImportNS::ImportResult r =
        ImportNS::RunFbxSceneImportToRegistry(fixturePath, *registry);
    assert(r.status == ImportNS::ImportStatus::Success &&
           "FBX scene 导入应 Success");
    assert(!r.destPath.empty() && r.destPath.find(".scene.json") != std::string::npos &&
           "destPath 应是 .scene.json");
    assert(fs::exists(r.destPath) && ".scene.json 应落盘");
    assert(fs::exists(r.destPath + ".meta") && "scene .meta 应落盘");
    std::fprintf(stdout, "  [PASS] 导入产出 scene: %s (%s)\n",
                 r.destPath.c_str(), r.message.c_str());

    // 每 mesh node 单独 .mesh（Parent + Child 两个 cube → 2 个 .mesh，不塌平）。
    {
        std::size_t meshFiles = 0;
        const fs::path modelDir = fs::path(r.destPath).parent_path().parent_path() /
                                  fs::path("Models") /
                                  fs::path(fixturePath).stem();
        // 更稳妥：直接扫 assets/Models/<stem>/。
        const fs::path md = fs::path("assets/Models") / fs::path(fixturePath).stem();
        for (const auto& de : fs::directory_iterator(md))
        {
            if (de.path().extension() == ".mesh") { ++meshFiles; }
        }
        assert(meshFiles == 2 &&
               "Parent + Child 两 mesh node → 2 个独立 .mesh（不塌平）");
        std::fprintf(stdout, "  [PASS] 2 个独立 .mesh（每 mesh node 单独写，不塌平）\n");
    }

    // ===== 3. Scene::Load round-trip：层级 + node local transform 共轭轴转换 =====
    {
        World world;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = registry.get();
        auto loadRes = SceneNS::Load(r.destPath, world, opts);
        assert(loadRes.IsOk() && "产出的 .scene.json 应能被 Scene::Load 加载");

        // 实体数：Parent + Child = 2。
        std::size_t named = 0;
        for (auto e : world.Registry().view<SceneNS::NameComponent>()) { (void)e; ++named; }
        assert(named == 2 && "应有 2 个实体（Parent + Child，保留 node 树）");

        const Entity parent = FindByName(world, "Parent");
        const Entity child  = FindByName(world, "Child");
        assert(world.IsValid(parent) && world.IsValid(child) &&
               "Parent / Child 两实体都应存在（名字保留）");

        // ---- Hierarchy 父子链：Child.parent == Parent；Parent 是根 ----
        const auto* pH = world.GetComponent<SceneNS::HierarchyComponent>(parent);
        const auto* cH = world.GetComponent<SceneNS::HierarchyComponent>(child);
        assert(pH != nullptr && pH->parent == Entity::Invalid() &&
               "Parent 应是根（parent Invalid）");
        assert(world.IsValid(pH->firstChild) &&
               "Parent 应有 firstChild（孩子链已建）");
        assert(cH != nullptr && cH->parent == parent &&
               "Child 的 parent 应是 Parent（层级保留）");
        std::fprintf(stdout,
                     "  [PASS] 层级：Parent(根) → Child（父子 Hierarchy 链正确）\n");

        // ---- 两 mesh node 都有 Renderable + 各自独立 mesh handle ----
        namespace RenderNS = ::Orange::Engine::Render;
        const auto* pR = world.GetComponent<RenderNS::RenderableComponent>(parent);
        const auto* cR = world.GetComponent<RenderNS::RenderableComponent>(child);
        assert(pR != nullptr && pR->mesh.IsValid() && "Parent 应有有效 Renderable");
        assert(cR != nullptr && cR->mesh.IsValid() && "Child 应有有效 Renderable");
        assert(pR->mesh.Value() != cR->mesh.Value() &&
               "Parent / Child 应指向各自独立 mesh（不塌平共享）");

        // ---- Parent local transform：世界 (2,0,0)（根，local==world）经换轴 ----
        // Blender Z-up (2,0,0) → 引擎 (x,z,-y) = (2,0,0)（X 不变，y=0/z=0）。
        const auto* pT = world.GetComponent<SceneNS::TransformComponent>(parent);
        assert(pT != nullptr && "Parent 应有 Transform");
        std::fprintf(stdout, "  [info] Parent local pos = (%.4f, %.4f, %.4f)\n",
                     pT->position.x, pT->position.y, pT->position.z);
        assert(std::fabs(pT->position.x - 2.0f) < 1e-3f &&
               std::fabs(pT->position.y - 0.0f) < 1e-3f &&
               std::fabs(pT->position.z - 0.0f) < 1e-3f &&
               "Parent local 位置应是 (2,0,0)（Blender (2,0,0) 换轴后 X 不变）");

        // ---- Child local transform：共轭轴转换 load-bearing 断言 ----
        // Blender local：平移 (0,3,0) + 旋转 90° 绕 Blender X。
        //   平移：R·(0,3,0) = (0,0,-3)（Blender +Y → 引擎 -Z）。
        //   旋转：绕 Blender X 90° 共轭后 = 绕引擎 X 90°（X 是换轴不动轴）。
        const auto* cT = world.GetComponent<SceneNS::TransformComponent>(child);
        assert(cT != nullptr && "Child 应有 Transform");
        std::fprintf(stdout,
                     "  [info] Child local pos = (%.4f, %.4f, %.4f) "
                     "rot quat = (%.4f, %.4f, %.4f, %.4f) scale = (%.4f, %.4f, %.4f)\n",
                     cT->position.x, cT->position.y, cT->position.z,
                     cT->rotation.x, cT->rotation.y, cT->rotation.z, cT->rotation.w,
                     cT->scale.x, cT->scale.y, cT->scale.z);

        assert(std::fabs(cT->position.x - 0.0f) < 2e-3f &&
               std::fabs(cT->position.y - 0.0f) < 2e-3f &&
               std::fabs(cT->position.z - (-3.0f)) < 2e-3f &&
               "Child local 位置应 ≈ (0,0,-3)（Blender +Y 平移经 R 共轭 → 引擎 -Z）");

        // 旋转：用"把引擎 +Y 前向 (0,1,0) 旋过去"验，比直接比 quat 分量稳健（quat
        // 有双覆盖 ±q 歧义）。绕 X 90°：(0,1,0) → (0,0,1)。
        const glm::vec3 rotatedY = cT->rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        std::fprintf(stdout,
                     "  [info] Child rotation applied to +Y = (%.4f, %.4f, %.4f)\n",
                     rotatedY.x, rotatedY.y, rotatedY.z);
        assert(std::fabs(rotatedY.x - 0.0f) < 5e-3f &&
               std::fabs(rotatedY.y - 0.0f) < 5e-3f &&
               std::fabs(std::fabs(rotatedY.z) - 1.0f) < 5e-3f &&
               "Child local 旋转应把引擎 +Y 转到 ±Z（绕引擎 X 轴 90°，共轭旋转轴正确）");

        // scale 应保持单位（child.scale=(1,1,1)，换轴不引镜像）。
        assert(std::fabs(cT->scale.x - 1.0f) < 1e-2f &&
               std::fabs(cT->scale.y - 1.0f) < 1e-2f &&
               std::fabs(cT->scale.z - 1.0f) < 1e-2f &&
               "Child local scale 应 ≈ (1,1,1)（无镜像 / 不被换轴破坏）");
        std::fprintf(stdout,
                     "  [PASS] 共轭轴转换：Child local pos (0,0,-3) + rot 绕引擎 X 90° "
                     "（R·M·R⁻¹ 共轭正确，非 R·M）\n");

        // ===== 4. end-to-end：importer 写 local + 引擎累积父变换 = 正确 world =====
        // Child world = Parent(2,0,0) ⊕ Child_local。Child 在 Blender 世界里位于
        // Parent(2,0,0) + local(0,3,0) = (2,3,0)；换轴 (x,z,-y) → 引擎世界 (2,0,-3)。
        // 引擎用 local TRS 沿 hierarchy 累积应得到同一世界位置。
        SceneNS::PropagateWorldTransforms(world);
        const auto* cWT =
            world.GetComponent<SceneNS::WorldTransformComponent>(child);
        assert(cWT != nullptr && "Child 应有 world transform cache");
        std::fprintf(stdout,
                     "  [info] Child world pos = (%.4f, %.4f, %.4f)\n",
                     cWT->world[3].x, cWT->world[3].y, cWT->world[3].z);
        assert(std::fabs(cWT->world[3].x - 2.0f) < 3e-3f &&
               std::fabs(cWT->world[3].y - 0.0f) < 3e-3f &&
               std::fabs(cWT->world[3].z - (-3.0f)) < 3e-3f &&
               "end-to-end：Child world 位置应 ≈ (2,0,-3)（Blender 世界 (2,3,0) 换轴后）");
        std::fprintf(stdout,
                     "  [PASS] end-to-end：importer 写 local + 引擎累积父变换 → "
                     "Child world (2,0,-3)（与 Blender 摆位一致）\n");
    }

    // ===== 5. hash-skip 增量短路：改 scene.json 后重导同源应跳过 =====
    {
        const std::string scenePath = r.destPath;
        {
            std::ofstream ofs(scenePath, std::ios::binary | std::ios::trunc);
            ofs << "MANUAL_EDIT_MARKER";
        }
        auto reg = MakeImportRegistry();
        const ImportNS::ImportResult r2 =
            ImportNS::RunFbxSceneImportToRegistry(fixturePath, *reg);
        assert(r2.status == ImportNS::ImportStatus::Success &&
               "重导未改源应 Success（hash 短路）");
        assert(r2.message.find("unchanged") != std::string::npos &&
               "重导未改源应走 hash 短路（message 含 unchanged）");
        std::ifstream ifs(scenePath, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(ifs)),
                                  std::istreambuf_iterator<char>());
        assert(content == "MANUAL_EDIT_MARKER" &&
               "源未改 → hash 短路跳过，scene.json 手工编辑应保留（不被覆盖）");
        std::fprintf(stdout,
                     "  [PASS] hash-skip：改 scene.json 后重导同源跳过，手工编辑保留\n");
    }

    // ===== 6. importScale：FBX 单位 → 米的显式缩放（真 cm 文件修正路径）=====
    // 把同一 fixture 复制成不同 basename（避 hash-skip + 独立 scene.json），用
    // importScale=0.5 导入：node 平移 + 顶点都应整体 ×0.5（Parent (2,0,0)→(1,0,0)，
    // Child local (0,0,-3)→(0,0,-1.5)，world (2,0,-3)→(1,0,-1.5)）。锁住 importScale
    // 经 MakeAxisConverter→conv.unitScale 流进顶点 + ConjugateNodeLocal 平移两条路径。
    {
        const fs::path scaledFbx = testRoot / "src_scaled" / "scaled_hierarchy.fbx";
        fs::create_directories(scaledFbx.parent_path(), ec);
        fs::copy_file(fixturePath, scaledFbx,
                      fs::copy_options::overwrite_existing, ec);
        assert(!ec && "复制 fixture 到新 basename 应成功");

        auto reg = MakeImportRegistry();
        const ImportNS::ImportResult rs = ImportNS::RunFbxSceneImportToRegistry(
            scaledFbx.generic_string(), *reg, 0.5f);
        assert(rs.status == ImportNS::ImportStatus::Success &&
               "importScale=0.5 导入应 Success");

        World w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg.get();
        auto lr = SceneNS::Load(rs.destPath, w, opts);
        assert(lr.IsOk() && "scaled scene 应能 Load");

        const Entity parent = FindByName(w, "Parent");
        const Entity child  = FindByName(w, "Child");
        assert(w.IsValid(parent) && w.IsValid(child) &&
               "scaled 场景的 Parent / Child 应存在");

        // Parent local (2,0,0) × 0.5 = (1,0,0)。
        const auto* pT = w.GetComponent<SceneNS::TransformComponent>(parent);
        assert(pT != nullptr &&
               std::fabs(pT->position.x - 1.0f) < 2e-3f &&
               std::fabs(pT->position.y - 0.0f) < 2e-3f &&
               std::fabs(pT->position.z - 0.0f) < 2e-3f &&
               "importScale=0.5：Parent local (2,0,0) 应缩成 (1,0,0)");

        // Child local (0,0,-3) × 0.5 = (0,0,-1.5)（共轭后的引擎 -Z 平移也被缩）。
        const auto* cT = w.GetComponent<SceneNS::TransformComponent>(child);
        assert(cT != nullptr &&
               std::fabs(cT->position.x - 0.0f) < 2e-3f &&
               std::fabs(cT->position.y - 0.0f) < 2e-3f &&
               std::fabs(cT->position.z - (-1.5f)) < 2e-3f &&
               "importScale=0.5：Child local (0,0,-3) 应缩成 (0,0,-1.5)");

        // importScale 只缩平移 + 顶点，不改 node scale（scale 无量纲）。
        assert(std::fabs(cT->scale.x - 1.0f) < 1e-2f &&
               std::fabs(cT->scale.y - 1.0f) < 1e-2f &&
               std::fabs(cT->scale.z - 1.0f) < 1e-2f &&
               "importScale 不应改 node scale（只缩平移 + 顶点）");

        // end-to-end world：(2,0,-3) × 0.5 = (1,0,-1.5)。
        SceneNS::PropagateWorldTransforms(w);
        const auto* cWT = w.GetComponent<SceneNS::WorldTransformComponent>(child);
        assert(cWT != nullptr &&
               std::fabs(cWT->world[3].x - 1.0f) < 3e-3f &&
               std::fabs(cWT->world[3].y - 0.0f) < 3e-3f &&
               std::fabs(cWT->world[3].z - (-1.5f)) < 3e-3f &&
               "importScale=0.5：Child world (2,0,-3) 应缩成 (1,0,-1.5)");
        std::fprintf(stdout,
                     "  [PASS] importScale=0.5：node 平移整体 ×0.5（Parent (1,0,0) / "
                     "Child local (0,0,-1.5) / world (1,0,-1.5)）\n");
    }
#else
    std::fprintf(stdout,
                 "  [SKIP] FBX scene fixture 未编入（无 ORANGE_ENGINE_FBX_SCENE_FIXTURE）\n");
#endif

    // ===== 7. FBX camera node → Render::Camera（投影 + 朝向桥接）=====
    // fixture：cube_with_camera.fbx —— RefCube（原点 mesh，验相机与 mesh 共存）+
    // Cam（Blender 世界 (0,0,5)，无旋转 = 看本地 -Z 俯看原点；focal 35mm /
    // sensor 36mm / clip 0.1~100）。经 Z-up→Y-up 换轴 (x,y,z)→(x,z,-y)：相机位置
    // (0,0,5)→(0,5,0)，朝向应仍俯看原点 = 引擎 forward(rotation·-Z) ≈ (0,-1,0)。
#ifdef ORANGE_ENGINE_FBX_CAMERA_FIXTURE
    {
        namespace RenderNS = ::Orange::Engine::Render;
        using RenderNS::Camera;

        const std::string camFixture = ORANGE_ENGINE_FBX_CAMERA_FIXTURE;
        if (!fs::exists(camFixture))
        {
            std::fprintf(stdout, "  [SKIP] FBX camera fixture 不存在: %s\n",
                         camFixture.c_str());
        }
        else
        {
            auto reg = MakeImportRegistry();
            const ImportNS::ImportResult rc =
                ImportNS::RunFbxSceneImportToRegistry(camFixture, *reg);
            assert(rc.status == ImportNS::ImportStatus::Success &&
                   "FBX camera 场景导入应 Success");
            assert(rc.message.find("cameras=1") != std::string::npos &&
                   "result message 应含 cameras=1（1 个相机 node 被计数）");
            std::fprintf(stdout, "  [PASS] camera 导入产出 scene (%s)\n",
                         rc.message.c_str());

            World w;
            SceneNS::LoadOptions opts;
            opts.assetRegistry = reg.get();
            auto lr = SceneNS::Load(rc.destPath, w, opts);
            assert(lr.IsOk() && "相机场景应能 Load（Camera component round-trip）");

            // 恰好 1 个 Camera component；RefCube mesh 与相机共存。
            std::size_t camCount = 0;
            for (auto e : w.Registry().view<Camera>()) { (void)e; ++camCount; }
            assert(camCount == 1 && "应恰好 1 个 Camera component");

            const Entity cam     = FindByName(w, "Cam");
            const Entity refCube = FindByName(w, "RefCube");
            assert(w.IsValid(cam) && "Cam 实体应存在（相机 node 保留名字）");
            assert(w.IsValid(refCube) && "RefCube 实体应存在（mesh 与相机共存）");
            const auto* cubeR =
                w.GetComponent<RenderNS::RenderableComponent>(refCube);
            assert(cubeR != nullptr && cubeR->mesh.IsValid() &&
                   "RefCube 应有有效 Renderable（相机不吞 mesh）");

            const auto* cc = w.GetComponent<Camera>(cam);
            assert(cc != nullptr && "Cam 应挂 Camera component");

            // ---- 位置：Blender 世界 (0,0,5) 经换轴 (x,z,-y) → 引擎 (0,5,0) ----
            const auto* cT = w.GetComponent<SceneNS::TransformComponent>(cam);
            assert(cT != nullptr && "Cam 应有 Transform");
            std::fprintf(stdout,
                         "  [info] Cam local pos = (%.4f, %.4f, %.4f)\n",
                         cT->position.x, cT->position.y, cT->position.z);
            assert(std::fabs(cT->position.x - 0.0f) < 1e-2f &&
                   std::fabs(cT->position.y - 5.0f) < 1e-2f &&
                   std::fabs(cT->position.z - 0.0f) < 1e-2f &&
                   "Cam 位置应 ≈ (0,5,0)（Blender (0,0,5) 换轴 (x,z,-y)）");

            // ---- 朝向桥接：引擎 forward = rotation·(0,0,-1) 应俯看原点 ≈ (0,-1,0) ----
            const glm::vec3 fwd = cT->rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            std::fprintf(stdout,
                         "  [info] Cam forward (rot·-Z) = (%.4f, %.4f, %.4f)\n",
                         fwd.x, fwd.y, fwd.z);
            assert(std::fabs(fwd.x - 0.0f) < 2e-2f &&
                   std::fabs(fwd.y - (-1.0f)) < 2e-2f &&
                   std::fabs(fwd.z - 0.0f) < 2e-2f &&
                   "Cam forward 应 ≈ (0,-1,0)（FBX 相机看 +X，桥接到引擎 -Z 后俯看原点）");

            // ---- 投影：perspective + near/far + 水平 FOV（focal35/sensor36）----
            const glm::mat4& p = cc->projection;
            std::fprintf(stdout,
                         "  [info] proj p00=%.5f p11=%.5f p22=%.5f p23=%.5f p32=%.5f p33=%.5f\n",
                         p[0][0], p[1][1], p[2][2], p[2][3], p[3][2], p[3][3]);
            // perspective 判据：p[2][3]==-1（w=-view-z）、p[3][3]==0。
            assert(std::fabs(p[2][3] - (-1.0f)) < 1e-4f &&
                   std::fabs(p[3][3] - 0.0f) < 1e-4f &&
                   "应是透视投影（p[2][3]==-1, p[3][3]==0）");
            // 从投影反推 near/far（Camera::Perspective 矩阵布局）：
            //   p[2][2]=zFar/(zNear-zFar)，p[3][2]=zNear·zFar/(zNear-zFar)
            //   → zNear = p[3][2]/p[2][2]；zFar = p[2][2]·zNear/(1+p[2][2])。
            const float recNear = p[3][2] / p[2][2];
            const float recFar  = p[2][2] * recNear / (1.0f + p[2][2]);
            std::fprintf(stdout, "  [info] recovered near=%.4f far=%.4f\n",
                         recNear, recFar);
            assert(std::fabs(recNear - 0.1f) < 5e-3f &&
                   "投影应保住 near ≈ 0.1（clip_start）");
            assert(std::fabs(recFar - 100.0f) < 1.0f &&
                   "投影应保住 far ≈ 100（clip_end）");
            // 水平 FOV（aspect 无关）：p[0][0] = 1/tan(hfov/2)，
            //   hfov = 2·atan(filmW_mm/(2·focal)) = 2·atan(36/70) ≈ 0.9485 rad
            //   → p[0][0] ≈ 1/tan(0.4743) ≈ 1.944。锁 focal/sensor 不回归。
            std::fprintf(stdout, "  [info] p00=%.5f (期望 ≈ 1.944, hfov focal35/sensor36)\n",
                         p[0][0]);
            assert(std::fabs(p[0][0] - 1.944f) < 0.08f &&
                   "水平 FOV 应来自 focal 35mm / sensor 36mm（p[0][0] ≈ 1.944）");

            std::fprintf(stdout,
                         "  [PASS] FBX camera：→ Render::Camera（位置换轴 + 朝向桥接俯看原点 + "
                         "透视投影 near/far/hfov 正确）\n");
        }
    }
#else
    std::fprintf(stdout,
                 "  [SKIP] FBX camera fixture 未编入（无 ORANGE_ENGINE_FBX_CAMERA_FIXTURE）\n");
#endif

    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(testRoot, ec);

    std::fprintf(stdout, "[FbxSceneImportTest] all tests passed.\n");
    return 0;
}
