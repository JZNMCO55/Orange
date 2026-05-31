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

    // 清理临时目录（切回上层先，避免删 cwd）。
    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(testRoot, ec);

    std::fprintf(stdout, "[HeadlessMeshImportTest] all tests passed.\n");
    return 0;
}
