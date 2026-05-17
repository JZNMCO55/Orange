// MaterialFileIO 单元测试 —— .material 文件 v1.1 schema 双向 round-trip
// + v1.0 → v1.1 graceful 兼容。
//
// 覆盖 5 条路径：
//   1. WriteMaterialFile / ReadMaterialFile round-trip：templateName +
//      uniforms（含 6 种 type）+ textures
//   2. v1.0 兼容（仅 schemaVersion + templateName，无 uniforms/textures
//      字段）→ Read 成功 + uniforms/textures 为空
//   3. BuildDataFromInstance：从 MaterialInstance 抽出 override 状态
//   4. ApplyDataToInstance：把读出来的 data 应用到新 instance 上还原
//      SetUniform 状态
//   5. 错误路径：文件不存在 / namespace 不匹配 / templateName 缺失 →
//      ReadMaterialFile 返回 nullopt
//
// 测试 fixture 写入 temp 目录（CMAKE_BINARY_DIR/test_tmp_mat），跑完
// 不自动清理（便于失败 inspect）。

#include "MaterialFileIO.h"  // include path 由 CMake 加 tools/OrangeEditor

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialTypes.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace MatIO   = ::Orange::Editor::Material;
namespace Render  = ::Orange::Engine::Render;

namespace
{

bool FloatEq(float a, float b) noexcept { return std::fabs(a - b) < 1e-5f; }

std::string TempPath(const char* name)
{
    auto p = std::filesystem::temp_directory_path() / "orange_mat_file_io_test" / name;
    std::filesystem::create_directories(p.parent_path());
    return p.string();
}

// 1. v1.1 round-trip：写入全 6 种 uniform + 1 个 texture → 读回比对
void TestRoundTrip()
{
    const std::string path = TempPath("round_trip.material");

    MatIO::MaterialFileData out;
    out.templateName = "toon_custom";
    out.uniforms = {
        {"uFloat",  Render::MaterialUniformType::Float, 1.5f},
        {"uInt",    Render::MaterialUniformType::Int,   std::int32_t{42}},
        {"uVec2",   Render::MaterialUniformType::Vec2,  glm::vec2(0.1f, 0.2f)},
        {"uVec3",   Render::MaterialUniformType::Vec3,  glm::vec3(0.3f, 0.4f, 0.5f)},
        {"uVec4",   Render::MaterialUniformType::Vec4,  glm::vec4(0.6f, 0.7f, 0.8f, 0.9f)},
        {"uMat4",   Render::MaterialUniformType::Mat4,  glm::mat4(2.0f)},  // 2 沿对角线
    };
    out.textures = {
        {0, "assets/textures/foo.png"},
        {3, "assets/textures/bar.png"},
    };

    bool wrote = MatIO::WriteMaterialFile(path, out);
    assert(wrote);

    auto inOpt = MatIO::ReadMaterialFile(path);
    assert(inOpt.has_value());
    const auto& in = *inOpt;

    assert(in.templateName == "toon_custom");
    assert(in.uniforms.size() == 6);
    assert(in.textures.size() == 2);

    // uniforms 按写入顺序保留
    assert(in.uniforms[0].name == "uFloat");
    assert(in.uniforms[0].type == Render::MaterialUniformType::Float);
    assert(FloatEq(std::get<float>(in.uniforms[0].value), 1.5f));

    assert(in.uniforms[1].name == "uInt");
    assert(std::get<std::int32_t>(in.uniforms[1].value) == 42);

    assert(in.uniforms[2].name == "uVec2");
    auto v2 = std::get<glm::vec2>(in.uniforms[2].value);
    assert(FloatEq(v2.x, 0.1f) && FloatEq(v2.y, 0.2f));

    assert(in.uniforms[3].name == "uVec3");
    auto v3 = std::get<glm::vec3>(in.uniforms[3].value);
    assert(FloatEq(v3.x, 0.3f) && FloatEq(v3.y, 0.4f) && FloatEq(v3.z, 0.5f));

    assert(in.uniforms[4].name == "uVec4");
    auto v4 = std::get<glm::vec4>(in.uniforms[4].value);
    assert(FloatEq(v4.x, 0.6f) && FloatEq(v4.w, 0.9f));

    assert(in.uniforms[5].name == "uMat4");
    auto m4 = std::get<glm::mat4>(in.uniforms[5].value);
    assert(FloatEq(m4[0][0], 2.0f) && FloatEq(m4[1][1], 2.0f)
        && FloatEq(m4[2][2], 2.0f) && FloatEq(m4[3][3], 2.0f));

    assert(in.textures[0].binding == 0u);
    assert(in.textures[0].path == "assets/textures/foo.png");
    assert(in.textures[1].binding == 3u);
    assert(in.textures[1].path == "assets/textures/bar.png");

    std::fprintf(stdout, "  [PASS] v1.1 round-trip\n");
}

// 2. v1.0 兼容：手写最小 v1.0 JSON（仅 schemaVersion + templateName） →
//    Read 成功 + uniforms/textures 为空
void TestV10Compat()
{
    const std::string path = TempPath("v10_compat.material");
    {
        std::ofstream f(path);
        f << R"({
  "schemaVersion": {"namespace":"render/material_instance","major":1,"minor":0},
  "templateName": "legacy_toon"
})";
    }

    auto inOpt = MatIO::ReadMaterialFile(path);
    assert(inOpt.has_value());
    assert(inOpt->templateName == "legacy_toon");
    assert(inOpt->uniforms.empty());
    assert(inOpt->textures.empty());

    std::fprintf(stdout, "  [PASS] v1.0 兼容（无 uniforms / textures 字段）\n");
}

// 3 + 4. BuildDataFromInstance + ApplyDataToInstance round-trip：
//        Instance A → Build → Apply 到 Instance B → 验证 override 一致
void TestInstanceRoundTrip()
{
    Render::Material m;
    m.uniforms.push_back({"uColor",  Render::MaterialUniformType::Vec3});
    m.uniforms.push_back({"uAlpha",  Render::MaterialUniformType::Float});
    m.uniforms.push_back({"uMVP",    Render::MaterialUniformType::Mat4});
    m.textureSlots.push_back({0, "uAlbedo"});

    Render::MaterialInstance a(&m);
    a.SetUniform("uColor",  glm::vec3(0.2f, 0.5f, 0.7f));
    a.SetUniform("uAlpha",  0.85f);
    a.SetUniform("uMVP",    glm::mat4(1.0f));
    a.SetTexture(0, ::Orange::Engine::Asset::AssetHandle<
                        ::Orange::Engine::Asset::TextureAsset>{99});

    MatIO::MaterialFileData data = MatIO::BuildDataFromInstance(a, "test_template");
    assert(data.templateName == "test_template");
    assert(data.uniforms.size() == 3);
    assert(data.textures.size() == 1);
    assert(data.textures[0].binding == 0u);
    // path 暂为空：写盘端没有 AssetHandle → 源路径反查 API
    assert(data.textures[0].path.empty());

    // 应用到新 instance（pAssetRegistry == nullptr：texture 跳过还原，
    // 因为本测试中 texture path 段为空）
    Render::MaterialInstance b(&m);
    MatIO::ApplyDataToInstance(data, b, nullptr);

    // uniform override 全部还原
    auto vColor = b.GetUniformVec3("uColor");
    assert(vColor.has_value());
    assert(FloatEq(vColor->x, 0.2f) && FloatEq(vColor->y, 0.5f) && FloatEq(vColor->z, 0.7f));

    auto vAlpha = b.GetUniformFloat("uAlpha");
    assert(vAlpha.has_value() && FloatEq(*vAlpha, 0.85f));

    auto vMVP = b.GetUniformMat4("uMVP");
    assert(vMVP.has_value() && FloatEq((*vMVP)[0][0], 1.0f));

    // texture override 未还原（path 段空 → skip）
    assert(!b.HasTextureOverride(0));

    std::fprintf(stdout, "  [PASS] BuildDataFromInstance + ApplyDataToInstance 还原 uniform override\n");
}

// 5. Texture override 完整 round-trip（GAP-2026-05-17-asset-registry-handle-to-path）：
//    AssetRegistry.Insert<TextureAsset> → SetTexture(handle) →
//    BuildDataFromInstance(&registry) 写 path → 序列化 → 反序列化 →
//    ApplyDataToInstance(&registry) → HasTextureOverride + GetTextureBinding
//    命中的 handle 通过 registry.Get 拿到同一 TextureAsset（即 dedup 命中
//    原 entry，没有重复加载）。
void TestTextureRoundTripWithRegistry()
{
    using ::Orange::Engine::Asset::AssetHandle;
    using ::Orange::Engine::Asset::AssetRegistry;
    using ::Orange::Engine::Asset::TextureAsset;
    using ::Orange::Engine::Asset::TextureFormat;

    AssetRegistry registry;

    // 程序式构造一张 1x1 红色纹理，Insert 进 registry（不需 RegisterLoader
    // 因为走 Insert 路径自带 deleter，详见 AssetRegistry.h Insert 注释）。
    const std::string texPath = "assets/textures/round_trip_red.png";
    auto tex = std::make_unique<TextureAsset>(
        1u, 1u, TextureFormat::R8G8B8A8_UNorm,
        std::vector<std::uint8_t>{255, 0, 0, 255});
    auto insertResult = registry.Insert<TextureAsset>(texPath, std::move(tex));
    assert(insertResult.IsOk());
    const AssetHandle<TextureAsset> texHandle = insertResult.Value();
    assert(texHandle.IsValid());

    // 验证 PathOf 反查 OK（前置 sanity）
    auto pathView = registry.PathOf(texHandle);
    assert(std::string(pathView) == texPath);

    // 准备 Material schema + Instance A，挂 texture override 到 binding 2
    Render::Material m;
    m.textureSlots.push_back({2u, "uMain"});

    Render::MaterialInstance a(&m);
    a.SetTexture(2u, texHandle);
    assert(a.HasTextureOverride(2u));

    // BuildDataFromInstance(&registry)：texture path 应被填充
    MatIO::MaterialFileData data = MatIO::BuildDataFromInstance(a, "tex_tpl", &registry);
    assert(data.templateName == "tex_tpl");
    assert(data.textures.size() == 1);
    assert(data.textures[0].binding == 2u);
    assert(data.textures[0].path == texPath);  // PathOf 写盘成功

    // 序列化 + 反序列化
    const std::string filePath = TempPath("texture_round_trip.material");
    bool wrote = MatIO::WriteMaterialFile(filePath, data);
    assert(wrote);

    auto readOpt = MatIO::ReadMaterialFile(filePath);
    assert(readOpt.has_value());
    assert(readOpt->textures.size() == 1);
    assert(readOpt->textures[0].binding == 2u);
    assert(readOpt->textures[0].path == texPath);

    // 应用到新 instance B —— Apply 内部走 registry.Load<TextureAsset>(path)。
    // AssetRegistry::LoadErased 在 loader 检查之前先看 pathToHandle dedup
    // 表（src/asset/AssetRegistry.cpp:352）：Insert 路径挂进去的 entry 同
    // path 命中即可直接返回 cached handle，根本不调 loader。所以即使本测
    // 试没 RegisterLoader<TextureAsset>，Apply 的 SetTexture 仍能命中。
    Render::MaterialInstance b(&m);
    MatIO::ApplyDataToInstance(*readOpt, b, &registry);
    assert(b.HasTextureOverride(2u));
    // 还原的 handle 与原 Insert 拿到的 handle 应是同一个（dedup 命中）。
    auto restoredHandle = b.GetTextureBinding(2u);
    assert(restoredHandle.IsValid());
    assert(restoredHandle.Value() == texHandle.Value());
    // 验证 registry.Get 拿回的 TextureAsset 是原 entry（指针等价）。
    const TextureAsset* pBefore = registry.Get(texHandle);
    const TextureAsset* pAfter  = registry.Get(restoredHandle);
    assert(pBefore != nullptr && pAfter == pBefore);

    std::fprintf(stdout,
                 "  [PASS] texture override 完整 round-trip（PathOf 写盘 + dedup 命中还原）\n");
}

// 6. 错误路径
void TestErrorPaths()
{
    // 文件不存在
    auto miss = MatIO::ReadMaterialFile(TempPath("nonexistent.material"));
    assert(!miss.has_value());

    // namespace 不匹配
    {
        const std::string path = TempPath("wrong_ns.material");
        std::ofstream f(path);
        f << R"({
  "schemaVersion": {"namespace":"foo/bar","major":1,"minor":0},
  "templateName": "toon"
})";
        f.close();
        auto r = MatIO::ReadMaterialFile(path);
        assert(!r.has_value());
    }

    // templateName 缺失
    {
        const std::string path = TempPath("no_name.material");
        std::ofstream f(path);
        f << R"({
  "schemaVersion": {"namespace":"render/material_instance","major":1,"minor":1}
})";
        f.close();
        auto r = MatIO::ReadMaterialFile(path);
        assert(!r.has_value());
    }

    // major 不匹配（v2.0 文件 reader 拒）
    {
        const std::string path = TempPath("major_mismatch.material");
        std::ofstream f(path);
        f << R"({
  "schemaVersion": {"namespace":"render/material_instance","major":2,"minor":0},
  "templateName": "toon"
})";
        f.close();
        auto r = MatIO::ReadMaterialFile(path);
        assert(!r.has_value());
    }

    std::fprintf(stdout, "  [PASS] 错误路径（文件不存在 / namespace / name 缺失 / major 不匹配）\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[MaterialFileIOTest] running\n");
    TestRoundTrip();
    TestV10Compat();
    TestInstanceRoundTrip();
    TestTextureRoundTripWithRegistry();
    TestErrorPaths();
    std::fprintf(stdout, "[MaterialFileIOTest] all tests passed.\n");
    return 0;
}
