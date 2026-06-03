// FBX 静态 mesh + 材质导入端到端测试（headless）—— 锁住 FbxImporter 的整条链：
// .fbx → OpenFBX 解析 → 坐标系转换（Z-up/cm → 引擎 Y-up/米）→ 合并 mesh +
// 多 material 拆 sub-mesh → MikkTSpace 切线 → .mesh + per-material .material +
// .meta sidecar → 可被 MeshLoader 读回。
//
// 为什么能 headless：走 ImportDispatcher 的 registry-only seam
// （ImportFbxMeshToRegistry / DispatchToRegistry），只依赖
// Orange::Engine::Asset::AssetRegistry&，不出现 EditorHost / Vulkan / ImGui /
// GLFW。直接编译真实 importer 源（FbxImporter / ImportDispatcher / 其余 importer
// 复用件 + MaterialFileIO）+ vendor（OpenFBX ofbx.cpp / libdeflate.c /
// mikktspace.c），只链 OrangeEngine::orange_engine。
//
// fixture：tests/fixtures/cube_two_material.fbx（由 gen_cube_fbx.py 经 Blender
// headless 生成，自有几何可自由分发，commit 进仓）。已知几何：单位 cube（8 顶点 /
// 12 三角）+ 2 材质（MatRed 红 slot 0 / MatGreen 绿 slot 1）。**Z-up FBX**（故意，
// 验 importer 轴转换）。fixture 路径经 ORANGE_ENGINE_FBX_FIXTURE 注入；CMake 用
// if(EXISTS) 门控（干净 checkout 缺 fixture 时跳过该段，仍测 importer 可链 + ext
// 路由）。

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
    // 隔离临时 cwd —— importer 产物落 cwd-相对 assets/Models/<stem>/。
    const fs::path testRoot =
        fs::temp_directory_path() / "orange_fbx_import_test";
    std::error_code ec;
    fs::remove_all(testRoot, ec);
    fs::create_directories(testRoot, ec);
    assert(!ec && "建临时测试根目录应成功");
    fs::current_path(testRoot, ec);
    assert(!ec && "切 cwd 到临时目录应成功");

    std::fprintf(stdout, "[FbxImportTest] cwd=%s\n",
                 fs::current_path().string().c_str());

    // ===== 1. ClassifyByExt：.fbx / .FBX 大小写都认 → FbxMesh =====
    {
        assert(ImportNS::ClassifyByExt("fbx") == ImportNS::ImportKind::FbxMesh &&
               "ext 'fbx' 应分类为 FbxMesh");
        assert(ImportNS::ClassifyByExt(".FBX") == ImportNS::ImportKind::FbxMesh &&
               "ext '.FBX' 大写也应分类为 FbxMesh");
        assert(ImportNS::ClassifyByExt(".Fbx") == ImportNS::ImportKind::FbxMesh &&
               "ext '.Fbx' 混合大小写也应分类为 FbxMesh");
        std::fprintf(stdout, "  [PASS] ClassifyByExt: .fbx/.FBX/.Fbx → FbxMesh\n");
    }

    // ===== 2. 缺失文件 → SourceReadFailed（不崩）=====
    {
        auto registry = MakeImportRegistry();
        const ImportNS::ImportResult r = ImportNS::ImportFbxMeshToRegistry(
            (testRoot / "does_not_exist.fbx").generic_string(), *registry);
        assert(r.status == ImportNS::ImportStatus::SourceReadFailed &&
               "不存在的 .fbx 应返回 SourceReadFailed（不崩）");
        std::fprintf(stdout, "  [PASS] 缺失 .fbx → SourceReadFailed\n");
    }

#ifdef ORANGE_ENGINE_FBX_FIXTURE
    const std::string fixturePath = ORANGE_ENGINE_FBX_FIXTURE;
    if (!fs::exists(fixturePath))
    {
        std::fprintf(stdout, "  [SKIP] FBX fixture 不存在: %s\n",
                     fixturePath.c_str());
        std::fprintf(stdout, "[FbxImportTest] (fixture-gated tests skipped)\n");
        fs::current_path(fs::temp_directory_path(), ec);
        fs::remove_all(testRoot, ec);
        return 0;
    }

    // ===== 3. 导入 cube_two_material.fbx → .mesh + 2 .material + .meta =====
    {
        auto registry = MakeImportRegistry();
        const ImportNS::ImportResult r =
            ImportNS::ImportFbxMeshToRegistry(fixturePath, *registry);
        assert(r.status == ImportNS::ImportStatus::Success &&
               "FBX headless 导入应 Success");
        assert(!r.destPath.empty() && fs::exists(r.destPath) &&
               ".mesh 产物应存在");
        assert(fs::exists(r.destPath + ".meta") && ".meta sidecar 应存在");
        // 源 .fbx 应被 copy 到模型目录（ADR-008 4 件套之 copy 源）。
        const fs::path modelDir = fs::path(r.destPath).parent_path();
        assert(fs::exists(modelDir / "cube_two_material.fbx") &&
               "源 .fbx 应被 copy 到模型目录");
        assert(FileContains(r.destPath + ".meta", "sourcePath") &&
               ".meta 应含 sourcePath");
        assert(FileContains(r.destPath + ".meta", "sourceHash") &&
               ".meta 应含 sourceHash");

        // ----- 几何：读回 .mesh，验顶点 / 索引 / AABB（轴 + 单位缩放）-----
        AssetNS::MeshLoader loader;
        auto loadRes = loader.Load(r.destPath);
        assert(loadRes.IsOk() && "MeshLoader::Load 应读回导入的 .mesh");
        const auto& mesh = *loadRes.Value();
        assert(!mesh.Positions().empty() && "读回 mesh 顶点数应 > 0");
        assert(!mesh.Indices().empty() && "读回 mesh 索引数应 > 0");
        // cube = 12 三角 = 36 索引（FbxImporter unindexed 展开 → MikkTSpace re-weld
        // 后顶点数 24/cube；索引仍 36）。
        assert(mesh.Indices().size() == 36 && "cube 应有 36 个索引（12 三角）");
        // 索引合法：最大索引 < 顶点数。
        std::uint32_t maxIdx = 0;
        for (auto idx : mesh.Indices()) { if (idx > maxIdx) { maxIdx = idx; } }
        assert(static_cast<std::size_t>(maxIdx) < mesh.Positions().size() &&
               "索引应全部落在顶点数组范围内");

        // AABB：单位 cube 在引擎里应是 ±0.5（米）。这同时验证两件事：
        //   * 单位：fixture 顶点已是烘好的米（Blender 导出 ±0.5 + UnitScaleFactor=1），
        //     importer **信任已烘单位**（unitScale=1，不按 UnitScaleFactor 折算，见
        //     FbxImporter AxisConverter）→ 保持 ±0.5。排除量级错（轴塌缩成某轴范围 0 /
        //     误缩放）。注：真正未烘的 cm 文件会偏大 100× 是已知 MVP 限制。
        //   * 轴转换：Z-up cube 转 Y-up 后仍是 ±0.5 立方（对称，各轴范围一致）。
        float minB[3] = {1e9f, 1e9f, 1e9f};
        float maxB[3] = {-1e9f, -1e9f, -1e9f};
        for (const auto& p : mesh.Positions())
        {
            minB[0] = std::min(minB[0], p.x); maxB[0] = std::max(maxB[0], p.x);
            minB[1] = std::min(minB[1], p.y); maxB[1] = std::max(maxB[1], p.y);
            minB[2] = std::min(minB[2], p.z); maxB[2] = std::max(maxB[2], p.z);
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            // ±0.5 米 ± 容差（mesh_smooth_type / 浮点误差）。关键是量级在 [0.4,0.6]，
            // 排除 cm 未转（±50）/ 轴塌缩（某轴范围 0）。
            assert(std::fabs(minB[axis] - (-0.5f)) < 0.05f &&
                   "AABB min 各轴应 ≈ -0.5 米（单位缩放 + 轴转换正确）");
            assert(std::fabs(maxB[axis] - (0.5f)) < 0.05f &&
                   "AABB max 各轴应 ≈ +0.5 米（单位缩放 + 轴转换正确）");
        }
        std::fprintf(stdout,
                     "  [PASS] 几何：vtx=%zu idx=%zu AABB=[%.3f,%.3f]³（轴+单位正确）\n",
                     mesh.Positions().size(), mesh.Indices().size(),
                     minB[0], maxB[0]);

        // ----- 法线 / 切线：DCC 导出带 normal，UV 在 → MikkTSpace 切线 -----
        assert(mesh.HasNormals() && "FBX 导出带 normal，读回应有 normal");
        // 法线应单位长（轴转换里重新单位化）。
        for (const auto& n : mesh.Normals())
        {
            const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            assert(std::isfinite(len) && "法线分量应有限");
            assert(std::fabs(len - 1.0f) < 1e-2f && "法线应单位长");
        }

        // ----- 轴转换正确性（load-bearing）：fixture 里绿材质（slot 1）贴在
        // Blender Z-up 的顶/底两面（法线 Z=±1）。经 Z-up→Y-up 旋转后，这两面在
        // 引擎里法线必须落 ±Y。对称 cube 的 AABB 旋转不变，无法验旋转；本断言用
        // "绿 sub-mesh 的法线全沿引擎 ±Y" 锁住旋转真发生且方向对。
        // 绿 slot 的 sub-mesh 区间从 .mesh sub-mesh 段拿。
        {
            const auto& subsForAxis = mesh.SubMeshes();
            std::uint32_t greenOffset = 0, greenCount = 0;
            bool foundGreen = false;
            for (const auto& s : subsForAxis)
            {
                if (s.materialSlot == 1)
                {
                    greenOffset = s.indexOffset;
                    greenCount  = s.indexCount;
                    foundGreen  = true;
                    break;
                }
            }
            assert(foundGreen && "应能定位绿材质（slot 1）的 sub-mesh 段");
            const auto& idxs  = mesh.Indices();
            const auto& norms = mesh.Normals();
            for (std::uint32_t i = greenOffset; i < greenOffset + greenCount; ++i)
            {
                const std::uint32_t vi = idxs[i];
                const auto& n = norms[vi];
                // 引擎 +Y/-Y：|y| 主导，x/z 近 0。证 Blender Z-up 顶/底面 → 引擎 ±Y。
                assert(std::fabs(n.y) > 0.9f &&
                       "绿面（Blender Z-up 顶/底）法线在引擎应沿 ±Y（轴转换正确）");
                assert(std::fabs(n.x) < 0.2f && std::fabs(n.z) < 0.2f &&
                       "绿面法线 x/z 分量应近 0（轴转换无歪斜）");
            }
        }

        // ----- 多 material → sub-mesh slot -----
        // 2 材质 → 2 个 .material + mesh 拆 2 段 sub-mesh（连续 / 覆盖全索引）。
        assert(r.materialPaths.size() == 2 &&
               "2 材质 cube 应有 2 项 materialPaths");
        for (const auto& mp : r.materialPaths)
        {
            assert(!mp.empty() && fs::exists(mp) &&
                   "每个 slot 的 .material 文件应落盘");
        }
        // slot 0 = <stem>.material（历史命名）。
        assert(r.materialPaths[0].find("cube_two_material.material") !=
                   std::string::npos &&
               "slot 0 应是 <stem>.material");

        assert(mesh.HasSubMeshes() && "2 材质 → HasSubMeshes()==true");
        const auto& subs = mesh.SubMeshes();
        assert(subs.size() >= 2 && "应有 >= 2 段 sub-mesh");
        std::uint64_t covered = 0;
        std::uint32_t expectOffset = 0;
        std::uint32_t maxSlot = 0;
        std::vector<bool> slotSeen;
        for (const auto& s : subs)
        {
            assert(s.indexOffset == expectOffset &&
                   "sub-mesh indexOffset 应连续（不重叠不留空）");
            expectOffset += s.indexCount;
            covered += s.indexCount;
            if (s.materialSlot >= slotSeen.size())
            {
                slotSeen.resize(s.materialSlot + 1, false);
            }
            slotSeen[s.materialSlot] = true;
            if (s.materialSlot > maxSlot) { maxSlot = s.materialSlot; }
        }
        for (std::uint32_t sl = 0; sl <= maxSlot; ++sl)
        {
            assert(sl < slotSeen.size() && slotSeen[sl] &&
                   "materialSlot 应覆盖 [0..maxSlot] 连续无空洞");
        }
        assert(covered == mesh.Indices().size() &&
               "sub-mesh indexCount 之和应等于总索引数");

        // .meta 含 subMeshMaterials（drop 落地端据此挂材质）。
        assert(FileContains(r.destPath + ".meta", "subMeshMaterials") &&
               ".meta 应含 subMeshMaterials 段");

        // ----- 材质 baseColor 值：MatRed slot 0 应红 / MatGreen slot 1 应绿 -----
        // .material 是 pbr 模板 + uBaseColor。粗粒度断言：slot 0 含红分量高的
        // baseColor（0.8…），slot 1 含绿分量高（0.7…）。直接文本查关键数值串
        // （MaterialFileIO 写 vec4 为 JSON 数组）。FBX 导出可能把 diffuse 略改，
        // 故只查 "0.8"（红）/ "0.7"（绿）出现在对应 .material 文件即可。
        const bool redOk =
            FileContains(r.materialPaths[0], "uBaseColor") &&
            (FileContains(r.materialPaths[0], "0.8") ||
             FileContains(r.materialPaths[0], "0.79") ||
             FileContains(r.materialPaths[0], "0.80"));
        const bool greenOk =
            FileContains(r.materialPaths[1], "uBaseColor") &&
            (FileContains(r.materialPaths[1], "0.7") ||
             FileContains(r.materialPaths[1], "0.69") ||
             FileContains(r.materialPaths[1], "0.70"));
        assert(redOk &&
               "slot 0 (.material) 应是 pbr + uBaseColor 含红基色 (0.8)");
        assert(greenOk &&
               "slot 1 (.material) 应是 pbr + uBaseColor 含绿基色 (0.7)");
        assert(FileContains(r.materialPaths[0], "\"pbr\"") &&
               ".material templateName 应为 pbr");

        std::fprintf(stdout,
                     "  [PASS] 多材质：2 .material(slot0 红/slot1 绿) + %zu sub-mesh 段"
                     "（连续+覆盖全索引）+ .meta subMeshMaterials\n",
                     subs.size());
    }

    // ===== 4. DispatchToRegistry 按 .fbx ext 路由（与直接 seam 等价）=====
    {
        // 删上一轮产物绕 hash 短路。
        const std::string stemMesh =
            (fs::path("assets/Models") / "cube_two_material" /
             "cube_two_material.mesh").generic_string();
        fs::remove(stemMesh, ec);
        fs::remove(stemMesh + ".meta", ec);
        fs::remove(fs::path(stemMesh).parent_path() / "cube_two_material.fbx", ec);

        auto registry = MakeImportRegistry();
        const ImportNS::ImportResult r =
            ImportNS::DispatchToRegistry(fixturePath, *registry);
        assert(r.status == ImportNS::ImportStatus::Success &&
               "DispatchToRegistry(.fbx) 应 Success");
        assert(fs::exists(r.destPath) && "DispatchToRegistry 应产出 .mesh");
        std::fprintf(stdout, "  [PASS] DispatchToRegistry 按 .fbx ext 路由\n");
    }

    // ===== 5. 确定性：同一 .fbx 导两次 .mesh 字节一致 =====
    {
        const std::string meshPath =
            (fs::path("assets/Models") / "cube_two_material" /
             "cube_two_material.mesh").generic_string();
        assert(fs::exists(meshPath) && "上一轮 .mesh 应在位");
        std::ifstream f1(meshPath, std::ios::binary);
        const std::vector<std::uint8_t> firstBytes(
            (std::istreambuf_iterator<char>(f1)),
            std::istreambuf_iterator<char>());
        f1.close();

        fs::remove(meshPath, ec);
        fs::remove(meshPath + ".meta", ec);
        fs::remove(fs::path(meshPath).parent_path() / "cube_two_material.fbx", ec);

        auto registry = MakeImportRegistry();
        const ImportNS::ImportResult r =
            ImportNS::ImportFbxMeshToRegistry(fixturePath, *registry);
        assert(r.status == ImportNS::ImportStatus::Success &&
               "第二次 FBX 导入应 Success");
        std::ifstream f2(meshPath, std::ios::binary);
        const std::vector<std::uint8_t> secondBytes(
            (std::istreambuf_iterator<char>(f2)),
            std::istreambuf_iterator<char>());
        f2.close();
        assert(firstBytes == secondBytes &&
               "同一 .fbx 两次导入 .mesh 字节应完全一致（确定性）");
        std::fprintf(stdout,
                     "  [PASS] 确定性：两次导入 .mesh 字节一致 (%zu bytes)\n",
                     secondBytes.size());
    }
#else
    std::fprintf(stdout,
                 "  [SKIP] FBX fixture 未编入（无 ORANGE_ENGINE_FBX_FIXTURE）\n");
#endif

    fs::current_path(fs::temp_directory_path(), ec);
    fs::remove_all(testRoot, ec);

    std::fprintf(stdout, "[FbxImportTest] all tests passed.\n");
    return 0;
}
