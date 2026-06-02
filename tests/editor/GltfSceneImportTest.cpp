// glTF scene-level 导入端到端测试（GAP-2026-05-28 G1）—— 锁住 "多 node /
// 多 mesh 的 .gltf 导入后保留 transform 层级 + 每 mesh 单独 .mesh + 产出
// 可加载 .scene.json" 这条链。区别于 HeadlessMeshImportTest（asset import，
// 整文件塌平合并成单 mesh），本测试验 scene import（保留 hierarchy 不塌平）。
//
// 自包含 fixture：程序化写一个 3-node 层级 glTF（RootGroup → {ChildA, ChildB}，
// 两个独立 mesh），buffer 走 base64 data: URI（无外部 .bin），干净 checkout 也跑。
// 导入后用 Scene::Load round-trip 回一个 World，断言实体数 / 父子关系 /
// transform / renderable mesh handle。
//
// 链接方式同 headless_mesh_import_test：直接编译真实 importer 源
// （GltfSceneImporter / GltfImporter[提供 cgltf IMPLEMENTATION] / ...）+ vendor
// 单 header，只链 orange_engine。无 Vulkan / ImGui / GLFW。

#include "GltfSceneImporter.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

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

// 写一个 3-node 层级 .gltf：RootGroup（无 mesh，translation (1,2,3)）→
// {ChildA(mesh 0, translation (0.5,0,0)), ChildB(mesh 1, translation (-0.5,0,0))}。
// 两个 mesh 各 1 primitive，共享同一 buffer 的 position / index accessor。
void WriteSceneHierarchyGltf(const std::string& path)
{
    // positions=(0,0,0)(1,0,0)(1,1,0)(0,1,0) 48B + indices 0,1,2,0,2,3（12B）。
    static const char* kBufferB64 =
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA"
        "AAABAAIAAAACAAMA";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    assert(ofs.is_open() && "写层级 .gltf fixture 应成功");
    ofs <<
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"scene\": 0,\n"
        "  \"scenes\": [{\"nodes\": [0]}],\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"RootGroup\", \"translation\": [1.0, 2.0, 3.0], "
        "\"children\": [1, 2]},\n"
        "    {\"name\": \"ChildA\", \"translation\": [0.5, 0.0, 0.0], \"mesh\": 0},\n"
        "    {\"name\": \"ChildB\", \"translation\": [-0.5, 0.0, 0.0], \"mesh\": 1}\n"
        "  ],\n"
        "  \"meshes\": [\n"
        "    {\"name\": \"Crate\",  \"primitives\": [{\"attributes\": "
        "{\"POSITION\": 0}, \"indices\": 1}]},\n"
        "    {\"name\": \"Barrel\", \"primitives\": [{\"attributes\": "
        "{\"POSITION\": 0}, \"indices\": 1}]}\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    {\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, "
        "\"type\": \"VEC3\", \"min\": [0,0,0], \"max\": [1,1,0]},\n"
        "    {\"bufferView\": 1, \"componentType\": 5123, \"count\": 6, "
        "\"type\": \"SCALAR\"}\n"
        "  ],\n"
        "  \"bufferViews\": [\n"
        "    {\"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 48},\n"
        "    {\"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 12}\n"
        "  ],\n"
        "  \"buffers\": [{\"byteLength\": 60, \"uri\": "
        "\"data:application/octet-stream;base64," << kBufferB64 << "\"}]\n"
        "}\n";
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
        fs::temp_directory_path() / "orange_gltf_scene_import_test";
    std::error_code ec;
    fs::remove_all(testRoot, ec);
    fs::create_directories(testRoot, ec);
    assert(!ec && "建临时测试根目录应成功");
    fs::current_path(testRoot, ec);
    assert(!ec && "切 cwd 到临时目录应成功");

    std::fprintf(stdout, "[GltfSceneImportTest] cwd=%s\n",
                 fs::current_path().string().c_str());

    const fs::path srcDir = testRoot / "src";
    fs::create_directories(srcDir, ec);
    const std::string gltfPath = (srcDir / "scene_hier.gltf").generic_string();
    WriteSceneHierarchyGltf(gltfPath);

    auto registry = MakeImportRegistry();
    const ImportNS::ImportResult r =
        ImportNS::RunGltfSceneImportToRegistry(gltfPath, *registry);
    assert(r.status == ImportNS::ImportStatus::Success &&
           "scene 导入应 Success");
    assert(!r.destPath.empty() && "destPath（.scene.json）应非空");
    assert(r.destPath.find(".scene.json") != std::string::npos &&
           "destPath 应是 .scene.json");
    assert(fs::exists(r.destPath) && ".scene.json 应落盘");
    std::fprintf(stdout, "  [PASS] 导入产出 scene: %s (%s)\n",
                 r.destPath.c_str(), r.message.c_str());

    // ===== 每 mesh 单独 .mesh（不塌平）=====
    const fs::path modelDir = fs::path("assets/Models/scene_hier");
    const fs::path crateMesh  = modelDir / "scene_hier_Crate.mesh";
    const fs::path barrelMesh = modelDir / "scene_hier_Barrel.mesh";
    assert(fs::exists(crateMesh) && "mesh 0 应单独写 scene_hier_Crate.mesh");
    assert(fs::exists(barrelMesh) && "mesh 1 应单独写 scene_hier_Barrel.mesh（不与 mesh 0 合并）");
    assert(fs::exists(crateMesh.generic_string() + ".meta") && "mesh .meta 应落盘");
    std::fprintf(stdout, "  [PASS] 2 个独立 .mesh（每 cgltf mesh 单独写，不塌平）\n");

    // ===== Scene::Load round-trip：实体数 / 父子关系 / transform / renderable =====
    {
        World world;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = registry.get();
        auto loadRes = SceneNS::Load(r.destPath, world, opts);
        assert(loadRes.IsOk() && "产出的 .scene.json 应能被 Scene::Load 加载");

        // 3 个实体（RootGroup + ChildA + ChildB）。
        std::size_t named = 0;
        for (auto e : world.Registry().view<SceneNS::NameComponent>()) { (void)e; ++named; }
        assert(named == 3 && "应有 3 个实体（保留 node 树，含 group 根）");

        const Entity root   = FindByName(world, "RootGroup");
        const Entity childA = FindByName(world, "ChildA");
        const Entity childB = FindByName(world, "ChildB");
        assert(world.IsValid(root) && world.IsValid(childA) && world.IsValid(childB) &&
               "RootGroup / ChildA / ChildB 三实体都应存在（名字保留）");

        // RootGroup：transform (1,2,3)；group 节点无 Renderable；parent 为根。
        const auto* rootT = world.GetComponent<SceneNS::TransformComponent>(root);
        assert(rootT != nullptr && "RootGroup 应有 Transform");
        assert(std::fabs(rootT->position.x - 1.0f) < 1e-4f &&
               std::fabs(rootT->position.y - 2.0f) < 1e-4f &&
               std::fabs(rootT->position.z - 3.0f) < 1e-4f &&
               "RootGroup transform 应是 glTF 摆位 (1,2,3)");
        assert(!world.HasComponent<::Orange::Engine::Render::RenderableComponent>(root) &&
               "group 节点（无 mesh）不应有 Renderable");
        const auto* rootH = world.GetComponent<SceneNS::HierarchyComponent>(root);
        assert(rootH != nullptr && rootH->parent == Entity::Invalid() &&
               "RootGroup 应是根（parent Invalid）");
        assert(world.IsValid(rootH->firstChild) &&
               "RootGroup 应有 firstChild（孩子链已建）");

        // ChildA：parent==RootGroup；有 Renderable + mesh handle 有效；transform。
        const auto* aH = world.GetComponent<SceneNS::HierarchyComponent>(childA);
        assert(aH != nullptr && aH->parent == root &&
               "ChildA 的 parent 应是 RootGroup（层级保留）");
        const auto* aR =
            world.GetComponent<::Orange::Engine::Render::RenderableComponent>(childA);
        assert(aR != nullptr && aR->mesh.IsValid() &&
               "ChildA 应有 Renderable 指向有效 mesh handle");
        const auto* aT = world.GetComponent<SceneNS::TransformComponent>(childA);
        assert(aT != nullptr && std::fabs(aT->position.x - 0.5f) < 1e-4f &&
               "ChildA transform 应是 (0.5,0,0)");

        // ChildB：parent==RootGroup；有 Renderable。两 child mesh 应不同 handle
        // （各自独立 .mesh，没被塌平共享）。
        const auto* bH = world.GetComponent<SceneNS::HierarchyComponent>(childB);
        assert(bH != nullptr && bH->parent == root &&
               "ChildB 的 parent 应是 RootGroup");
        const auto* bR =
            world.GetComponent<::Orange::Engine::Render::RenderableComponent>(childB);
        assert(bR != nullptr && bR->mesh.IsValid() &&
               "ChildB 应有 Renderable 指向有效 mesh handle");
        assert(aR->mesh.Value() != bR->mesh.Value() &&
               "ChildA / ChildB 应指向各自独立的 mesh（不塌平共享）");

        std::fprintf(stdout,
                     "  [PASS] Scene::Load round-trip：3 实体 + RootGroup→{ChildA,ChildB} "
                     "层级 + transform + 各自独立 mesh handle\n");
    }

    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(testRoot, ec);

    std::fprintf(stdout, "[GltfSceneImportTest] all tests passed.\n");
    return 0;
}
