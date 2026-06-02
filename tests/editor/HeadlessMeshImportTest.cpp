// Headless mesh 导入端到端测试（GAP-2026-05-27 G1）—— 锁住 "资产导入不拉起
// GUI" 这条解耦链：通过 ImportDispatcher 的 registry-only seam
// （DispatchToRegistry / ImportObjMeshToRegistry）把 .obj / .gltf 导入一个仅
// 注册了 Mesh + Texture loader 的最小 AssetRegistry，断言产出 .mesh + .meta +
// 可被 MeshLoader 读回 + 确定性。
//
// 为什么能 headless：seam 只依赖 Orange::Engine::Asset::AssetRegistry&，不出现
// EditorHost / AudioEngine / ThumbnailService / Vulkan / ImGui / GLFW。本测试
// 直接编译真实 importer 源（ImportDispatcher.cpp / ObjImporter.cpp /
// GltfImporter.cpp / GltfMaterialParse.cpp / MetaSidecar.cpp / MeshTangentGen.cpp
// + MaterialFileIO.cpp）+ vendor 单 header（tinyobjloader / cgltf / mikktspace），
// 只链 OrangeEngine::orange_engine。覆盖的就是 GUI 路径委托到的同一份逻辑。
//
// CMake 用 CreateImportAssetRegistry 工厂的等价物（手工注册 Mesh + Texture
// loader），避免把 BuiltinAssets.cpp（拖 MaterialSystem / ShaderLoader 等编辑
// 器态）编进测试——只验导入落盘必需的两个 loader。
//
// .obj 路径自给 fixture（程序化写最小立方体），不依赖外部文件，干净 checkout
// 也跑。.gltf 路径走 Avocado fixture + `if(EXISTS)` 门控（CMake 注入
// ORANGE_ENGINE_GLTF_FIXTURE 时编 + 跑，否则该段编译期短路）。

#include "ImportDispatcher.h"  // include path 由 CMake 加 tools/OrangeEditor/import

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/asset/TextureLoader.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace ImportNS = ::Orange::Editor::Import;
namespace AssetNS  = ::Orange::Engine::Asset;
namespace fs       = std::filesystem;

namespace
{

// 建一个仅注册 Mesh + Texture loader 的最小 AssetRegistry —— 等价于
// BuiltinAssets::CreateImportAssetRegistry，但不编 BuiltinAssets.cpp（避免拖
// MaterialSystem / ShaderLoader 等编辑器态进测试）。
std::unique_ptr<AssetNS::AssetRegistry> MakeImportRegistry()
{
    auto registry = std::make_unique<AssetNS::AssetRegistry>();
    auto rm = registry->RegisterLoader<AssetNS::MeshAsset>(
        std::make_unique<AssetNS::MeshLoader>());
    assert(rm.IsOk() && "RegisterLoader<MeshAsset> 应成功");
    auto rt = registry->RegisterLoader<AssetNS::TextureAsset>(
        std::make_unique<AssetNS::TextureLoader>());
    assert(rt.IsOk() && "RegisterLoader<TextureAsset> 应成功");
    return registry;
}

// 写一个最小立方体 .obj（8 顶点 + 12 三角面，含 vt / vn）到给定路径。
// 立方体导入后去重的 unified vertex 数量取决于 face-vertex 三元组（pos,uv,nrm）
// 的唯一组合数——这里 6 面各 4 顶点 / 每面唯一 normal + 共享 uv 网格，故断言
// 用 ">0 且与第二次导入字节一致" 这种结构 / 确定性断言，而不是写死精确数量
// （避免与 importer dedup 细节耦合）。
void WriteMinimalCubeObj(const std::string& path)
{
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    assert(ofs.is_open() && "写 .obj fixture 应成功");
    // 单位立方体，中心在原点。8 个 position。
    ofs << "# minimal cube for HeadlessMeshImportTest\n";
    ofs << "v -0.5 -0.5 -0.5\n";
    ofs << "v  0.5 -0.5 -0.5\n";
    ofs << "v  0.5  0.5 -0.5\n";
    ofs << "v -0.5  0.5 -0.5\n";
    ofs << "v -0.5 -0.5  0.5\n";
    ofs << "v  0.5 -0.5  0.5\n";
    ofs << "v  0.5  0.5  0.5\n";
    ofs << "v -0.5  0.5  0.5\n";
    // texcoords。
    ofs << "vt 0.0 0.0\n";
    ofs << "vt 1.0 0.0\n";
    ofs << "vt 1.0 1.0\n";
    ofs << "vt 0.0 1.0\n";
    // 6 个 face normal。
    ofs << "vn  0.0  0.0 -1.0\n";  // -Z
    ofs << "vn  0.0  0.0  1.0\n";  // +Z
    ofs << "vn -1.0  0.0  0.0\n";  // -X
    ofs << "vn  1.0  0.0  0.0\n";  // +X
    ofs << "vn  0.0 -1.0  0.0\n";  // -Y
    ofs << "vn  0.0  1.0  0.0\n";  // +Y
    // 6 个 quad face（每面引用同一 normal + 4 个 uv 角），各拆 2 三角形。
    // 格式 v/vt/vn（1-based）。
    // -Z 面（1,2,3,4）
    ofs << "f 1/1/1 2/2/1 3/3/1\n";
    ofs << "f 1/1/1 3/3/1 4/4/1\n";
    // +Z 面（5,6,7,8）
    ofs << "f 5/1/2 6/2/2 7/3/2\n";
    ofs << "f 5/1/2 7/3/2 8/4/2\n";
    // -X 面（1,4,8,5）
    ofs << "f 1/1/3 4/2/3 8/3/3\n";
    ofs << "f 1/1/3 8/3/3 5/4/3\n";
    // +X 面（2,6,7,3）
    ofs << "f 2/1/4 6/2/4 7/3/4\n";
    ofs << "f 2/1/4 7/3/4 3/4/4\n";
    // -Y 面（1,5,6,2）
    ofs << "f 1/1/5 5/2/5 6/3/5\n";
    ofs << "f 1/1/5 6/3/5 2/4/5\n";
    // +Y 面（4,3,7,8）
    ofs << "f 4/1/6 3/2/6 7/3/6\n";
    ofs << "f 4/1/6 7/3/6 8/4/6\n";
}

// 写一个**无 UV** 的立方体 .obj（含 v / vn，无 vt；face 用 `v//vn` 格式）。
// 用于验证 tangent fallback 端到端路径：缺 UV → importer 跳过 MikkTSpace →
// 写出 .mesh（hasTangents=0 + 全零 UV 占位 + normal）→ Load 端 Lengyel
// （ComputeTangentsFromTriangles）对全零 UV 每三角 det=0 跳过、每顶点落
// ArbitraryTangent，产出与法线正交的有效 TBN（GAP-2026-05-25 gap ②）。
void WriteCubeObjNoUV(const std::string& path)
{
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    assert(ofs.is_open() && "写无 UV .obj fixture 应成功");
    ofs << "# cube without UV for tangent-fallback end-to-end test\n";
    ofs << "v -0.5 -0.5 -0.5\n";
    ofs << "v  0.5 -0.5 -0.5\n";
    ofs << "v  0.5  0.5 -0.5\n";
    ofs << "v -0.5  0.5 -0.5\n";
    ofs << "v -0.5 -0.5  0.5\n";
    ofs << "v  0.5 -0.5  0.5\n";
    ofs << "v  0.5  0.5  0.5\n";
    ofs << "v -0.5  0.5  0.5\n";
    ofs << "vn  0.0  0.0 -1.0\n";  // -Z
    ofs << "vn  0.0  0.0  1.0\n";  // +Z
    ofs << "vn -1.0  0.0  0.0\n";  // -X
    ofs << "vn  1.0  0.0  0.0\n";  // +X
    ofs << "vn  0.0 -1.0  0.0\n";  // -Y
    ofs << "vn  0.0  1.0  0.0\n";  // +Y
    // face 格式 v//vn（无 vt）。每面拆 2 三角。
    ofs << "f 1//1 2//1 3//1\n";  ofs << "f 1//1 3//1 4//1\n";  // -Z
    ofs << "f 5//2 6//2 7//2\n";  ofs << "f 5//2 7//2 8//2\n";  // +Z
    ofs << "f 1//3 4//3 8//3\n";  ofs << "f 1//3 8//3 5//3\n";  // -X
    ofs << "f 2//4 6//4 7//4\n";  ofs << "f 2//4 7//4 3//4\n";  // +X
    ofs << "f 1//5 5//5 6//5\n";  ofs << "f 1//5 6//5 2//5\n";  // -Y
    ofs << "f 4//6 3//6 7//6\n";  ofs << "f 4//6 7//6 8//6\n";  // +Y
}

// 读整个文件为字节，做确定性比较用。
std::vector<std::uint8_t> ReadAllBytes(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    assert(ifs.is_open() && "读 .mesh 字节应成功");
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(ifs),
                                     std::istreambuf_iterator<char>());
}

// .meta 文件文本里应含某子串（粗粒度断言 sourcePath / sourceHash 字段已写出，
// 不重新跑 JSON 解析——MetaSidecar 自身有专门的字段语义，本测试只锁 "sidecar
// 存在且含关键字段名"）。
bool FileContains(const std::string& path, const std::string& needle)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) { return false; }
    const std::string content((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
    return content.find(needle) != std::string::npos;
}

// 写一个最小的"双 material" 内嵌 .gltf fixture：一个 quad（4 顶点）拆成 2 个
// triangle primitive，各引用一个独立 material（slot 0 红 / slot 1 绿）。顶点 /
// 索引数据走 base64 data: URI 的单一 buffer（cgltf_load_buffers 自动解码），无
// 外部 .bin 依赖，干净 checkout 也能跑。导入后应产出：mesh 带 2 段 sub-mesh、
// 2 个 .material、materialSlot 覆盖 {0,1}。
//
// buffer 布局（小端 float / uint16，共 60 字节）：
//   positions: 4 × vec3 = 48 字节（offset 0）
//   indices  : prim0 {0,1,2} + prim1 {0,2,3} = 6 × uint16 = 12 字节（offset 48）
// base64 由 Python 预生成硬编码（避免测试里再实现 base64 编码器）。
void WriteTwoMaterialGltf(const std::string& path)
{
    // positions=(0,0,0)(1,0,0)(1,1,0)(0,1,0)；indices=0,1,2,0,2,3 的 base64。
    static const char* kBufferB64 =
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA"
        "AAABAAIAAAACAAMA";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    assert(ofs.is_open() && "写 .gltf fixture 应成功");
    ofs <<
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"scene\": 0,\n"
        "  \"scenes\": [{\"nodes\": [0]}],\n"
        "  \"nodes\": [{\"mesh\": 0}],\n"
        "  \"meshes\": [{\"primitives\": [\n"
        "    {\"attributes\": {\"POSITION\": 0}, \"indices\": 1, \"material\": 0},\n"
        "    {\"attributes\": {\"POSITION\": 0}, \"indices\": 2, \"material\": 1}\n"
        "  ]}],\n"
        "  \"materials\": [\n"
        "    {\"name\": \"RedMat\",   \"pbrMetallicRoughness\": "
        "{\"baseColorFactor\": [1.0, 0.0, 0.0, 1.0]}},\n"
        "    {\"name\": \"GreenMat\", \"pbrMetallicRoughness\": "
        "{\"baseColorFactor\": [0.0, 1.0, 0.0, 1.0]}}\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    {\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, "
        "\"type\": \"VEC3\", \"min\": [0,0,0], \"max\": [1,1,0]},\n"
        "    {\"bufferView\": 1, \"componentType\": 5123, \"count\": 3, "
        "\"type\": \"SCALAR\"},\n"
        "    {\"bufferView\": 1, \"componentType\": 5123, \"count\": 3, "
        "\"type\": \"SCALAR\", \"byteOffset\": 6}\n"
        "  ],\n"
        "  \"bufferViews\": [\n"
        "    {\"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 48},\n"
        "    {\"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 12}\n"
        "  ],\n"
        "  \"buffers\": [{\"byteLength\": 60, \"uri\": "
        "\"data:application/octet-stream;base64," << kBufferB64 << "\"}]\n"
        "}\n";
}

// 自包含**单 material** .gltf fixture（1 primitive，1 material）。验证单材质
// mesh 导入侧也把材质写进 .meta subMeshMaterials（供 drop 时自动设
// Renderable.materialInstance），不再像历史那样单材质留空。
void WriteSingleMaterialGltf(const std::string& path)
{
    // 同 two-material buffer：4 个 position（48B）+ 6 个 uint16 索引(0,1,2,0,2,3，12B）。
    static const char* kBufferB64 =
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAA"
        "AAABAAIAAAACAAMA";

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    assert(ofs.is_open() && "写单 material .gltf fixture 应成功");
    ofs <<
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"scene\": 0,\n"
        "  \"scenes\": [{\"nodes\": [0]}],\n"
        "  \"nodes\": [{\"mesh\": 0}],\n"
        "  \"meshes\": [{\"primitives\": [\n"
        "    {\"attributes\": {\"POSITION\": 0}, \"indices\": 1, \"material\": 0}\n"
        "  ]}],\n"
        "  \"materials\": [\n"
        "    {\"name\": \"SoloMat\", \"pbrMetallicRoughness\": "
        "{\"baseColorFactor\": [0.2, 0.4, 0.8, 1.0]}}\n"
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

}  // namespace

int main()
{
    // 把 cwd 切到一个隔离的临时目录 —— importer 把产物落到 cwd-相对的
    // assets/Models/<stem>/，本测试不能污染仓库根 assets/。
    const fs::path testRoot =
        fs::temp_directory_path() / "orange_headless_mesh_import_test";
    std::error_code ec;
    fs::remove_all(testRoot, ec);  // 清上次残留，保证确定性
    fs::create_directories(testRoot, ec);
    assert(!ec && "建临时测试根目录应成功");
    fs::current_path(testRoot, ec);
    assert(!ec && "切 cwd 到临时目录应成功");

    std::fprintf(stdout, "[HeadlessMeshImportTest] cwd=%s\n",
                 fs::current_path().string().c_str());

    // ===== 1. .obj headless 导入 → .mesh + .meta + 可读回 =====
    {
        // 程序化写最小立方体 fixture（不依赖外部文件）。
        const fs::path srcDir = testRoot / "src";
        fs::create_directories(srcDir, ec);
        const std::string objPath = (srcDir / "cube.obj").generic_string();
        WriteMinimalCubeObj(objPath);

        auto registry = MakeImportRegistry();

        // 走 registry-only seam（GUI Dispatch 委托到的同一份逻辑）。
        const ImportNS::ImportResult r =
            ImportNS::ImportObjMeshToRegistry(objPath, *registry);
        assert(r.status == ImportNS::ImportStatus::Success &&
               "obj headless 导入应 Success");
        assert(!r.destPath.empty() && "destPath 应非空");

        // 产物落 assets/Models/cube/cube.mesh（cwd-相对）。
        const std::string meshPath = r.destPath;
        assert(fs::exists(meshPath) && ".mesh 产物文件应存在");
        // .meta sidecar 同目录、同名 + ".meta"。
        const std::string metaPath = meshPath + ".meta";
        assert(fs::exists(metaPath) && ".meta sidecar 应存在");
        // 源 .obj 也应被 copy 到模型目录（ADR-008 4 件套之 copy 源）。
        assert(fs::exists((fs::path(meshPath).parent_path() / "cube.obj")) &&
               "源 .obj 应被 copy 到模型目录");

        // .meta 含 sourcePath / sourceHash 字段（粗粒度锁字段写出）。
        assert(FileContains(metaPath, "sourcePath") && ".meta 应含 sourcePath 字段");
        assert(FileContains(metaPath, "sourceHash") && ".meta 应含 sourceHash 字段");

        // MeshLoader 直接读回（不经 registry，验 .mesh 二进制本身合法）。
        AssetNS::MeshLoader loader;
        auto loadRes = loader.Load(meshPath);
        assert(loadRes.IsOk() && "MeshLoader::Load 应成功读回导入的 .mesh");
        const auto& mesh = *loadRes.Value();
        assert(!mesh.Positions().empty() && "读回 mesh 顶点数应 > 0");
        assert(!mesh.Indices().empty() && "读回 mesh 索引数应 > 0");
        // 立方体 = 12 三角形 = 36 索引（triangulate 后稳定）。
        assert(mesh.Indices().size() == 36 && "立方体应有 36 个索引（12 三角）");
        // 索引最大值 < 顶点数（合法 indexed mesh）。
        std::uint32_t maxIdx = 0;
        for (auto idx : mesh.Indices()) { if (idx > maxIdx) { maxIdx = idx; } }
        assert(static_cast<std::size_t>(maxIdx) < mesh.Positions().size() &&
               "索引应全部落在顶点数组范围内");

        std::fprintf(stdout,
                     "  [PASS] .obj headless：.mesh + .meta + 源 copy + 读回 "
                     "(vtx=%zu idx=%zu)\n",
                     mesh.Positions().size(), mesh.Indices().size());

        // ===== 2. 确定性：同一文件导两次，.mesh 字节一致 =====
        // 先记第一次产物字节，删除产物 + .meta（绕开 T5 hash 短路），重导。
        const std::vector<std::uint8_t> firstBytes = ReadAllBytes(meshPath);
        fs::remove(meshPath, ec);
        fs::remove(metaPath, ec);
        // 同时删掉源 copy 让重导走完整路径。
        fs::remove(fs::path(meshPath).parent_path() / "cube.obj", ec);

        auto registry2 = MakeImportRegistry();
        const ImportNS::ImportResult r2 =
            ImportNS::ImportObjMeshToRegistry(objPath, *registry2);
        assert(r2.status == ImportNS::ImportStatus::Success &&
               "第二次 obj 导入应 Success");
        assert(fs::exists(meshPath) && "第二次导入应重新产出 .mesh");
        const std::vector<std::uint8_t> secondBytes = ReadAllBytes(meshPath);
        assert(firstBytes == secondBytes &&
               "同一 .obj 两次 headless 导入 .mesh 字节应完全一致（确定性）");
        std::fprintf(stdout,
                     "  [PASS] 确定性：两次导入 .mesh 字节一致 (%zu bytes)\n",
                     secondBytes.size());

        // ===== 3. DispatchToRegistry 按 ext 路由（与直接调 obj seam 等价）=====
        fs::remove(meshPath, ec);
        fs::remove(metaPath, ec);
        fs::remove(fs::path(meshPath).parent_path() / "cube.obj", ec);
        auto registry3 = MakeImportRegistry();
        const ImportNS::ImportResult r3 =
            ImportNS::DispatchToRegistry(objPath, *registry3);
        assert(r3.status == ImportNS::ImportStatus::Success &&
               "DispatchToRegistry(.obj) 应 Success");
        assert(fs::exists(meshPath) && "DispatchToRegistry 应产出同款 .mesh");
        std::fprintf(stdout, "  [PASS] DispatchToRegistry 按 .obj ext 路由\n");
    }

#ifdef ORANGE_ENGINE_GLTF_FIXTURE
    // ===== 4. .gltf headless 导入（fixture 门控）=====
    {
        const std::string gltfPath = ORANGE_ENGINE_GLTF_FIXTURE;
        if (fs::exists(gltfPath))
        {
            auto registry = MakeImportRegistry();
            const ImportNS::ImportResult r =
                ImportNS::ImportGltfMeshToRegistry(gltfPath, *registry);
            assert(r.status == ImportNS::ImportStatus::Success &&
                   "gltf headless 导入应 Success");
            assert(!r.destPath.empty() && fs::exists(r.destPath) &&
                   "gltf 产物 .mesh 应存在");
            assert(fs::exists(r.destPath + ".meta") &&
                   "gltf .meta sidecar 应存在");

            AssetNS::MeshLoader loader;
            auto loadRes = loader.Load(r.destPath);
            assert(loadRes.IsOk() && "MeshLoader::Load 应读回 gltf 导出的 .mesh");
            const auto& mesh = *loadRes.Value();
            assert(!mesh.Positions().empty() && !mesh.Indices().empty() &&
                   "gltf 读回 mesh 顶点 / 索引应 > 0");
            std::fprintf(stdout,
                         "  [PASS] .gltf headless：.mesh + .meta + 读回 "
                         "(vtx=%zu idx=%zu)\n",
                         mesh.Positions().size(), mesh.Indices().size());
        }
        else
        {
            std::fprintf(stdout, "  [SKIP] .gltf fixture 不存在: %s\n",
                         gltfPath.c_str());
        }
    }
#else
    std::fprintf(stdout, "  [SKIP] .gltf 路径未编入（无 ORANGE_ENGINE_GLTF_FIXTURE）\n");
#endif

    // ===== 5. 多 material gltf headless 导入（自给 fixture，恒跑）=====
    // 程序化写一个双 material 内嵌 .gltf（不依赖外部文件），断言：
    //   * mesh 带 >= 2 段 sub-mesh
    //   * materialSlot 覆盖 [0..N-1] 连续、无空洞
    //   * sub-mesh 的 indexOffset/indexCount 之和 == 总 indexCount 且不重叠不留空
    //   * 生成 N 个 .material 文件 + ImportResult.materialPaths 对应填好
    //   * .meta 含 subMeshMaterials 段（drop 落地端据此挂 SubMeshMaterialsComponent）
    {
        const fs::path srcDir = testRoot / "multimat_src";
        fs::create_directories(srcDir, ec);
        const std::string gltfPath =
            (srcDir / "two_material.gltf").generic_string();
        WriteTwoMaterialGltf(gltfPath);

        auto registry = MakeImportRegistry();
        const ImportNS::ImportResult r =
            ImportNS::ImportGltfMeshToRegistry(gltfPath, *registry);
        assert(r.status == ImportNS::ImportStatus::Success &&
               "多 material gltf headless 导入应 Success");
        assert(!r.destPath.empty() && fs::exists(r.destPath) &&
               "多 material gltf 产物 .mesh 应存在");

        // materialPaths：两个 slot 都应有非空 .material 路径 + 文件落盘。
        assert(r.materialPaths.size() == 2 &&
               "两 material 模型应有 2 项 materialPaths");
        for (std::size_t i = 0; i < r.materialPaths.size(); ++i)
        {
            assert(!r.materialPaths[i].empty() &&
                   "每个有 material 的 slot 应填 materialPaths");
            assert(fs::exists(r.materialPaths[i]) && ".material 文件应落盘");
        }
        // slot 0 沿用 <stem>.material 历史命名。
        assert(r.materialPaths[0].find("two_material.material") != std::string::npos &&
               "slot 0 应是 <stem>.material 历史命名");

        // 读回 .mesh：sub-mesh >= 2，slot 覆盖 [0..N-1] 连续，区间无重叠无空隙。
        AssetNS::MeshLoader loader;
        auto loadRes = loader.Load(r.destPath);
        assert(loadRes.IsOk() && "MeshLoader::Load 应读回多 material .mesh");
        const auto& mesh = *loadRes.Value();
        const auto& subs = mesh.SubMeshes();
        assert(subs.size() >= 2 && "多 material mesh 应有 >= 2 段 sub-mesh");

        std::uint32_t maxSlot = 0;
        std::vector<bool> slotSeen;
        std::uint64_t coveredIndices = 0;
        std::uint32_t expectOffset = 0;
        for (const auto& s : subs)
        {
            if (s.materialSlot >= slotSeen.size())
            {
                slotSeen.resize(s.materialSlot + 1, false);
            }
            slotSeen[s.materialSlot] = true;
            if (s.materialSlot > maxSlot) { maxSlot = s.materialSlot; }
            // 按 importer 输出顺序，区间连续拼接（offset == 累计前缀）。
            assert(s.indexOffset == expectOffset &&
                   "sub-mesh indexOffset 应紧接上一段（不重叠不留空）");
            expectOffset += s.indexCount;
            coveredIndices += s.indexCount;
        }
        for (std::uint32_t sl = 0; sl <= maxSlot; ++sl)
        {
            assert(sl < slotSeen.size() && slotSeen[sl] &&
                   "materialSlot 应覆盖 [0..maxSlot] 连续，无空洞");
        }
        assert(coveredIndices == mesh.Indices().size() &&
               "sub-mesh indexCount 之和应等于总 indexCount");

        // .meta 应含 subMeshMaterials 段（落地端 drop 时回读挂多材质组件）。
        assert(FileContains(r.destPath + ".meta", "subMeshMaterials") &&
               ".meta 应含 subMeshMaterials 段");

        std::fprintf(stdout,
                     "  [PASS] 多 material gltf：sub-mesh=%zu material=%zu "
                     "idx 覆盖=%llu/%zu\n",
                     subs.size(), r.materialPaths.size(),
                     static_cast<unsigned long long>(coveredIndices),
                     mesh.Indices().size());
    }

    // ===== 6. tangent fallback 端到端：无 UV .obj → Lengyel 兜底有效 TBN =====
    // GAP-2026-05-25 gap ②。缺 UV 时 importer 跳过 MikkTSpace（GenerateMikkTSpace
    // Tangents 前置 uvs/normals 非空不满足）→ 写 .mesh（hasTangents=0 + 全零 UV
    // 占位 + normal）→ Load 端 ComputeTangentsFromTriangles（Lengyel）对全零 UV
    // 每三角 det=0 跳过、每顶点落 ArbitraryTangent → HasTangents()=true 且每条
    // 切线单位长 + 与对应 normal 正交 + w=±1（TBN 非奇异，shader 不会 0×NaN）。
    {
        const fs::path srcDir = testRoot / "src_nouv";
        fs::create_directories(srcDir, ec);
        const std::string objPath = (srcDir / "cube_nouv.obj").generic_string();
        WriteCubeObjNoUV(objPath);

        auto registry = MakeImportRegistry();
        const ImportNS::ImportResult r =
            ImportNS::ImportObjMeshToRegistry(objPath, *registry);
        assert(r.status == ImportNS::ImportStatus::Success &&
               "无 UV .obj 导入应 Success（importer 不得因缺 UV 崩溃 / 失败）");
        assert(!r.destPath.empty() && "destPath 应非空");

        AssetNS::MeshLoader loader;
        auto loadRes = loader.Load(r.destPath);
        assert(loadRes.IsOk() && "无 UV mesh 应能 Load 读回");
        const auto& mesh = *loadRes.Value();
        assert(!mesh.Positions().empty() && "读回顶点 > 0");
        assert(mesh.Indices().size() == 36 && "立方体 36 索引");

        // 关键：normal 在（OBJ 自带 / 缺则 loader 补算）；tangent 经 Lengyel
        // 兜底落地（HasTangents 为真）。
        assert(mesh.HasNormals() && "无 UV mesh 应仍有 normal");
        assert(mesh.HasTangents() &&
               "缺 UV → Load 端 Lengyel 兜底应产出 tangent（HasTangents=true）");
        assert(mesh.Tangents().size() == mesh.Positions().size() &&
               "tangent 与顶点一一对应");

        // 逐顶点验 TBN 有效性：切线单位长 + 与 normal 正交 + w=±1（非 NaN）。
        const auto& tans = mesh.Tangents();
        const auto& norms = mesh.Normals();
        for (std::size_t i = 0; i < tans.size(); ++i)
        {
            const auto& t = tans[i];
            const float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
            assert(std::isfinite(len) && "切线分量必须有限（无 NaN/Inf）");
            assert(std::fabs(len - 1.0f) < 1e-3f && "Lengyel 兜底切线应单位长");
            assert(std::fabs(std::fabs(t.w) - 1.0f) < 1e-3f && "handedness w=±1");
            const auto& n = norms[i];
            const float dotTN = t.x * n.x + t.y * n.y + t.z * n.z;
            assert(std::fabs(dotTN) < 1e-2f &&
                   "切线应与法线正交（Gram-Schmidt 后 |dot(T,N)|≈0）");
        }
        std::fprintf(stdout,
                     "  [PASS] tangent fallback：无 UV .obj → Lengyel 兜底有效 TBN "
                     "(vtx=%zu, 全切线单位长+正交+w=±1)\n",
                     mesh.Positions().size());
    }

    // ===== 7. 单 material gltf：导入侧也把材质写进 .meta（供 drop 自动应用）=====
    // 之前单 material 的 .meta subMeshMaterials 留空 → drop 时材质不自动应用
    // （单材质模型拖进场景是默认材质，对齐不上 Lumix/Unity）。本段验证导入侧
    // 修复：单 material 也写 subMeshMaterials（1 条）+ 1 个 .material；mesh 仍
    // 无 sub-mesh（HasSubMeshes()==false，drop 端据此走"设 Renderable.material
    // Instance"而非挂 SubMeshMaterialsComponent）。
    {
        const fs::path srcDir = testRoot / "src_solo";
        fs::create_directories(srcDir, ec);
        const std::string gltfPath = (srcDir / "solo_material.gltf").generic_string();
        WriteSingleMaterialGltf(gltfPath);

        auto registry = MakeImportRegistry();
        const ImportNS::ImportResult r =
            ImportNS::ImportGltfMeshToRegistry(gltfPath, *registry);
        assert(r.status == ImportNS::ImportStatus::Success &&
               "单 material gltf 导入应 Success");
        assert(fs::exists(r.destPath) && ".mesh 产物应存在");

        // .meta 现在含 subMeshMaterials（单材质也写）。
        const std::string metaPath = r.destPath + ".meta";
        assert(fs::exists(metaPath) && ".meta 应存在");
        assert(FileContains(metaPath, "subMeshMaterials") &&
               "单 material 的 .meta 也应含 subMeshMaterials 段（drop 自动应用材质）");
        assert(FileContains(metaPath, "solo_material.material") &&
               ".meta subMeshMaterials 应引用 slot 0 的 .material 路径");

        // slot 0 .material（单材质 = <stem>.material）落盘。
        const fs::path modelDir = fs::path(r.destPath).parent_path();
        assert(fs::exists(modelDir / "solo_material.material") &&
               "单 material slot 0 .material 应落盘");

        // mesh 本身无 sub-mesh（单材质退化路径）。
        AssetNS::MeshLoader loader;
        auto loadRes = loader.Load(r.destPath);
        assert(loadRes.IsOk() && "单 material .mesh 应能 Load");
        assert(!loadRes.Value()->HasSubMeshes() &&
               "单 material → 无 sub-mesh（HasSubMeshes()==false）");

        // ImportResult.materialPaths 恰 1 条且非空。
        assert(r.materialPaths.size() == 1 && !r.materialPaths[0].empty() &&
               "单 material → materialPaths 恰 1 条非空");
        std::fprintf(stdout,
                     "  [PASS] 单 material gltf：.meta 写 subMeshMaterials(1) + "
                     "1 .material + mesh 无 sub-mesh（drop 自动应用单材质地基）\n");
    }

    // 清理临时目录（切回上层先，避免删 cwd）。
    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(testRoot, ec);

    std::fprintf(stdout, "[HeadlessMeshImportTest] all tests passed.\n");
    return 0;
}
