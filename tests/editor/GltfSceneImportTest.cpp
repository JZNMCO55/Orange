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

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/SubMeshMaterialsComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/TransformSystem.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp> // glm::quat / glm::slerp（动画 quat 轨道断言）
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp> // glm::radians
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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
        auto rm       = registry->RegisterLoader<AssetNS::MeshAsset>(
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
        ofs << "{\n"
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
               "\"data:application/octet-stream;base64,"
            << kBufferB64 << "\"}]\n"
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
        ofs << "{\n"
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
               "\"data:application/octet-stream;base64,"
            << kBufferB64 << "\"}]\n"
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
        ofs << "{\n"
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
               "\"data:application/octet-stream;base64,"
            << kBufferB64 << "\"}]\n"
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
        ofs << "{\n"
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
               "\"data:application/octet-stream;base64,"
            << kBufferB64 << "\"}]\n"
                             "}\n";
    }

    // 第六个 fixture：**旋转父下的 directional 灯**（lights-only，无 mesh/buffer）——
    // 验 A1.1 后 importer 的 R-bridging 与"光源消费者读 world rotation"配套。父 RotParent
    // 绕 +Y 转 90°（glTF quat [x,y,z,w]=[0, √½, 0, √½]）→ 子 ChildSun（directional，无自身
    // rotation）。glTF 灯沿 node 本地 -Z，父把 (0,0,-1) 转到世界 (-1,0,0)。importer 只写
    // local rotation（-Z→-Y 桥接），引擎 PropagateWorldTransforms 累积父旋转后，消费者
    // 同款公式算出的世界光向应 = (-1,0,0)。旧的"world 光向直接编码进 local"会在这里被父
    // 旋转二次应用得到错误方向 → 本例锁住修正。
    void WriteRotatedParentLightGltf(const std::string& path)
    {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        assert(ofs.is_open() && "写旋转父灯光 .gltf fixture 应成功");
        ofs << "{\n"
               "  \"asset\": {\"version\": \"2.0\"},\n"
               "  \"extensionsUsed\": [\"KHR_lights_punctual\"],\n"
               "  \"extensions\": {\"KHR_lights_punctual\": {\"lights\": [\n"
               "    {\"name\": \"Sun\", \"type\": \"directional\", "
               "\"color\": [1.0, 1.0, 1.0], \"intensity\": 1.0}\n"
               "  ]}},\n"
               "  \"scene\": 0,\n"
               "  \"scenes\": [{\"nodes\": [0]}],\n"
               "  \"nodes\": [\n"
               "    {\"name\": \"RotParent\", "
               "\"rotation\": [0.0, 0.70710678, 0.0, 0.70710678], \"children\": [1]},\n"
               "    {\"name\": \"ChildSun\", "
               "\"extensions\": {\"KHR_lights_punctual\": {\"light\": 0}}}\n"
               "  ]\n"
               "}\n";
    }

    // 第七个 fixture：**per-mesh PBR material**（G2）—— 覆盖：
    //   * 单 material mesh（SoloNode 引用 mesh 0，1 primitive → material "Red"）
    //   * 多 material mesh（MultiNode 引用 mesh 1，2 primitive → material "Red" / "Blue"）
    //   * material 去重：material "Red" 被 mesh 0 + mesh 1 的 primitive 0 共用 →
    //     全局只写一个 <basename>_Red.material（不是每次新建）。
    // 期望产物：2 个 .material（Red + Blue）；SoloNode 的 Renderable.materialInstanceId
    // 指向 Red.material；MultiNode 的 SubMeshMaterials slots = [Red.material, Blue.material]。
    void WriteMaterialSceneGltf(const std::string& path)
    {
        // positions 48B（accessor 0）+ idxA 12B（6 idx，accessor 1）+ idxB 6B
        // （3 idx，accessor 2）。bufferView 0 = pos[0,48)，1 = idxA[48,60)，
        // 2 = idxB[60,66)。mesh 1 两 primitive 各用一个 index accessor + material。
        static const char* kBufferB64 =
            "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA"
            "AAABAAIAAAACAAMAAAACAAMA";

        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        assert(ofs.is_open() && "写 material 场景 .gltf fixture 应成功");
        ofs << "{\n"
               "  \"asset\": {\"version\": \"2.0\"},\n"
               "  \"scene\": 0,\n"
               "  \"scenes\": [{\"nodes\": [0, 1]}],\n"
               "  \"nodes\": [\n"
               "    {\"name\": \"SoloNode\",  \"mesh\": 0},\n"
               "    {\"name\": \"MultiNode\", \"mesh\": 1}\n"
               "  ],\n"
               "  \"materials\": [\n"
               "    {\"name\": \"Red\",  \"pbrMetallicRoughness\": "
               "{\"baseColorFactor\": [1.0, 0.0, 0.0, 1.0], "
               "\"metallicFactor\": 0.1, \"roughnessFactor\": 0.7}},\n"
               "    {\"name\": \"Blue\", \"pbrMetallicRoughness\": "
               "{\"baseColorFactor\": [0.0, 0.0, 1.0, 1.0], "
               "\"metallicFactor\": 0.9, \"roughnessFactor\": 0.2}}\n"
               "  ],\n"
               "  \"meshes\": [\n"
               // mesh 0：单 primitive，material 0（Red）。
               "    {\"name\": \"Solo\", \"primitives\": [{\"attributes\": "
               "{\"POSITION\": 0}, \"indices\": 1, \"material\": 0}]},\n"
               // mesh 1：两 primitive，material 0（Red）+ material 1（Blue）。
               "    {\"name\": \"Multi\", \"primitives\": [\n"
               "      {\"attributes\": {\"POSITION\": 0}, \"indices\": 1, \"material\": 0},\n"
               "      {\"attributes\": {\"POSITION\": 0}, \"indices\": 2, \"material\": 1}\n"
               "    ]}\n"
               "  ],\n"
               "  \"accessors\": [\n"
               "    {\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, "
               "\"type\": \"VEC3\", \"min\": [0,0,0], \"max\": [1,1,0]},\n"
               "    {\"bufferView\": 1, \"componentType\": 5123, \"count\": 6, "
               "\"type\": \"SCALAR\"},\n"
               "    {\"bufferView\": 2, \"componentType\": 5123, \"count\": 3, "
               "\"type\": \"SCALAR\"}\n"
               "  ],\n"
               "  \"bufferViews\": [\n"
               "    {\"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 48},\n"
               "    {\"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 12},\n"
               "    {\"buffer\": 0, \"byteOffset\": 60, \"byteLength\": 6}\n"
               "  ],\n"
               "  \"buffers\": [{\"byteLength\": 66, \"uri\": "
               "\"data:application/octet-stream;base64,"
            << kBufferB64 << "\"}]\n"
                             "}\n";
    }

    // 第八个 fixture：**glTF cameras**（G3）—— camera-only，无 mesh/buffer。覆盖：
    //   * 透视相机（全参 yfov/aspectRatio/znear/zfar）
    //   * 透视相机（仅 yfov/znear，缺 aspectRatio/zfar → 验导入侧默认 16:9 / far 1000）
    //   * 正交相机（xmag/ymag/znear/zfar → 半宽高映 left/right/bottom/top）
    // 验 node.camera → Render::Camera component，投影矩阵烘焙正确（与 Camera::Perspective /
    // Orthographic 工厂逐元素对位）+ Save→Load round-trip 保住 projection。
    void WriteCameraSceneGltf(const std::string& path)
    {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        assert(ofs.is_open() && "写相机场景 .gltf fixture 应成功");
        ofs << "{\n"
               "  \"asset\": {\"version\": \"2.0\"},\n"
               "  \"scene\": 0,\n"
               "  \"scenes\": [{\"nodes\": [0, 1, 2]}],\n"
               "  \"nodes\": [\n"
               "    {\"name\": \"PerspCam\", \"translation\": [0.0, 1.0, 5.0], \"camera\": 0},\n"
               "    {\"name\": \"PerspDefaultCam\", \"camera\": 1},\n"
               "    {\"name\": \"OrthoCam\", \"camera\": 2}\n"
               "  ],\n"
               "  \"cameras\": [\n"
               "    {\"name\": \"Persp\", \"type\": \"perspective\", \"perspective\": "
               "{\"yfov\": 0.6981317, \"aspectRatio\": 1.5, \"znear\": 0.1, \"zfar\": 100.0}},\n"
               "    {\"name\": \"PerspDefault\", \"type\": \"perspective\", \"perspective\": "
               "{\"yfov\": 0.5, \"znear\": 0.2}},\n"
               "    {\"name\": \"Ortho\", \"type\": \"orthographic\", \"orthographic\": "
               "{\"xmag\": 4.0, \"ymag\": 3.0, \"znear\": 0.1, \"zfar\": 50.0}}\n"
               "  ]\n"
               "}\n";
    }

    // 第九个 fixture：**node TRS animation**（DCC→clip 桥）—— 单 node（带 mesh）被一条
    // translation channel + 一条 rotation channel 驱动（2 帧，LINEAR）。验 importer 把
    // cgltf animation 解析成 AnimationClip 挂 AnimatorComponent(ClipAnimator)：
    //   * translation → "position" Vec3 轨道：(0,0,0) @t0 → (2,0,0) @t1
    //   * rotation    → "rotation.quat" Quat 轨道：identity @t0 → 90°Y @t1（最短弧 slerp）
    //   * duration = 1.0；Seek(0.5) → position (1,0,0) + rotation 45°Y
    // buffer 布局（base64，4-byte 对齐，由 tests 内联生成脚本算出）：
    //   acc0 pos VEC3×4 [0,48) / acc1 idx u16×6 [48,60) / acc2 time f32×2 [60,68) /
    //   acc3 trans VEC3×2 [68,92) / acc4 rot VEC4×2 [92,124)。
    void WriteAnimatedNodeGltf(const std::string& path)
    {
        static const char* kBufferB64 =
            "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA"
            "AAABAAIAAAACAAMAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAIA/AAAAAPMENT8AAAAA8wQ1Pw==";

        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        assert(ofs.is_open() && "写动画 .gltf fixture 应成功");
        ofs << "{\n"
               "  \"asset\": {\"version\": \"2.0\"},\n"
               "  \"scene\": 0,\n"
               "  \"scenes\": [{\"nodes\": [0]}],\n"
               "  \"nodes\": [\n"
               "    {\"name\": \"Spinner\", \"mesh\": 0}\n"
               "  ],\n"
               "  \"meshes\": [\n"
               "    {\"name\": \"Quad\", \"primitives\": [{\"attributes\": "
               "{\"POSITION\": 0}, \"indices\": 1}]}\n"
               "  ],\n"
               "  \"animations\": [\n"
               "    {\"name\": \"Spin\",\n"
               "     \"samplers\": [\n"
               "       {\"input\": 2, \"output\": 3, \"interpolation\": \"LINEAR\"},\n"
               "       {\"input\": 2, \"output\": 4, \"interpolation\": \"LINEAR\"}\n"
               "     ],\n"
               "     \"channels\": [\n"
               "       {\"sampler\": 0, \"target\": {\"node\": 0, \"path\": \"translation\"}},\n"
               "       {\"sampler\": 1, \"target\": {\"node\": 0, \"path\": \"rotation\"}}\n"
               "     ]}\n"
               "  ],\n"
               "  \"accessors\": [\n"
               "    {\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, "
               "\"type\": \"VEC3\", \"min\": [0,0,0], \"max\": [1,1,0]},\n"
               "    {\"bufferView\": 1, \"componentType\": 5123, \"count\": 6, "
               "\"type\": \"SCALAR\"},\n"
               "    {\"bufferView\": 2, \"componentType\": 5126, \"count\": 2, "
               "\"type\": \"SCALAR\", \"min\": [0.0], \"max\": [1.0]},\n"
               "    {\"bufferView\": 3, \"componentType\": 5126, \"count\": 2, "
               "\"type\": \"VEC3\"},\n"
               "    {\"bufferView\": 4, \"componentType\": 5126, \"count\": 2, "
               "\"type\": \"VEC4\"}\n"
               "  ],\n"
               "  \"bufferViews\": [\n"
               "    {\"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 48},\n"
               "    {\"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 12},\n"
               "    {\"buffer\": 0, \"byteOffset\": 60, \"byteLength\": 8},\n"
               "    {\"buffer\": 0, \"byteOffset\": 68, \"byteLength\": 24},\n"
               "    {\"buffer\": 0, \"byteOffset\": 92, \"byteLength\": 32}\n"
               "  ],\n"
               "  \"buffers\": [{\"byteLength\": 124, \"uri\": "
               "\"data:application/octet-stream;base64,"
            << kBufferB64 << "\"}]\n"
                             "}\n";
    }

    // 在 Load 回来的 World 里按名字找实体（名字唯一）。找不到返回 Invalid。
    Entity FindByName(World& world, const std::string& name)
    {
        Entity found = Entity::Invalid();
        auto   view  = world.Registry().view<SceneNS::NameComponent>();
        for (auto e : view)
        {
            const Entity ent = World::FromEntt(e);
            const auto*  nc  = world.GetComponent<SceneNS::NameComponent>(ent);
            if (nc != nullptr && nc->name == name)
            {
                found = ent;
                break;
            }
        }
        return found;
    }

} // namespace

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

    auto                         registry = MakeImportRegistry();
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
    const fs::path modelDir   = fs::path("assets/Models/scene_hier");
    const fs::path crateMesh  = modelDir / "scene_hier_Crate.mesh";
    const fs::path barrelMesh = modelDir / "scene_hier_Barrel.mesh";
    assert(fs::exists(crateMesh) && "mesh 0 应单独写 scene_hier_Crate.mesh");
    assert(fs::exists(barrelMesh) && "mesh 1 应单独写 scene_hier_Barrel.mesh（不与 mesh 0 合并）");
    assert(fs::exists(crateMesh.generic_string() + ".meta") && "mesh .meta 应落盘");
    std::fprintf(stdout, "  [PASS] 2 个独立 .mesh（每 cgltf mesh 单独写，不塌平）\n");

    // ===== Scene::Load round-trip：实体数 / 父子关系 / transform / renderable =====
    {
        World                world;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = registry.get();
        auto loadRes       = SceneNS::Load(r.destPath, world, opts);
        assert(loadRes.IsOk() && "产出的 .scene.json 应能被 Scene::Load 加载");

        // 5 个实体（RootGroup + ChildA + ChildB + SunLight + Lamp）。
        std::size_t named = 0;
        for (auto e : world.Registry().view<SceneNS::NameComponent>())
        {
            (void)e;
            ++named;
        }
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
        auto                         reg = MakeImportRegistry();
        const ImportNS::ImportResult r2 =
            ImportNS::RunGltfSceneImportToRegistry(gltfPath, *reg);
        assert(r2.status == ImportNS::ImportStatus::Success &&
               "重导未改源应 Success（hash 短路）");
        assert(r2.message.find("unchanged") != std::string::npos &&
               "重导未改源应走 hash 短路（message 含 unchanged）");
        std::ifstream     ifs(scenePath, std::ios::binary);
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

        auto                         reg2 = MakeImportRegistry();
        const ImportNS::ImportResult r2 =
            ImportNS::RunGltfSceneImportToRegistry(mPath, *reg2);
        assert(r2.status == ImportNS::ImportStatus::Success && "matrix/深嵌套 导入应 Success");
        assert(fs::exists(r2.destPath) && "matrix_deep.scene.json 应落盘");

        World                w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg2.get();
        auto lr            = SceneNS::Load(r2.destPath, w, opts);
        assert(lr.IsOk() && "matrix_deep scene 应能 Load");

        std::size_t cnt = 0;
        for (auto e : w.Registry().view<SceneNS::NameComponent>())
        {
            (void)e;
            ++cnt;
        }
        assert(cnt == 5 && "应有 5 实体（L1/L2/L3/MatrixNode/SpotNode）");

        // L3：importer 写 **local** = (0,0,2)；其 world 由引擎累积穿 2 层祖先
        // L1(10,0,0)→L2(0,5,0)→L3(0,0,2) = (10,5,2)（下方 end-to-end 验）。
        const Entity l3  = FindByName(w, "L3");
        const auto*  l3T = w.GetComponent<SceneNS::TransformComponent>(l3);
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
        const Entity mn  = FindByName(w, "MatrixNode");
        const auto*  mnT = w.GetComponent<SceneNS::TransformComponent>(mn);
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
        const Entity spot  = FindByName(w, "SpotNode");
        const auto*  sl    = w.GetComponent<RenderNS::SpotLight>(spot);
        assert(sl != nullptr && "SpotNode 应有 SpotLight component");
        assert(std::fabs(sl->intensity - 3.0f / 683.0f) < 1e-4f &&
               std::fabs(sl->range - 12.0f) < 1e-4f &&
               std::fabs(sl->innerConeAngle - 0.2f) < 1e-4f &&
               std::fabs(sl->outerConeAngle - 0.5f) < 1e-4f &&
               "SpotLight intensity(÷683)/range/cone 应 = glTF (3÷683/12/0.2/0.5)");
        const auto*     spotT   = w.GetComponent<SceneNS::TransformComponent>(spot);
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

        auto                         reg = MakeImportRegistry();
        const ImportNS::ImportResult ri =
            ImportNS::RunGltfSceneImportToRegistry(iPath, *reg);
        assert(ri.status == ImportNS::ImportStatus::Success && "实例化导入应 Success");
        // 2 node 共用 mesh 0 → 只写 1 个 .mesh（按指针去重）。
        assert(ri.message.find("meshes=1") != std::string::npos &&
               "2 node 共用同一 mesh → 只写 1 个 .mesh（按指针去重）");

        World                w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg.get();
        auto lr            = SceneNS::Load(ri.destPath, w, opts);
        assert(lr.IsOk() && "实例化 scene 应能 Load");

        const Entity a  = FindByName(w, "InstA");
        const Entity b  = FindByName(w, "InstB");
        const auto*  aR = w.GetComponent<::Orange::Engine::Render::RenderableComponent>(a);
        const auto*  bR = w.GetComponent<::Orange::Engine::Render::RenderableComponent>(b);
        assert(aR != nullptr && bR != nullptr &&
               aR->mesh.IsValid() && bR->mesh.IsValid() &&
               "两实例都应有有效 Renderable");
        assert(aR->mesh.Value() == bR->mesh.Value() &&
               "两实例应指向同一 mesh handle（去重共享，不是各写一份）");

        std::size_t meshFiles = 0;
        for (const auto& de : fs::directory_iterator(fs::path("assets/Models/instanced")))
        {
            if (de.path().extension() == ".mesh")
            {
                ++meshFiles;
            }
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
        auto       reg1 = MakeImportRegistry();
        const auto rv1  = ImportNS::RunGltfSceneImportToRegistry(mutPath, *reg1);
        assert(rv1.status == ImportNS::ImportStatus::Success && "v1 导入应 Success");
        // v2：把同一源文件覆盖成 2 实体的实例化场景（源 hash 变）。
        WriteInstancedMeshGltf(mutPath);
        auto       reg2 = MakeImportRegistry();
        const auto rv2  = ImportNS::RunGltfSceneImportToRegistry(mutPath, *reg2);
        assert(rv2.status == ImportNS::ImportStatus::Success && "v2 导入应 Success");
        assert(rv2.message.find("unchanged") == std::string::npos &&
               "源改了 → 不应 over-skip（hash 不匹配应真重导，而非永远跳过）");
        // 重导后 scene.json 反映 v2（2 实体，而非 v1 残留的 5）。
        World                w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg2.get();
        auto lr            = SceneNS::Load(rv2.destPath, w, opts);
        assert(lr.IsOk() && "v2 scene 应能 Load");
        std::size_t cnt = 0;
        for (auto e : w.Registry().view<SceneNS::NameComponent>())
        {
            (void)e;
            ++cnt;
        }
        assert(cnt == 2 && "重导后应是 v2 的 2 实体（确认真重导覆盖了 v1）");
        std::fprintf(stdout,
                     "  [PASS] hash-skip 正确性：源改了真重导（5→2 实体），不 over-skip\n");
    }

    // ===== 第五组：无 scenes 数组 → fallback 取 parent-less node 当根 =====
    {
        const std::string nsPath = (srcDir / "no_scenes.gltf").generic_string();
        WriteNoScenesGltf(nsPath);
        auto       reg = MakeImportRegistry();
        const auto rn  = ImportNS::RunGltfSceneImportToRegistry(nsPath, *reg);
        assert(rn.status == ImportNS::ImportStatus::Success &&
               "无 scenes 的 glTF 应走 fallback 成功导入（不崩）");

        World                w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg.get();
        auto lr            = SceneNS::Load(rn.destPath, w, opts);
        assert(lr.IsOk() && "no_scenes scene 应能 Load");
        const Entity lone = FindByName(w, "Lone");
        assert(w.IsValid(lone) && "fallback 应把 parent-less node 'Lone' 当根导入");
        assert(w.GetComponent<::Orange::Engine::Render::RenderableComponent>(lone) != nullptr &&
               "Lone 应有 Renderable");
        std::fprintf(stdout,
                     "  [PASS] 无 scenes 数组：fallback 取 parent-less node 当根\n");
    }

    // ===== 第六组：旋转父下的 directional 灯 —— R-bridging + 消费者读 world 配套 =====
    // 父绕 Y 90°、子 directional 灯无自身 rotation。glTF 灯本地 -Z 被父转到世界
    // (-1,0,0)。importer 写 local（-Z→-Y 桥接），引擎累积父旋转后，按消费者同款公式
    // normalize(world * (0,-1,0,0)) 算世界光向应 ≈ (-1,0,0)。旧 world-dir 编码会得错误
    // 方向（父旋转二次应用），故本例直接锁住"importer + A1.1 消费者"端到端正确。
    {
        const std::string rpPath = (srcDir / "rot_parent_light.gltf").generic_string();
        WriteRotatedParentLightGltf(rpPath);
        auto       reg = MakeImportRegistry();
        const auto rp  = ImportNS::RunGltfSceneImportToRegistry(rpPath, *reg);
        assert(rp.status == ImportNS::ImportStatus::Success &&
               "旋转父灯光 scene 应导入 Success");

        World                w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg.get();
        auto lr            = SceneNS::Load(rp.destPath, w, opts);
        assert(lr.IsOk() && "旋转父灯光 scene 应能 Load");

        const Entity sun = FindByName(w, "ChildSun");
        assert(w.IsValid(sun) && "ChildSun 实体应存在");
        assert(w.GetComponent<::Orange::Engine::Render::DirectionalLight>(sun) != nullptr &&
               "ChildSun 应有 DirectionalLight component");

        ::Orange::Engine::Scene::PropagateWorldTransforms(w);
        const auto* sunWT =
            w.GetComponent<::Orange::Engine::Scene::WorldTransformComponent>(sun);
        assert(sunWT != nullptr && "ChildSun 应有 world transform cache");
        // 消费者（Pipeline.cpp）同款公式：世界光向 = normalize(world * 本地前向 (0,-1,0,0))。
        const glm::vec3 worldDir =
            glm::normalize(glm::vec3(sunWT->world * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));
        assert(std::fabs(worldDir.x - (-1.0f)) < 1e-3f &&
               std::fabs(worldDir.y) < 1e-3f &&
               std::fabs(worldDir.z) < 1e-3f &&
               "父绕 Y 90° 把灯本地 -Z 转到世界 (-1,0,0)：R-bridging + 累积父旋转后世界光向 "
               "应 = (-1,0,0)（旧 world-dir 编码会被父旋转二次应用得错误方向）");
        std::fprintf(stdout,
                     "  [PASS] 旋转父下 directional 灯：R-bridging + 累积父旋转 → 世界光向 (-1,0,0)\n");
    }

    // ===== 第七组：per-mesh PBR material（G2）—— 单 material / 多 material /
    //       material 去重 + Save→Load round-trip 保住材质引用 =====
    {
        namespace RenderNS = ::Orange::Engine::Render;

        const std::string mPath = (srcDir / "mat_scene.gltf").generic_string();
        WriteMaterialSceneGltf(mPath);

        auto                         reg = MakeImportRegistry();
        const ImportNS::ImportResult rm =
            ImportNS::RunGltfSceneImportToRegistry(mPath, *reg);
        assert(rm.status == ImportNS::ImportStatus::Success && "material 场景导入应 Success");
        // material "Red" 被 mesh 0 + mesh 1 primitive 0 共用 → 全局去重为 2 个
        // material（Red + Blue），不是 3 个。
        assert(rm.message.find("materials=2") != std::string::npos &&
               "Red 被两 mesh 共用 → 全局去重为 2 个 material（result message materials=2）");

        // ----- .material 文件落盘 + 去重：恰好 2 个 .material（Red + Blue）-----
        const fs::path matModelDir = fs::path("assets/Models/mat_scene");
        const fs::path redMat      = matModelDir / "mat_scene_Red.material";
        const fs::path blueMat     = matModelDir / "mat_scene_Blue.material";
        assert(fs::exists(redMat) && "Red material 应写出 mat_scene_Red.material");
        assert(fs::exists(blueMat) && "Blue material 应写出 mat_scene_Blue.material");
        std::size_t matFiles = 0;
        for (const auto& de : fs::directory_iterator(matModelDir))
        {
            if (de.path().extension() == ".material")
            {
                ++matFiles;
            }
        }
        assert(matFiles == 2 &&
               "恰好 2 个 .material（Red 被两 mesh 共用，全局去重不重复写）");
        std::fprintf(stdout,
                     "  [PASS] G2 .material 落盘 + 去重：Red/Blue 各一份（共用 material 不重复写）\n");

        // ----- 原始 scene.json 文本断言：material id 用 .material 路径 -----
        // Save 把 sentinel materialInstance 经 namedMaterialInstances 反查成
        // .material 路径写进 materialInstanceId / subMeshMaterials slots。
        std::ifstream     ifs(rm.destPath, std::ios::binary);
        const std::string json((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
        assert(json.find("mat_scene_Red.material") != std::string::npos &&
               "scene.json 应含 Red .material 路径作为 material id");
        assert(json.find("mat_scene_Blue.material") != std::string::npos &&
               "scene.json 应含 Blue .material 路径作为 material id");
        assert(json.find("\"materialInstanceId\"") != std::string::npos &&
               "scene.json 应写出 materialInstanceId 字段（单 material mesh 路径）");
        assert(json.find("\"SubMeshMaterials\"") != std::string::npos &&
               "scene.json 应写出 SubMeshMaterials 段（多 material mesh 路径）");
        std::fprintf(stdout,
                     "  [PASS] G2 scene.json：materialInstanceId + SubMeshMaterials 用 .material 路径\n");

        // ----- Save→Load round-trip：注入 materialResolver（path → sentinel
        //       instance），断言单 material 实体 materialInstance 非空 + 多
        //       material 实体 SubMeshMaterials slots 各段材质正确 -----
        std::vector<std::unique_ptr<RenderNS::MaterialInstance>> owned;
        std::map<std::string, RenderNS::MaterialInstance*>       byPath;
        auto                                                     resolver = [&](const std::string& matId) -> RenderNS::MaterialInstance*
        {
            auto it = byPath.find(matId);
            if (it != byPath.end())
            {
                return it->second;
            }
            owned.push_back(std::make_unique<RenderNS::MaterialInstance>(nullptr));
            RenderNS::MaterialInstance* raw = owned.back().get();
            byPath[matId]                   = raw;
            return raw;
        };

        World                w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry    = reg.get();
        opts.materialResolver = resolver;
        auto lr               = SceneNS::Load(rm.destPath, w, opts);
        assert(lr.IsOk() && "material 场景应能 Load");

        // SoloNode：单 material → Renderable.materialInstance 非空（= Red sentinel）。
        const Entity solo = FindByName(w, "SoloNode");
        assert(w.IsValid(solo) && "SoloNode 实体应存在");
        const auto* soloR = w.GetComponent<RenderNS::RenderableComponent>(solo);
        assert(soloR != nullptr && soloR->mesh.IsValid() &&
               "SoloNode 应有 Renderable + 有效 mesh");
        assert(soloR->materialInstance != nullptr &&
               "SoloNode 单 material → Renderable.materialInstance 应非空（Red）");
        // resolver 是按 id 缓存的，Red 的 sentinel 应等于 byPath[Red 路径]。
        RenderNS::MaterialInstance* redInst =
            byPath.at(redMat.generic_string());
        assert(soloR->materialInstance == redInst &&
               "SoloNode materialInstance 应解析到 Red .material（id 路径正确）");
        assert(!w.HasComponent<RenderNS::SubMeshMaterialsComponent>(solo) &&
               "单 material mesh 不应挂 SubMeshMaterialsComponent");

        // MultiNode：多 material → SubMeshMaterials slots = [Red, Blue]。
        const Entity multi = FindByName(w, "MultiNode");
        assert(w.IsValid(multi) && "MultiNode 实体应存在");
        const auto* multiR = w.GetComponent<RenderNS::RenderableComponent>(multi);
        assert(multiR != nullptr && multiR->mesh.IsValid() &&
               "MultiNode 应有 Renderable + 有效 mesh");
        const auto* smc =
            w.GetComponent<RenderNS::SubMeshMaterialsComponent>(multi);
        assert(smc != nullptr && "MultiNode 多 material → 应挂 SubMeshMaterialsComponent");
        assert(smc->slots.size() == 2 &&
               "MultiNode 两 primitive → 2 个 slot（Red / Blue）");
        RenderNS::MaterialInstance* blueInst =
            byPath.at(blueMat.generic_string());
        assert(smc->slots[0] == redInst &&
               "slot 0 应是 Red（primitive 0 material 0，与 mesh sub-mesh 段顺序对齐）");
        assert(smc->slots[1] == blueInst &&
               "slot 1 应是 Blue（primitive 1 material 1，段顺序对齐）");
        // slot 0 兜底到 Renderable.materialInstance（与导入侧 AttachMeshMaterials 一致）。
        assert(multiR->materialInstance == redInst &&
               "MultiNode Renderable.materialInstance 应兜底到 slot 0（Red）");
        std::fprintf(stdout,
                     "  [PASS] G2 round-trip：单 material→materialInstance(Red) / "
                     "多 material→SubMeshMaterials[Red,Blue] + slot 顺序对齐\n");
    }

    // ===== 第八组：glTF cameras（G3）—— node.camera → Render::Camera 投影烘焙 =====
    // camera-only 场景（无 mesh/buffer）。验透视/正交相机的投影矩阵烘焙正确（逐元素
    // 对位 Camera::Perspective/Orthographic 工厂）+ aspect/zfar 缺省默认 + Transform
    // 位姿（importer 不桥接，直接写 node translation）+ Save→Load round-trip 保 projection。
    {
        namespace RenderNS = ::Orange::Engine::Render;
        using RenderNS::Camera;

        const std::string cPath = (srcDir / "camera_scene.gltf").generic_string();
        WriteCameraSceneGltf(cPath);

        auto                         reg = MakeImportRegistry();
        const ImportNS::ImportResult rc =
            ImportNS::RunGltfSceneImportToRegistry(cPath, *reg);
        assert(rc.status == ImportNS::ImportStatus::Success && "相机场景导入应 Success");
        assert(rc.message.find("cameras=3") != std::string::npos &&
               "result message 应含 cameras=3（3 个相机 node 被计数）");

        World                w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg.get();
        auto lr            = SceneNS::Load(rc.destPath, w, opts);
        assert(lr.IsOk() && "相机场景应能 Load（Camera component round-trip）");

        // 逐元素比较投影矩阵（serialization round-trip 后用 1e-4 容差）。
        auto projMatches = [](const glm::mat4& a, const glm::mat4& b) -> bool
        {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (std::fabs(a[c][r] - b[c][r]) > 1e-4f)
                    {
                        return false;
                    }
            return true;
        };

        // PerspCam：全参 → Camera::Perspective(yfov 0.6981317, aspect 1.5, near 0.1, far 100)。
        const Entity persp = FindByName(w, "PerspCam");
        assert(w.IsValid(persp) && "PerspCam 实体应存在");
        const auto* pc = w.GetComponent<Camera>(persp);
        assert(pc != nullptr && "PerspCam 应有 Camera component");
        const glm::mat4 expectPersp =
            Camera::Perspective(0.6981317f, 1.5f, 0.1f, 100.0f).projection;
        assert(projMatches(pc->projection, expectPersp) &&
               "PerspCam projection 应 = Camera::Perspective(0.6981317,1.5,0.1,100) 烘焙结果");
        // 相机位姿在 Transform（importer 不桥接，直接写 node translation）。
        const auto* pcT = w.GetComponent<SceneNS::TransformComponent>(persp);
        assert(pcT != nullptr &&
               std::fabs(pcT->position.x - 0.0f) < 1e-4f &&
               std::fabs(pcT->position.y - 1.0f) < 1e-4f &&
               std::fabs(pcT->position.z - 5.0f) < 1e-4f &&
               "PerspCam 位姿应在 Transform = glTF translation (0,1,5)（无 -Z→-Y 桥接）");

        // PerspDefaultCam：缺 aspectRatio/zfar → 导入侧默认 16:9 / far 1000。
        const Entity perspDef = FindByName(w, "PerspDefaultCam");
        const auto*  pdc      = w.GetComponent<Camera>(perspDef);
        assert(pdc != nullptr && "PerspDefaultCam 应有 Camera component");
        const glm::mat4 expectDef =
            Camera::Perspective(0.5f, 16.0f / 9.0f, 0.2f, 1000.0f).projection;
        assert(projMatches(pdc->projection, expectDef) &&
               "PerspDefaultCam 缺 aspect/zfar → 应用默认 16:9 / far 1000 烘焙");

        // OrthoCam：xmag 4 / ymag 3 → Orthographic(-4,4,-3,3, 0.1, 50)（半宽高映盒子）。
        const Entity ortho = FindByName(w, "OrthoCam");
        const auto*  oc    = w.GetComponent<Camera>(ortho);
        assert(oc != nullptr && "OrthoCam 应有 Camera component");
        const glm::mat4 expectOrtho =
            Camera::Orthographic(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 50.0f).projection;
        assert(projMatches(oc->projection, expectOrtho) &&
               "OrthoCam projection 应 = Camera::Orthographic(-4,4,-3,3,0.1,50)（xmag/ymag 半宽高）");

        std::fprintf(stdout,
                     "  [PASS] glTF cameras（G3）：perspective(全参+默认 aspect/far) + "
                     "orthographic 投影烘焙 + Transform 位姿 + round-trip\n");
    }

    // ===== 第九组：node TRS animation（DCC→clip 桥）—— translation/rotation channel
    //       → AnimatorComponent(ClipAnimator)，quat 轨道最短弧 slerp + Save→Load 仍可播 =====
    {
        namespace AnimNS = ::Orange::Engine::Animation;

        const std::string aPath = (srcDir / "animated_node.gltf").generic_string();
        WriteAnimatedNodeGltf(aPath);

        auto                         reg = MakeImportRegistry();
        const ImportNS::ImportResult ra =
            ImportNS::RunGltfSceneImportToRegistry(aPath, *reg);
        assert(ra.status == ImportNS::ImportStatus::Success && "动画场景导入应 Success");
        // 一个 node 被 translation + rotation 两 channel 驱动 → 合并成 1 个 clip
        // （per-node 一个 ClipAnimator）→ animations=1。
        assert(ra.message.find("animations=1") != std::string::npos &&
               "result message 应含 animations=1（被驱动 node 挂 1 个 ClipAnimator）");
        std::fprintf(stdout, "  [PASS] 导入产出动画 clip：%s\n", ra.message.c_str());

        // Save→Load round-trip：Scene::Load 重建 ClipAnimator（消费内联 clipJson）+
        // SetTarget 到本 entity Transform。取出 ClipAnimator，Seek 后断言插值数值。
        World                w;
        SceneNS::LoadOptions opts;
        opts.assetRegistry = reg.get();
        auto lr            = SceneNS::Load(ra.destPath, w, opts);
        assert(lr.IsOk() && "动画场景应能 Load（AnimatorComponent/clipJson round-trip）");

        const Entity spinner = FindByName(w, "Spinner");
        assert(w.IsValid(spinner) && "Spinner 实体应存在");
        const auto* ac = w.GetComponent<AnimNS::AnimatorComponent>(spinner);
        assert(ac != nullptr && ac->animator != nullptr &&
               "Spinner 应有 AnimatorComponent（被动画驱动）");
        assert(ac->animator->BackendName() == "clip" &&
               "应是 ClipAnimator backend（clip）");
        auto* clipAnim = static_cast<AnimNS::ClipAnimator*>(ac->animator.get());

        // duration = 末关键帧时间 = 1.0（ParseAnimations RecomputeClipDuration）。
        assert(std::fabs(clipAnim->Duration() - 1.0f) < 1e-4f &&
               "clip duration 应 = 1.0（末关键帧时间）");

        // clip 应有 2 条轨道：position(Vec3) + rotation.quat(Quat)。
        const AnimNS::AnimationClip& clip = clipAnim->Clip();
        assert(clip.tracks.size() == 2 && "应有 position + rotation.quat 两条轨道");
        const AnimNS::AnimationTrack* posTr = AnimNS::FindTrack(clip, "position");
        const AnimNS::AnimationTrack* rotTr = AnimNS::FindTrack(clip, "rotation.quat");
        assert(posTr != nullptr && posTr->valueType == AnimNS::TrackValueType::Vec3 &&
               "position 轨道应为 Vec3");
        assert(rotTr != nullptr && rotTr->valueType == AnimNS::TrackValueType::Quat &&
               "rotation 轨道应为 Quat（quat 轨道，避欧拉 gimbal）");

        // SetTarget 必须由 Scene::Load 接到本 entity Transform——验 Seek 真写 Transform。
        const auto* tc = w.GetComponent<SceneNS::TransformComponent>(spinner);
        assert(tc != nullptr && "Spinner 应有 Transform（ClipAnimator 写目标）");

        // t=0：position (0,0,0) + rotation identity。
        clipAnim->Seek(0.0f);
        assert(std::fabs(tc->position.x - 0.0f) < 1e-4f &&
               std::fabs(tc->position.y - 0.0f) < 1e-4f &&
               std::fabs(tc->position.z - 0.0f) < 1e-4f &&
               "t=0：position 应为 (0,0,0)");
        const glm::quat ident = glm::quat(1, 0, 0, 0);
        assert(std::fabs(tc->rotation.x - ident.x) < 1e-3f &&
               std::fabs(tc->rotation.y - ident.y) < 1e-3f &&
               std::fabs(tc->rotation.z - ident.z) < 1e-3f &&
               std::fabs(std::fabs(tc->rotation.w) - 1.0f) < 1e-3f &&
               "t=0：rotation 应为 identity");

        // t=0.5：position 线性中点 (1,0,0)；rotation 最短弧 slerp 中点 = 45°Y。
        clipAnim->Seek(0.5f);
        assert(std::fabs(tc->position.x - 1.0f) < 1e-4f &&
               std::fabs(tc->position.y - 0.0f) < 1e-4f &&
               std::fabs(tc->position.z - 0.0f) < 1e-4f &&
               "t=0.5：position 线性中点应为 (1,0,0)");
        // 期望 = slerp(identity, 90°Y, 0.5) = 45°Y。把 +Z 转到约 (0.707,0,0.707)。
        const glm::quat expect45 =
            glm::slerp(glm::quat(1, 0, 0, 0),
                       glm::quat(glm::radians(glm::vec3(0, 90, 0))), 0.5f);
        // quat 与 -quat 表示同一旋转：用 |dot| 近 1 判定（避符号歧义）。
        const float qdot = std::fabs(glm::dot(tc->rotation, expect45));
        assert(qdot > 1.0f - 1e-3f &&
               "t=0.5：rotation 最短弧 slerp 中点应 = 45°Y（quat 轨道，非逐分量 mix）");
        const glm::vec3 dir = tc->rotation * glm::vec3(0, 0, 1);
        assert(std::fabs(dir.x - 0.7071f) < 2e-3f && std::fabs(dir.z - 0.7071f) < 2e-3f &&
               "45°Y 把 +Z 转到约 (0.707,0,0.707)（quat 插值朝向正确）");

        // t=1：position 终点 (2,0,0) + rotation 90°Y。
        clipAnim->Seek(1.0f);
        assert(std::fabs(tc->position.x - 2.0f) < 1e-4f &&
               "t=1：position 终点应为 (2,0,0)");
        const glm::vec3 dir1 = tc->rotation * glm::vec3(0, 0, 1);
        assert(std::fabs(dir1.x - 1.0f) < 2e-3f && std::fabs(dir1.z - 0.0f) < 2e-3f &&
               "t=1：90°Y 把 +Z 转到约 (1,0,0)");

        std::fprintf(stdout,
                     "  [PASS] node TRS animation：position Vec3 + rotation.quat Quat 轨道 + "
                     "duration 1.0 + Seek 插值（含 quat 最短弧中点 45°Y）+ Save→Load 可播\n");
    }

    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(testRoot, ec);

    std::fprintf(stdout, "[GltfSceneImportTest] all tests passed.\n");
    return 0;
}
