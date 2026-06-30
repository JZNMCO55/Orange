// F1 回归 —— 经 set_field 设 Renderable.materialInstance 不视觉生效的根因守卫。
//
// F1（2026-06-30 自动化 dogfood 发现）：经 OE-MCP set_field 设 materialInstance
// 为一个 .material path 时材质不生效。根因：schema 的 materialSet 只在
// namedMaterialInstances 表里查、找不到就静默 no-op（materialInstance 不变，
// 但 set_field 仍返回 ok）。GUI writePath 早有 EnsureMaterialInstance 前置 lazy
// 注册兜底，MCP HandleSetField 漏了。修复把该前置抽成 PrepareAssetRefWrite，
// GUI 与 MCP 共用；其 Material 分支调 EnsureMaterialInstance 先把不在表的
// .material lazy 注册进 namedMaterialInstances，使随后 materialSet 查表命中。
//
// 本测试锁住 EnsureMaterialInstance 的 lazy 注册契约。它走 EditorAssetContext&
// 重载（EnsureMaterialInstance 只用 host.assets，下沉到此可独立测试的 seam）：
// EditorHost 聚合 ThumbnailService / AudioEngine（Vulkan / ImGui）无法 headless
// 链接（见 GltfMaterialImportTest 注释），而 EditorAssetContext 是纯资产数据，
// headless 可构造。
//
// 两条断言：
//   A（环境无关）：templateName 未注册的 .material → EnsureMaterialInstance 返回
//     nullptr 且不写表（"ensure 失败不静默成功"——修复点要求 MCP 显式报错）。
//   B（probe-gated）：当 pbr 模板可 CreateInstance 时，对一个不在表的 pbr
//     .material → EnsureMaterialInstance 返回非空 + path 进 namedMaterialInstances
//     （= 修复后 materialSet 查表命中，F1 正向）。模板/shader 资源缺失的环境下
//     probe 失败则跳过 B（不阻塞），A 仍守住分发逻辑。
//
// 链接面：BuiltinAssets.cpp + MaterialFileIO.cpp + ShaderTemplateMetaIO.cpp +
// orange_engine —— 均无 import-impl / vulkan，headless 可链。

#include "BuiltinAssets.h"   // EnsureMaterialInstance(EditorAssetContext&)
#include "MaterialFileIO.h"  // MaterialFileData / WriteMaterialFile

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/render/MaterialSystem.h>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Render::MaterialSystem;
using Orange::Editor::Material::MaterialFileData;
using Orange::Editor::Material::WriteMaterialFile;

namespace
{

// 写一份最小 .material（指定 templateName，无 uniform/texture override）到磁盘。
std::string WriteFixtureMaterial(const std::filesystem::path& path,
                                 const std::string& templateName)
{
    MaterialFileData data;
    data.templateName = templateName;
    const std::string p = path.string();
    const bool wrote = WriteMaterialFile(p, data);
    assert(wrote && "写 fixture .material 失败");
    return p;
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[MaterialEnsureLazyRegisterTest] running\n");

    // headless EditorAssetContext（纯资产数据，无 EditorHost / Vulkan / ImGui）。
    EditorAssetContext assets;
    assets.pAssets = std::make_unique<AssetRegistry>();
    {
        auto reg =
            assets.pAssets->RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        assert(reg.IsOk() && "注册 ShaderLoader 失败");
    }
    assets.pMaterials = std::make_unique<MaterialSystem>(*assets.pAssets);
    const bool builtinsOk = assets.pMaterials->RegisterBuiltins().IsOk();

    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path();

    // ---- 断言 A：未注册模板 → ensure 失败、不写表（环境无关）----------------
    {
        const std::string badPath =
            WriteFixtureMaterial(tmpDir / "orange_f1_unknown_template.material",
                                 "__orange_nonexistent_template__");
        assert(assets.namedMaterialInstances.find(badPath)
                   == assets.namedMaterialInstances.end());

        auto* inst = EnsureMaterialInstance(assets, badPath);
        assert(inst == nullptr
               && "未注册模板应 ensure 失败返回 nullptr（不静默成功）");
        assert(assets.namedMaterialInstances.find(badPath)
                   == assets.namedMaterialInstances.end()
               && "ensure 失败不应往 namedMaterialInstances 写入");
        std::filesystem::remove(tmpDir / "orange_f1_unknown_template.material");
        std::fprintf(stdout, "  [PASS] A: 未注册模板 ensure 失败且不写表\n");
    }

    // ---- 断言 B：pbr 可用时，不在表的 .material → ensure 成功 + 进表 ---------
    bool pbrUsable = false;
    if (builtinsOk)
    {
        // probe：pbr 模板能否 CreateInstance（缺 shader 资源的环境下会失败）。
        pbrUsable = (assets.pMaterials->CreateInstance("pbr") != nullptr);
    }
    if (!pbrUsable)
    {
        std::fprintf(stderr,
                     "  [SKIP] B: pbr 模板不可 CreateInstance（builtinsOk=%d）——"
                     "本环境缺 shader 资源，跳过正向路径（A 已守住分发）\n",
                     builtinsOk ? 1 : 0);
        std::fprintf(stdout, "[MaterialEnsureLazyRegisterTest] passed (B skipped).\n");
        return 0;
    }

    {
        const std::filesystem::path matFs =
            tmpDir / "orange_f1_pbr_lazy_register.material";
        const std::string matPath = WriteFixtureMaterial(matFs, "pbr");

        // F1 前提：path 不在表 → 修复前 materialSet 会 miss（材质不生效）。
        assert(assets.namedMaterialInstances.find(matPath)
                   == assets.namedMaterialInstances.end()
               && "前提:fixture .material 不应预先在表里");

        // 修复核心：EnsureMaterialInstance lazy create + 注册。
        auto* inst = EnsureMaterialInstance(assets, matPath);
        assert(inst != nullptr
               && "EnsureMaterialInstance 应对不在表的 pbr .material lazy create 成功");

        // F1 修复后：path 进表，materialSet 现在能查到（非 nullptr）。
        auto it = assets.namedMaterialInstances.find(matPath);
        assert(it != assets.namedMaterialInstances.end() && it->second == inst
               && "F1 回归:ensure 后 .material 应在 namedMaterialInstances 使 "
                  "materialSet 命中");

        std::filesystem::remove(matFs);
        std::fprintf(stdout, "  [PASS] B: 不在表的 pbr .material ensure 后进表\n");
    }

    std::fprintf(stdout, "[MaterialEnsureLazyRegisterTest] all checks passed.\n");
    return 0;
}
