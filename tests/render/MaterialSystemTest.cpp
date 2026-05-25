// MaterialSystem 单元测试。验证 6 条路径：
//   1. 默认构造的 MaterialSystem TemplateCount == 0、FindTemplate 任意 name
//      返回 nullptr；
//   2. RegisterBuiltins() 后 TemplateCount == 2、FindTemplate("toon") /
//      FindTemplate("rim_light") 都命中且 name 字段对；
//   3. 自定义 ShaderTemplateDesc 注册（用内置 toon.spv 路径作为占位
//      shader、自取一个新 name "test_template" + 若干 uniform）→
//      FindTemplate 命中、uniforms 数量与 desc 一致；
//   4. 重复 RegisterTemplate 同名返回 AlreadyExists、表内 template 不
//      被覆盖；
//   5. CreateInstance("toon") 返回非 nullptr，绑定的 Material->name ==
//      "toon"；CreateInstance("nonexistent") 返回 nullptr；
//   6. 通过 CreateInstance 拿到的 MaterialInstance 上 SetUniform 命中
//      toon 的 uniform 名（验证 Task 01 silent-ignore 与 Task 04
//      system-managed Material 引用真正贯通）。
//
// 测试假定：
//   * orange_engine_builtin_shaders 自定义 target 已编出 4 个 .spv 落到
//     `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders/orange_engine/`；
//   * test 可执行体也落在同一 $<CONFIG>/ 子目录（CMake 默认行为），所
//     以 BuiltinMaterials 内部的 .exe-相对解析能命中 .spv。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/MaterialTypes.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

using Orange::Engine::ResultCode;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Render::Material;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::MaterialUniformType;
using Orange::Engine::Render::ShaderTemplateDesc;

namespace
{

// 与 BuiltinMaterials.cpp 内部 GetExecutableDir 同思路——把内置 shader
// 的真实绝对路径解析出来，作为自定义模板的 SPIR-V 占位路径，这样
// AssetRegistry::Load<ShaderAsset> 能真正命中文件、产生有效 handle，
// 验证"自定义 desc 注册"路径走通。
std::filesystem::path GetExecutableDir()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
    {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::wstring(buffer, len)).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path BuiltinShaderPath(const char* relative)
{
    return GetExecutableDir() / relative;
}

// 1. 默认 MaterialSystem 空表 + FindTemplate 行为
void TestEmptyConstruction()
{
    AssetRegistry registry;
    MaterialSystem matSys(registry);

    assert(matSys.TemplateCount() == 0);
    assert(matSys.FindTemplate("toon")        == nullptr);
    assert(matSys.FindTemplate("rim_light")   == nullptr);
    assert(matSys.FindTemplate("nonexistent") == nullptr);
    assert(matSys.FindTemplate("")            == nullptr);

    std::fprintf(stdout, "  [PASS] MaterialSystem 空表与 FindTemplate 默认行为\n");
}

// 2. RegisterBuiltins 注册 textured + toon + rim_light + dissolve + emissive + pbr
void TestRegisterBuiltins()
{
    AssetRegistry registry;
    auto reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    assert(reg.IsOk());

    MaterialSystem matSys(registry);
    auto result = matSys.RegisterBuiltins();
    assert(result.IsOk());
    // textured / toon / rim_light / dissolve / emissive / pbr 六件内置模板。
    assert(matSys.TemplateCount() == 6);

    const Material* textured = matSys.FindTemplate("textured");
    const Material* toon     = matSys.FindTemplate("toon");
    const Material* rim      = matSys.FindTemplate("rim_light");
    const Material* dissolve = matSys.FindTemplate("dissolve");
    const Material* emissive = matSys.FindTemplate("emissive");
    const Material* pbr      = matSys.FindTemplate("pbr");
    assert(textured != nullptr);
    assert(toon     != nullptr);
    assert(rim      != nullptr);
    assert(dissolve != nullptr);
    assert(emissive != nullptr);
    assert(pbr      != nullptr);
    assert(textured->name == "textured");
    assert(toon->name     == "toon");
    assert(rim->name      == "rim_light");
    assert(dissolve->name == "dissolve");
    assert(emissive->name == "emissive");
    assert(pbr->name      == "pbr");
    assert(textured->vertexShader.IsValid());
    assert(textured->fragmentShader.IsValid());
    assert(toon->vertexShader.IsValid());
    assert(toon->fragmentShader.IsValid());
    assert(rim->vertexShader.IsValid());
    assert(rim->fragmentShader.IsValid());
    assert(dissolve->vertexShader.IsValid());
    assert(dissolve->fragmentShader.IsValid());
    assert(emissive->vertexShader.IsValid());
    assert(emissive->fragmentShader.IsValid());
    assert(pbr->vertexShader.IsValid());
    assert(pbr->fragmentShader.IsValid());

    // textured / toon / rim_light push-constant 收为 {uMVP, uModel} = 2 项；
    // textured 多带一个 textureSlot 占位。pbr 扩出 uBaseColor + uMRA 两条
    // vec4，共 4 项（uMVP / uModel / uBaseColor / uMRA）；textureSlots 自
    // GAP-2026-05-25 A2/G1 起为 set 1 的 4 槽（baseColor / normal / metalRough /
    // ao，binding 0..3），Pipeline 按 MaterialInstance 绑定（未绑喂 default 贴图）。
    assert(textured->uniforms.size()     == 2);
    assert(textured->textureSlots.size() == 1);
    assert(toon->uniforms.size() == 2);
    assert(rim->uniforms.size()  == 2);
    assert(pbr->uniforms.size()     == 4);
    assert(pbr->textureSlots.size() == 4);
    assert(pbr->textureSlots[0].binding == 0 && pbr->textureSlots[0].name == "uBaseColorTex");
    assert(pbr->textureSlots[1].binding == 1 && pbr->textureSlots[1].name == "uNormalTex");
    assert(pbr->textureSlots[2].binding == 2 && pbr->textureSlots[2].name == "uMetalRoughTex");
    assert(pbr->textureSlots[3].binding == 3 && pbr->textureSlots[3].name == "uAoTex");

    std::fprintf(stdout, "  [PASS] RegisterBuiltins 注册 textured + toon + rim_light + dissolve + emissive + pbr\n");
}

// 3. 自定义 ShaderTemplateDesc 注册
void TestRegisterCustomTemplate()
{
    AssetRegistry registry;
    auto reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    assert(reg.IsOk());

    MaterialSystem matSys(registry);

    ShaderTemplateDesc desc;
    desc.name              = "test_template";
    desc.vertexSpirvPath   = BuiltinShaderPath("shaders/orange_engine/toon.vert.spv");
    desc.fragmentSpirvPath = BuiltinShaderPath("shaders/orange_engine/toon.frag.spv");
    desc.uniforms = {
        {"uColor",  MaterialUniformType::Vec3 },
        {"uAlpha",  MaterialUniformType::Float},
        {"uMatrix", MaterialUniformType::Mat4 },
    };
    desc.textureSlots = {
        {0, "uMainTex"},
    };

    auto result = matSys.RegisterTemplate(desc);
    assert(result.IsOk());
    assert(matSys.TemplateCount() == 1);

    const Material* mat = matSys.FindTemplate("test_template");
    assert(mat != nullptr);
    assert(mat->name == "test_template");
    assert(mat->uniforms.size()     == 3);
    assert(mat->textureSlots.size() == 1);
    assert(mat->textureSlots[0].binding == 0u);
    assert(mat->textureSlots[0].name    == "uMainTex");
    // SPIR-V 真实存在 → handle 应该有效
    assert(mat->vertexShader.IsValid());
    assert(mat->fragmentShader.IsValid());

    std::fprintf(stdout, "  [PASS] 自定义 ShaderTemplateDesc 注册路径\n");
}

// 4. 重名注册返回 AlreadyExists、表内不被覆盖
void TestDuplicateNameRejected()
{
    AssetRegistry registry;
    auto reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    assert(reg.IsOk());

    MaterialSystem matSys(registry);
    matSys.RegisterBuiltins();

    // 再次注册一个同名 "toon" 的自定义 desc，但 uniforms 故意不同——
    // 如果错误路径覆盖了原表，FindTemplate("toon") 后 uniforms.size 就
    // 会变成 1 而不是原本的 5。
    ShaderTemplateDesc dupDesc;
    dupDesc.name              = "toon";
    dupDesc.vertexSpirvPath   = BuiltinShaderPath("shaders/orange_engine/toon.vert.spv");
    dupDesc.fragmentSpirvPath = BuiltinShaderPath("shaders/orange_engine/toon.frag.spv");
    dupDesc.uniforms = {
        {"uOverride", MaterialUniformType::Float},
    };

    auto result = matSys.RegisterTemplate(dupDesc);
    assert(result.IsErr());
    assert(result.Error() == ResultCode::AlreadyExists);
    // textured + toon + rim_light + dissolve + emissive + pbr 六件内置模板。
    assert(matSys.TemplateCount() == 6);

    const Material* toon = matSys.FindTemplate("toon");
    assert(toon != nullptr);
    assert(toon->uniforms.size() == 2);  // 没被 dupDesc 覆盖（Task 07 后 schema = uMVP+uModel）

    std::fprintf(stdout, "  [PASS] 重名注册返回 AlreadyExists 且不覆盖原表\n");
}

// 5. CreateInstance 路径
void TestCreateInstance()
{
    AssetRegistry registry;
    auto reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    assert(reg.IsOk());

    MaterialSystem matSys(registry);
    matSys.RegisterBuiltins();

    auto toonInst = matSys.CreateInstance("toon");
    assert(toonInst != nullptr);
    const Material* boundMat = toonInst->GetMaterial();
    assert(boundMat != nullptr);
    assert(boundMat->name == "toon");

    auto rimInst = matSys.CreateInstance("rim_light");
    assert(rimInst != nullptr);
    assert(rimInst->GetMaterial()->name == "rim_light");

    auto missingInst = matSys.CreateInstance("nonexistent");
    assert(missingInst == nullptr);

    auto emptyInst = matSys.CreateInstance("");
    assert(emptyInst == nullptr);

    std::fprintf(stdout, "  [PASS] CreateInstance 命中 / 不命中路径\n");
}

// 6.5 GetTemplateNames：默认空、RegisterBuiltins 后 6 项、自定义注册后 7 项
void TestGetTemplateNames()
{
    AssetRegistry registry;
    auto reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    assert(reg.IsOk());

    MaterialSystem matSys(registry);
    assert(matSys.GetTemplateNames().empty());

    matSys.RegisterBuiltins();
    std::vector<std::string> names = matSys.GetTemplateNames();
    assert(names.size() == 6);
    // 不假定顺序——unordered_map 遍历无序。排序后比对内容。
    std::sort(names.begin(), names.end());
    assert(names[0] == "dissolve");
    assert(names[1] == "emissive");
    assert(names[2] == "pbr");
    assert(names[3] == "rim_light");
    assert(names[4] == "textured");
    assert(names[5] == "toon");

    // 自定义注册后 1 + 6 = 7 项，且新名出现在列表里
    ShaderTemplateDesc desc;
    desc.name              = "user_custom";
    desc.vertexSpirvPath   = BuiltinShaderPath("shaders/orange_engine/toon.vert.spv");
    desc.fragmentSpirvPath = BuiltinShaderPath("shaders/orange_engine/toon.frag.spv");
    auto regResult = matSys.RegisterTemplate(desc);
    assert(regResult.IsOk());

    names = matSys.GetTemplateNames();
    assert(names.size() == 7);
    const bool hasCustom =
        std::find(names.begin(), names.end(), std::string("user_custom")) != names.end();
    assert(hasCustom);

    std::fprintf(stdout, "  [PASS] GetTemplateNames 默认空 + builtin 6 + 自定义 7\n");
}

// 6. CreateInstance 拿到的 MaterialInstance 上 SetUniform 真正命中 toon
//    的 uniform 名 —— 验证 Task 01 silent-ignore 与 Task 04 system-managed
//    Material 引用贯通
void TestInstanceUniformRouting()
{
    AssetRegistry registry;
    auto reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    assert(reg.IsOk());

    MaterialSystem matSys(registry);
    matSys.RegisterBuiltins();

    auto inst = matSys.CreateInstance("toon");
    assert(inst != nullptr);

    // toon 的 uniform → 命中（Task 07 后 schema 收缩为 uMVP / uModel）
    inst->SetUniform("uMVP", glm::mat4(1.0f));
    assert(inst->HasUniformOverride("uMVP"));

    // 不在 schema 里的字段（旧 toon 字段 / rim_light 字段）→ 在 toon
    // instance 上是 no-op
    inst->SetUniform("uShadowThreshold", 0.5f);
    inst->SetUniform("uRimColor",        glm::vec3(1.0f));
    inst->SetUniform("uRimIntensity",    1.0f);
    assert(!inst->HasUniformOverride("uShadowThreshold"));
    assert(!inst->HasUniformOverride("uRimColor"));
    assert(!inst->HasUniformOverride("uRimIntensity"));

    std::fprintf(stdout, "  [PASS] system-managed instance 的 uniform 路由\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[MaterialSystemTest] running\n");

    TestEmptyConstruction();
    TestRegisterBuiltins();
    TestRegisterCustomTemplate();
    TestDuplicateNameRejected();
    TestCreateInstance();
    TestGetTemplateNames();
    TestInstanceUniformRouting();

    std::fprintf(stdout, "[MaterialSystemTest] all tests passed.\n");
    return 0;
}
