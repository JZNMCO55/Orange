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
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/TransformSystem.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// 写一个 5-node 层级 .gltf：RootGroup（无 mesh，translation (1,2,3)）→
// {ChildA(mesh 0), ChildB(mesh 1), SunLight(directional), Lamp(point)}。
// 两个 mesh 各 1 primitive，共享同一 buffer。灯光走 KHR_lights_punctual。
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
        "  \"extensionsUsed\": [\"KHR_lights_punctual\"],\n"
        "  \"extensions\": {\"KHR_lights_punctual\": {\"lights\": [\n"
        "    {\"name\": \"Sun\",  \"type\": \"directional\", "
        "\"color\": [1.0, 0.9, 0.8], \"intensity\": 2.5},\n"
        "    {\"name\": \"Bulb\", \"type\": \"point\", "
        "\"color\": [0.2, 0.4, 1.0], \"intensity\": 5.0, \"range\": 8.0}\n"
        "  ]}},\n"
        "  \"scene\": 0,\n"
        "  \"scenes\": [{\"nodes\": [0]}],\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"RootGroup\", \"translation\": [1.0, 2.0, 3.0], "
        "\"children\": [1, 2, 3, 4]},\n"
        "    {\"name\": \"ChildA\", \"translation\": [0.5, 0.0, 0.0], \"mesh\": 0},\n"
        "    {\"name\": \"ChildB\", \"translation\": [-0.5, 0.0, 0.0], \"mesh\": 1},\n"
        "    {\"name\": \"SunLight\", "
        "\"extensions\": {\"KHR_lights_punctual\": {\"light\": 0}}},\n"
        "    {\"name\": \"Lamp\", \"translation\": [2.0, 1.0, 0.0], "
        "\"extensions\": {\"KHR_lights_punctual\": {\"light\": 1}}}\n"
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

// 第二个 fixture：覆盖真实 Blender 导出的风险路径 ——
//   * node 用 "matrix"（列主序 4x4）而非 TRS → 验 glm::decompose 路径
//   * 3 层深嵌套（L1→L2→L3）→ 验 world 变换穿 2 层祖先累积
//   * spot light → 验 SpotLight 锥角映射 + 方向编码
void WriteMatrixAndDeepNestGltf(const std::string& path)
{
    static const char* kBufferB64 =
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA"
        "AAABAAIAAAACAAMA";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    assert(ofs.is_open() && "写 matrix/深嵌套 .gltf fixture 应成功");
    ofs <<
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"extensionsUsed\": [\"KHR_lights_punctual\"],\n"
        "  \"extensions\": {\"KHR_lights_punctual\": {\"lights\": [\n"
        "    {\"name\": \"Torch\", \"type\": \"spot\", \"color\": [1.0, 0.5, 0.0], "
        "\"intensity\": 3.0, \"range\": 12.0, "
        "\"spot\": {\"innerConeAngle\": 0.2, \"outerConeAngle\": 0.5}}\n"
        "  ]}},\n"
        "  \"scene\": 0,\n"
        "  \"scenes\": [{\"nodes\": [0, 3, 4]}],\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"L1\", \"translation\": [10.0, 0.0, 0.0], \"children\": [1]},\n"
        "    {\"name\": \"L2\", \"translation\": [0.0, 5.0, 0.0], \"children\": [2]},\n"
        "    {\"name\": \"L3\", \"translation\": [0.0, 0.0, 2.0], \"mesh\": 0},\n"
        // 列主序 translate(3,4,5)*scale(2,2,2)：col0(2,0,0,0) col1(0,2,0,0)
        // col2(0,0,2,0) col3(3,4,5,1)。
        "    {\"name\": \"MatrixNode\", \"matrix\": "
        "[2,0,0,0, 0,2,0,0, 0,0,2,0, 3,4,5,1], \"mesh\": 0},\n"
        "    {\"name\": \"SpotNode\", "
        "\"extensions\": {\"KHR_lights_punctual\": {\"light\": 0}}}\n"
        "  ],\n"
        "  \"meshes\": [\n"
        "    {\"name\": \"M\", \"primitives\": [{\"attributes\": "
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

// 第三个 fixture：mesh instancing —— 2 个 node 引用**同一** cgltf mesh。
// 验证 importer 按指针去重：只写一个 .mesh，两个 entity 的 Renderable 指向同一
// mesh 路径（真实 DCC 场景大量用实例化，如 10 棵同款树共享一个 mesh）。
void WriteInstancedMeshGltf(const std::string& path)
{
    static const char* kBufferB64 =
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA"
        "AAABAAIAAAACAAMA";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    assert(ofs.is_open() && "写实例化 .gltf fixture 应成功");
    ofs <<
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"scene\": 0,\n"
        "  \"scenes\": [{\"nodes\": [0, 1]}],\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"InstA\", \"translation\": [0.0, 0.0, 0.0], \"mesh\": 0},\n"
        "    {\"name\": \"InstB\", \"translation\": [3.0, 0.0, 0.0], \"mesh\": 0}\n"
        "  ],\n"
        "  \"meshes\": [\n"
        "    {\"name\": \"Tree\", \"primitives\": [{\"attributes\": "
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

// 第五个 fixture：**无 scenes 数组**的 glTF（spec 允许省略 scene/scenes）。
// 验证 importer 的 fallback 分支：无 scene 定义时退回"所有 parent-less node 当根"。
void WriteNoScenesGltf(const std::string& path)
{
    static const char* kBufferB64 =
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA"
        "AAABAAIAAAACAAMA";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    assert(ofs.is_open() && "写无 scenes .gltf fixture 应成功");
    // 故意不写 "scene" / "scenes" key。
    ofs <<
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"Lone\", \"translation\": [4.0, 0.0, 0.0], \"mesh\": 0}\n"
        "  ],\n"
        "  \"meshes\": [\n"
        "    {\"name\": \"Solo\", \"primitives\": [{\"attributes\": "
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
    assert(r.message.find("lights=2") != std::string::npos &&
           "result message 应含 lights=2（Sun + Bulb 两灯被计数）");
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

        // 5 个实体（RootGroup + ChildA + ChildB + SunLight + Lamp）。
        std::size_t named = 0;
        for (auto e : world.Registry().view<SceneNS::NameComponent>()) { (void)e; ++named; }
        assert(named == 5 && "应有 5 个实体（保留 node 树，含 group 根 + 2 灯光 node）");

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
        // Transform 现在是 **local**（A1.1 step 2 / ADR-016 后引擎累积 hierarchy，
        // importer 写 local，world 由引擎 PropagateWorldTransforms 累积）。
        // ChildA local = (0.5,0,0)。
        const auto* aT = world.GetComponent<SceneNS::TransformComponent>(childA);
        assert(aT != nullptr &&
               std::fabs(aT->position.x - 0.5f) < 1e-4f &&
               std::fabs(aT->position.y - 0.0f) < 1e-4f &&
               std::fabs(aT->position.z - 0.0f) < 1e-4f &&
               "ChildA local transform 应是 (0.5,0,0)（importer 写 local，非 world-bake）");

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
                     "  [PASS] Scene::Load round-trip：5 实体 + RootGroup→{ChildA,ChildB,...} "
                     "层级 + transform + 各自独立 mesh handle\n");

        // ===== KHR_lights_punctual → 引擎光源 component（G3）=====
        namespace RenderNS = ::Orange::Engine::Render;

        // SunLight：DirectionalLight，color (1,0.9,0.8) intensity 2.5；方向沿
        // glTF -Z 转引擎 -Y —— node 无 rotation → world 光向 (0,0,-1)，
        // ComputeDirectionalLightWorldDir(编码后 rotation) 应 ≈ (0,0,-1)。
        const Entity sun = FindByName(world, "SunLight");
        assert(world.IsValid(sun) && "SunLight 实体应存在");
        const auto* sunH = world.GetComponent<SceneNS::HierarchyComponent>(sun);
        assert(sunH != nullptr && sunH->parent == root &&
               "SunLight 应挂在 RootGroup 下");
        const auto* dl = world.GetComponent<RenderNS::DirectionalLight>(sun);
        assert(dl != nullptr && "SunLight 应有 DirectionalLight component");
        assert(std::fabs(dl->color.r - 1.0f) < 1e-4f &&
               std::fabs(dl->color.g - 0.9f) < 1e-4f &&
               std::fabs(dl->color.b - 0.8f) < 1e-4f &&
               "DirectionalLight color 应 = glTF (1,0.9,0.8)");
        assert(std::fabs(dl->intensity - 2.5f / 683.0f) < 1e-5f &&
               "DirectionalLight intensity 应 = glTF 2.5 ÷683 luminous efficacy（映射引擎尺度）");
        const auto* sunT = world.GetComponent<SceneNS::TransformComponent>(sun);
        assert(sunT != nullptr && "SunLight 应有 Transform");
        const glm::vec3 sunDir = RenderNS::ComputeDirectionalLightWorldDir(sunT->rotation);
        assert(std::fabs(sunDir.x - 0.0f) < 1e-3f &&
               std::fabs(sunDir.y - 0.0f) < 1e-3f &&
               std::fabs(sunDir.z - (-1.0f)) < 1e-3f &&
               "光向应沿 glTF -Z 转引擎约定后为 world (0,0,-1)（方向编码正确）");

        // Lamp：PointLight，color (0.2,0.4,1.0) intensity 5 range 8；位置 local
        // = (2,1,0)（importer 写 local；point 光的世界位置由引擎累积——点光消费者
        // 切到 world cache 是后续 increment，glTF 灯光通常 root 子 local==world）。
        const Entity lamp = FindByName(world, "Lamp");
        assert(world.IsValid(lamp) && "Lamp 实体应存在");
        const auto* pl = world.GetComponent<RenderNS::PointLight>(lamp);
        assert(pl != nullptr && "Lamp 应有 PointLight component");
        assert(std::fabs(pl->color.b - 1.0f) < 1e-4f &&
               std::fabs(pl->intensity - 5.0f / 683.0f) < 1e-4f &&
               std::fabs(pl->range - 8.0f) < 1e-4f &&
               "PointLight color/intensity(÷683)/range 应 = glTF (蓝/5÷683/8)");
        const auto* lampT = world.GetComponent<SceneNS::TransformComponent>(lamp);
        assert(lampT != nullptr &&
               std::fabs(lampT->position.x - 2.0f) < 1e-4f &&
               std::fabs(lampT->position.y - 1.0f) < 1e-4f &&
               std::fabs(lampT->position.z - 0.0f) < 1e-4f &&
               "Lamp local 位置 = (2,1,0)（importer 写 local）");

        std::fprintf(stdout,
                     "  [PASS] KHR_lights_punctual：SunLight=DirectionalLight(方向编码"
                     "正确) + Lamp=PointLight(color/intensity/range + local 位置)\n");

        // ===== end-to-end：importer local + 引擎 PropagateWorldTransforms 累积 = 正确 world =====
        // ChildA world = RootGroup(1,2,3) × local(0.5,0,0) = (1.5,2,3)；Lamp world =
        // (1,2,3) × (2,1,0) = (3,3,3)。验证"importer 写 local"+"引擎累积"端到端对位。
        ::Orange::Engine::Scene::PropagateWorldTransforms(world);
        const auto* aWT =
            world.GetComponent<::Orange::Engine::Scene::WorldTransformComponent>(childA);
        assert(aWT != nullptr &&
               std::fabs(aWT->world[3].x - 1.5f) < 1e-4f &&
               std::fabs(aWT->world[3].y - 2.0f) < 1e-4f &&
               std::fabs(aWT->world[3].z - 3.0f) < 1e-4f &&
               "end-to-end：ChildA local(0.5,0,0) 经引擎累积 → world (1.5,2,3)");
        const auto* lWT =
            world.GetComponent<::Orange::Engine::Scene::WorldTransformComponent>(lamp);
        assert(lWT != nullptr &&
               std::fabs(lWT->world[3].x - 3.0f) < 1e-4f &&
               std::fabs(lWT->world[3].y - 3.0f) < 1e-4f &&
               std::fabs(lWT->world[3].z - 3.0f) < 1e-4f &&
               "end-to-end：Lamp local(2,1,0) 经引擎累积 → world (3,3,3)");
        std::fprintf(stdout,
                     "  [PASS] end-to-end：importer 写 local + 引擎累积 → ChildA world "
                     "(1.5,2,3) / Lamp world (3,3,3)\n");
    }

    // ===== hash-skip 增量短路：改 scene.json 后重导同源应跳过（不覆盖手工编辑）=====
    {
        // 往已产出的 scene.json 写一个 marker（模拟用户手工编辑），重导同一
        // .gltf（源 hash 未变）应命中 scene .meta 的 hash 短路 → 跳过 Scene::Save，
        // marker 保留（与单 mesh importer T5 同款语义）。
        const std::string scenePath = r.destPath;
        {
            std::ofstream ofs(scenePath, std::ios::binary | std::ios::trunc);
            ofs << "MANUAL_EDIT_MARKER";
        }
        auto reg = MakeImportRegistry();
        const ImportNS::ImportResult r2 =
            ImportNS::RunGltfSceneImportToRegistry(gltfPath, *reg);
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

    // ===== 第二组：has_matrix 分解 + 3 层深嵌套 world 累积 + spot light =====
    {
        const std::string mPath = (srcDir / "matrix_deep.gltf").generic_string();
        WriteMatrixAndDeepNestGltf(mPath);

        auto reg2 = MakeImportRegistry();
        const ImportNS::ImportResult r2 =
            ImportNS::RunGltfSceneImportToRegistry(mPath, *reg2);
        assert(r2.status == ImportNS::ImportStatus::Success && "matrix/深嵌套 导入应 Success");
        assert(fs::exists(r2.destPath) && "matrix_deep.scene.json 应落盘");

        World w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg2.get();
        auto lr = SceneNS::Load(r2.destPath, w, opts);
        assert(lr.IsOk() && "matrix_deep scene 应能 Load");

        std::size_t cnt = 0;
        for (auto e : w.Registry().view<SceneNS::NameComponent>()) { (void)e; ++cnt; }
        assert(cnt == 5 && "应有 5 实体（L1/L2/L3/MatrixNode/SpotNode）");

        // L3：importer 写 **local** = (0,0,2)；其 world 由引擎累积穿 2 层祖先
        // L1(10,0,0)→L2(0,5,0)→L3(0,0,2) = (10,5,2)（下方 end-to-end 验）。
        const Entity l3 = FindByName(w, "L3");
        const auto* l3T = w.GetComponent<SceneNS::TransformComponent>(l3);
        assert(l3T != nullptr &&
               std::fabs(l3T->position.x - 0.0f) < 1e-3f &&
               std::fabs(l3T->position.y - 0.0f) < 1e-3f &&
               std::fabs(l3T->position.z - 2.0f) < 1e-3f &&
               "L3 local 应是 (0,0,2)（importer 写 local）");
        ::Orange::Engine::Scene::PropagateWorldTransforms(w);
        const auto* l3WT =
            w.GetComponent<::Orange::Engine::Scene::WorldTransformComponent>(l3);
        assert(l3WT != nullptr &&
               std::fabs(l3WT->world[3].x - 10.0f) < 1e-3f &&
               std::fabs(l3WT->world[3].y - 5.0f) < 1e-3f &&
               std::fabs(l3WT->world[3].z - 2.0f) < 1e-3f &&
               "end-to-end：L3 local(0,0,2) 经引擎累积穿 2 层祖先 → world (10,5,2)");

        // MatrixNode：has_matrix 列主序 translate(3,4,5)*scale(2,2,2) →
        // decompose position (3,4,5) + scale (2,2,2)。
        const Entity mn = FindByName(w, "MatrixNode");
        const auto* mnT = w.GetComponent<SceneNS::TransformComponent>(mn);
        assert(mnT != nullptr &&
               std::fabs(mnT->position.x - 3.0f) < 1e-3f &&
               std::fabs(mnT->position.y - 4.0f) < 1e-3f &&
               std::fabs(mnT->position.z - 5.0f) < 1e-3f &&
               "MatrixNode position 应从 matrix 分解为 (3,4,5)");
        assert(std::fabs(mnT->scale.x - 2.0f) < 1e-3f &&
               std::fabs(mnT->scale.y - 2.0f) < 1e-3f &&
               std::fabs(mnT->scale.z - 2.0f) < 1e-3f &&
               "MatrixNode scale 应从 matrix 分解为 (2,2,2)");

        // SpotNode：SpotLight，cone 角映射 + 方向编码（无 rotation → (0,0,-1)）。
        namespace RenderNS = ::Orange::Engine::Render;
        const Entity spot = FindByName(w, "SpotNode");
        const auto* sl = w.GetComponent<RenderNS::SpotLight>(spot);
        assert(sl != nullptr && "SpotNode 应有 SpotLight component");
        assert(std::fabs(sl->intensity - 3.0f / 683.0f) < 1e-4f &&
               std::fabs(sl->range - 12.0f) < 1e-4f &&
               std::fabs(sl->innerConeAngle - 0.2f) < 1e-4f &&
               std::fabs(sl->outerConeAngle - 0.5f) < 1e-4f &&
               "SpotLight intensity(÷683)/range/cone 应 = glTF (3÷683/12/0.2/0.5)");
        const auto* spotT = w.GetComponent<SceneNS::TransformComponent>(spot);
        const glm::vec3 spotDir = RenderNS::ComputeSpotLightWorldDir(spotT->rotation);
        assert(std::fabs(spotDir.z - (-1.0f)) < 1e-3f &&
               std::fabs(spotDir.x) < 1e-3f && std::fabs(spotDir.y) < 1e-3f &&
               "spot 方向应沿 glTF -Z 转引擎约定后为 (0,0,-1)");

        std::fprintf(stdout,
                     "  [PASS] has_matrix 分解 + 3 层深嵌套 world 累积 (10,5,2) + "
                     "SpotLight 锥角/方向\n");
    }

    // ===== 第三组：mesh instancing —— 多 node 共用同一 mesh 去重 =====
    {
        const std::string iPath = (srcDir / "instanced.gltf").generic_string();
        WriteInstancedMeshGltf(iPath);

        auto reg = MakeImportRegistry();
        const ImportNS::ImportResult ri =
            ImportNS::RunGltfSceneImportToRegistry(iPath, *reg);
        assert(ri.status == ImportNS::ImportStatus::Success && "实例化导入应 Success");
        // 2 node 共用 mesh 0 → 只写 1 个 .mesh（按指针去重）。
        assert(ri.message.find("meshes=1") != std::string::npos &&
               "2 node 共用同一 mesh → 只写 1 个 .mesh（按指针去重）");

        World w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg.get();
        auto lr = SceneNS::Load(ri.destPath, w, opts);
        assert(lr.IsOk() && "实例化 scene 应能 Load");

        const Entity a = FindByName(w, "InstA");
        const Entity b = FindByName(w, "InstB");
        const auto* aR = w.GetComponent<::Orange::Engine::Render::RenderableComponent>(a);
        const auto* bR = w.GetComponent<::Orange::Engine::Render::RenderableComponent>(b);
        assert(aR != nullptr && bR != nullptr &&
               aR->mesh.IsValid() && bR->mesh.IsValid() &&
               "两实例都应有有效 Renderable");
        assert(aR->mesh.Value() == bR->mesh.Value() &&
               "两实例应指向同一 mesh handle（去重共享，不是各写一份）");

        std::size_t meshFiles = 0;
        for (const auto& de : fs::directory_iterator(fs::path("assets/Models/instanced")))
        {
            if (de.path().extension() == ".mesh") { ++meshFiles; }
        }
        assert(meshFiles == 1 && "实例化只应产出 1 个 .mesh 文件（不是 2 份）");

        std::fprintf(stdout,
                     "  [PASS] mesh instancing：2 node 共用 mesh → 1 .mesh + 共享 handle\n");
    }

    // ===== 第四组：hash-skip 正确性反向验证 —— 源改了不该 over-skip =====
    {
        const std::string mutPath = (srcDir / "mutate.gltf").generic_string();
        // v1：5 实体的层级场景。
        WriteSceneHierarchyGltf(mutPath);
        auto reg1 = MakeImportRegistry();
        const auto rv1 = ImportNS::RunGltfSceneImportToRegistry(mutPath, *reg1);
        assert(rv1.status == ImportNS::ImportStatus::Success && "v1 导入应 Success");
        // v2：把同一源文件覆盖成 2 实体的实例化场景（源 hash 变）。
        WriteInstancedMeshGltf(mutPath);
        auto reg2 = MakeImportRegistry();
        const auto rv2 = ImportNS::RunGltfSceneImportToRegistry(mutPath, *reg2);
        assert(rv2.status == ImportNS::ImportStatus::Success && "v2 导入应 Success");
        assert(rv2.message.find("unchanged") == std::string::npos &&
               "源改了 → 不应 over-skip（hash 不匹配应真重导，而非永远跳过）");
        // 重导后 scene.json 反映 v2（2 实体，而非 v1 残留的 5）。
        World w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg2.get();
        auto lr = SceneNS::Load(rv2.destPath, w, opts);
        assert(lr.IsOk() && "v2 scene 应能 Load");
        std::size_t cnt = 0;
        for (auto e : w.Registry().view<SceneNS::NameComponent>()) { (void)e; ++cnt; }
        assert(cnt == 2 && "重导后应是 v2 的 2 实体（确认真重导覆盖了 v1）");
        std::fprintf(stdout,
                     "  [PASS] hash-skip 正确性：源改了真重导（5→2 实体），不 over-skip\n");
    }

    // ===== 第五组：无 scenes 数组 → fallback 取 parent-less node 当根 =====
    {
        const std::string nsPath = (srcDir / "no_scenes.gltf").generic_string();
        WriteNoScenesGltf(nsPath);
        auto reg = MakeImportRegistry();
        const auto rn = ImportNS::RunGltfSceneImportToRegistry(nsPath, *reg);
        assert(rn.status == ImportNS::ImportStatus::Success &&
               "无 scenes 的 glTF 应走 fallback 成功导入（不崩）");

        World w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg.get();
        auto lr = SceneNS::Load(rn.destPath, w, opts);
        assert(lr.IsOk() && "no_scenes scene 应能 Load");
        const Entity lone = FindByName(w, "Lone");
        assert(w.IsValid(lone) && "fallback 应把 parent-less node 'Lone' 当根导入");
        assert(w.GetComponent<::Orange::Engine::Render::RenderableComponent>(lone) != nullptr &&
               "Lone 应有 Renderable");
        std::fprintf(stdout,
                     "  [PASS] 无 scenes 数组：fallback 取 parent-less node 当根\n");
    }

    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(testRoot, ec);

    std::fprintf(stdout, "[GltfSceneImportTest] all tests passed.\n");
    return 0;
}
