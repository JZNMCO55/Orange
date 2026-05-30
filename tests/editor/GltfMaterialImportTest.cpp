// glTF material 导入端到端测试 —— 锁住 "glTF material factor / 贴图 →
// .material 字段" 这条链（GAP-2026-05-25 G3 验证 + AO factor 修复回归守卫）。
//
// 为什么不直接调 RunGltfImport：它需要 EditorHost&，后者聚合 AudioEngine /
// ThumbnailService（Vulkan / ImGui）+ 整套 sub-context，无法 headless 链接。
// 故走 GltfMaterialParse.{h,cpp} 抽出的可独立测试 seam：
//   cgltf 解析 Avocado.gltf → ExtractGltfMaterial（cgltf_material → GltfMatInfo）
//   → BuildMaterialFileData（GltfMatInfo → MaterialFileData）。
// 这两个函数就是 RunGltfImport 真实 import 路径用到的同一份逻辑——测试覆盖
// 的就是真实链，只是把"贴图源路径 → 落盘 path"的 resolver 换成 identity
// （headless 不真 copy 贴图）。
//
// fixture（gltf_test/Avocado/Avocado.gltf）是 session 内下载的未跟踪资产，
// 故本测试由 CMake `if(EXISTS)` 门控——fixture 在就编 + 跑，干净 checkout
// 自动跳过（不阻塞 CI）。Avocado 含 baseColor + metalRough + normal 三贴图，
// 无 AO 贴图、无 baseColorFactor/metallic/roughness 显式声明（取 glTF 默认值）。
//
// 覆盖：
//   1. ExtractGltfMaterial：present + factor 默认值 + 三贴图源路径解析 OK +
//      无 occlusion texture 时 occlusionStrength = 中性 1.0
//   2. BuildMaterialFileData：templateName=pbr + uBaseColor + uMRA + binding
//      0/1/2 贴图槽（无 binding 3 因 Avocado 无 AO 贴图）+ AO 位（uMRA.z）=1.0
//      （顺带验证任务 A 的 AO 修复不破坏"无 AO 贴图"的中性情形）
//   3. .material 文件 round-trip（WriteMaterialFile → ReadMaterialFile）锁住
//      落盘字段
//   4. 合成 occlusionStrength（不依赖 fixture 有 AO 贴图）：手造一个带
//      occlusionStrength=0.5 的 GltfMatInfo，验证 BuildMaterialFileData 把它
//      填进 uMRA.z（独立锁住任务 A 的 AO factor 修复——之前写死 1.0 的 bug）

#include "GltfMaterialParse.h"  // include path 由 CMake 加 tools/OrangeEditor/import
#include "MaterialFileIO.h"     // include path 由 CMake 加 tools/OrangeEditor

// cgltf 单 header IMPLEMENTATION 在本测试 TU expand（与 MikkTSpaceRealModelTest
// 同款）；GltfMaterialParse.cpp 只取声明不 expand，故两份不冲突。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4244)
#  pragma warning(disable: 4267)
#  pragma warning(disable: 4505)
#  pragma warning(disable: 4996)
#  pragma warning(disable: 4100)
#  pragma warning(disable: 4456)
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <orange/engine/render/MaterialTypes.h>

#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#ifndef ORANGE_ENGINE_GLTF_FIXTURE
#  error "ORANGE_ENGINE_GLTF_FIXTURE 必须由 CMake 注入 .gltf 路径"
#endif

namespace ImportNS = ::Orange::Editor::Import;
namespace MatIO    = ::Orange::Editor::Material;
namespace Render   = ::Orange::Engine::Render;

namespace
{

bool FloatEq(float a, float b) noexcept { return std::fabs(a - b) < 1e-5f; }

// 在 MaterialFileData 里按 name 找 uniform override。
const MatIO::UniformOverrideValue* FindUniform(const MatIO::MaterialFileData& d,
                                               const std::string& name)
{
    for (const auto& u : d.uniforms)
    {
        if (u.name == name) { return &u; }
    }
    return nullptr;
}

// 在 MaterialFileData 里按 binding 找 texture 槽。
const MatIO::TextureOverrideEntry* FindTexture(const MatIO::MaterialFileData& d,
                                               std::uint32_t binding)
{
    for (const auto& t : d.textures)
    {
        if (t.binding == binding) { return &t; }
    }
    return nullptr;
}

std::string TempPath(const char* name)
{
    auto p = std::filesystem::temp_directory_path()
           / "orange_gltf_material_import_test" / name;
    std::filesystem::create_directories(p.parent_path());
    return p.string();
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[GltfMaterialImportTest] fixture=%s\n", ORANGE_ENGINE_GLTF_FIXTURE);

    // ---- 解析 fixture，取第一个带 material 的 triangle primitive 的 material ----
    cgltf_options options{};
    cgltf_data*   data = nullptr;
    if (cgltf_parse_file(&options, ORANGE_ENGINE_GLTF_FIXTURE, &data) != cgltf_result_success)
    {
        std::fprintf(stderr, "  cgltf_parse_file failed\n");
        return 1;
    }
    // material 的 image uri 不需要 load_buffers 就能拿到（external uri），但 Avocado
    // 的几何在 .bin 里——这里只关心 material，不读几何，故跳过 load_buffers。

    const cgltf_material* firstMat = nullptr;
    for (cgltf_size mi = 0; mi < data->meshes_count && firstMat == nullptr; ++mi)
    {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type == cgltf_primitive_type_triangles && prim.material != nullptr)
            {
                firstMat = prim.material;
                break;
            }
        }
    }
    assert(firstMat != nullptr && "Avocado fixture 应有带 material 的 triangle primitive");

    const std::filesystem::path gltfDir =
        std::filesystem::path(ORANGE_ENGINE_GLTF_FIXTURE).parent_path();

    // ===== 1. ExtractGltfMaterial =====
    ImportNS::GltfMatInfo info = ImportNS::ExtractGltfMaterial(firstMat, gltfDir);
    assert(info.present && "解析出的 material 应 present");

    // Avocado 未声明 baseColorFactor / metallic / roughness → cgltf 默认值
    // （base=[1,1,1,1]，metallic=1，roughness=1）。
    assert(FloatEq(info.baseColor[0], 1.0f) && FloatEq(info.baseColor[1], 1.0f)
        && FloatEq(info.baseColor[2], 1.0f) && FloatEq(info.baseColor[3], 1.0f)
        && "baseColorFactor 默认 [1,1,1,1]");
    assert(FloatEq(info.metallic, 1.0f)  && "metallic 默认 1.0");
    assert(FloatEq(info.roughness, 1.0f) && "roughness 默认 1.0");

    // 三贴图源路径应解析成功（文件真实存在于 fixture 目录）。
    assert(!info.baseColorSrc.empty()  && "baseColor 贴图源路径应解析 OK");
    assert(!info.normalSrc.empty()     && "normal 贴图源路径应解析 OK");
    assert(!info.metalRoughSrc.empty() && "metalRough 贴图源路径应解析 OK");
    assert(info.aoSrc.empty()          && "Avocado 无 AO 贴图 → aoSrc 应空");
    // 解析出的路径应指向 fixture 目录下实际存在的文件。
    assert(std::filesystem::exists(info.baseColorSrc)  && "baseColor 源文件存在");
    assert(std::filesystem::exists(info.normalSrc)     && "normal 源文件存在");
    assert(std::filesystem::exists(info.metalRoughSrc) && "metalRough 源文件存在");

    // 任务 A 回归：无 occlusion texture 时 occlusionStrength 必须是中性 1.0
    // （而非 cgltf zero-init 的 0.0——那会把 AO 压全黑）。
    assert(FloatEq(info.occlusionStrength, 1.0f)
           && "无 occlusion texture → occlusionStrength 中性 1.0");
    std::fprintf(stdout, "  [PASS] ExtractGltfMaterial：factor 默认值 + 三贴图源 + 无 AO 中性 1.0\n");

    // ===== 2. BuildMaterialFileData（identity resolver：源路径直接当落盘 path）=====
    auto identity = [](const std::string& s) { return s; };
    MatIO::MaterialFileData mdata = ImportNS::BuildMaterialFileData(info, identity);

    assert(mdata.templateName == "pbr" && "templateName=pbr");

    // uBaseColor = baseColorFactor。
    const MatIO::UniformOverrideValue* uBase = FindUniform(mdata, "uBaseColor");
    assert(uBase != nullptr && uBase->type == Render::MaterialUniformType::Vec4
           && "uBaseColor 应存在且为 vec4");
    {
        auto v = std::get<glm::vec4>(uBase->value);
        assert(FloatEq(v.x, 1.0f) && FloatEq(v.y, 1.0f)
            && FloatEq(v.z, 1.0f) && FloatEq(v.w, 1.0f)
            && "uBaseColor = baseColorFactor [1,1,1,1]");
    }

    // uMRA = (metallic, roughness, occlusionStrength, 0)。Avocado：=(1,1,1,0)。
    const MatIO::UniformOverrideValue* uMra = FindUniform(mdata, "uMRA");
    assert(uMra != nullptr && uMra->type == Render::MaterialUniformType::Vec4
           && "uMRA 应存在且为 vec4");
    {
        auto v = std::get<glm::vec4>(uMra->value);
        assert(FloatEq(v.x, 1.0f) && "uMRA.x = metallic");
        assert(FloatEq(v.y, 1.0f) && "uMRA.y = roughness");
        // 任务 A 回归：无 AO 贴图时 AO 位（uMRA.z）= 1.0 中性，shader
        // `ao = uMRA.z * aoTex.r` 配 default 白贴图 r=1 → ao=1（不破坏旧行为）。
        assert(FloatEq(v.z, 1.0f) && "uMRA.z = occlusionStrength（无 AO 贴图→中性 1.0）");
    }

    // texture 槽：binding 0 baseColor / 1 normal / 2 metalRough；无 binding 3。
    const MatIO::TextureOverrideEntry* t0 = FindTexture(mdata, 0u);
    const MatIO::TextureOverrideEntry* t1 = FindTexture(mdata, 1u);
    const MatIO::TextureOverrideEntry* t2 = FindTexture(mdata, 2u);
    const MatIO::TextureOverrideEntry* t3 = FindTexture(mdata, 3u);
    assert(t0 != nullptr && t0->path == info.baseColorSrc  && "binding 0 = baseColor 源路径");
    assert(t1 != nullptr && t1->path == info.normalSrc     && "binding 1 = normal 源路径");
    assert(t2 != nullptr && t2->path == info.metalRoughSrc && "binding 2 = metalRough 源路径");
    assert(t3 == nullptr && "Avocado 无 AO 贴图 → 无 binding 3 槽");
    assert(mdata.textures.size() == 3 && "恰 3 个 texture 槽");
    std::fprintf(stdout, "  [PASS] BuildMaterialFileData：pbr + uBaseColor + uMRA(AO=1) + binding 0/1/2\n");

    // ===== 3. .material 文件 round-trip（锁住落盘字段）=====
    {
        const std::string matPath = TempPath("avocado.material");
        bool wrote = MatIO::WriteMaterialFile(matPath, mdata);
        assert(wrote && "WriteMaterialFile 应成功");

        auto readOpt = MatIO::ReadMaterialFile(matPath);
        assert(readOpt.has_value() && "ReadMaterialFile 应成功");
        const auto& rd = *readOpt;
        assert(rd.templateName == "pbr");
        assert(rd.textures.size() == 3);

        const MatIO::UniformOverrideValue* rBase = FindUniform(rd, "uBaseColor");
        const MatIO::UniformOverrideValue* rMra  = FindUniform(rd, "uMRA");
        assert(rBase != nullptr && rMra != nullptr);
        auto rv = std::get<glm::vec4>(rMra->value);
        assert(FloatEq(rv.z, 1.0f) && "落盘 round-trip 后 uMRA.z 仍 = 1.0");

        const MatIO::TextureOverrideEntry* r0 = FindTexture(rd, 0u);
        assert(r0 != nullptr && r0->path == info.baseColorSrc
               && "round-trip 后 binding 0 路径不变");
        std::fprintf(stdout, "  [PASS] .material round-trip（写盘 → 读回字段一致）\n");
    }

    cgltf_free(data);

    // ===== 4. 合成 occlusionStrength（独立锁住任务 A 的 AO factor 修复）=====
    // 不依赖 fixture 有 AO 贴图：手造一个 occlusionStrength=0.5 + 有 aoSrc 的
    // GltfMatInfo，验证 BuildMaterialFileData 把 0.5 填进 uMRA.z（而非旧 bug 的
    // 写死 1.0），且 binding 3 AO 槽落地。
    {
        ImportNS::GltfMatInfo synth{};
        synth.present           = true;
        synth.metallic          = 0.25f;
        synth.roughness         = 0.75f;
        synth.occlusionStrength = 0.5f;
        synth.aoSrc             = "fake/ao.png";   // identity resolver 直接落它
        synth.baseColorSrc      = "fake/base.png";

        auto idr = [](const std::string& s) { return s; };
        MatIO::MaterialFileData sd = ImportNS::BuildMaterialFileData(synth, idr);

        const MatIO::UniformOverrideValue* sMra = FindUniform(sd, "uMRA");
        assert(sMra != nullptr);
        auto sv = std::get<glm::vec4>(sMra->value);
        assert(FloatEq(sv.x, 0.25f) && "uMRA.x = metallic");
        assert(FloatEq(sv.y, 0.75f) && "uMRA.y = roughness");
        assert(FloatEq(sv.z, 0.5f)
               && "uMRA.z = occlusionStrength 0.5（任务 A 修复：不再写死 1.0）");

        const MatIO::TextureOverrideEntry* s0 = FindTexture(sd, 0u);
        const MatIO::TextureOverrideEntry* s3 = FindTexture(sd, 3u);
        assert(s0 != nullptr && s0->path == "fake/base.png" && "binding 0 = baseColor");
        assert(s3 != nullptr && s3->path == "fake/ao.png"   && "binding 3 = AO 贴图槽");
        std::fprintf(stdout, "  [PASS] 合成 occlusionStrength=0.5 → uMRA.z=0.5 + binding 3 AO 槽\n");
    }

    std::fprintf(stdout, "[GltfMaterialImportTest] all tests passed.\n");
    return 0;
}
