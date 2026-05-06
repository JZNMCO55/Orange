// BuiltinMaterials::LoadToon / LoadRimLight 的最小单元测试。
//
// 4 条路径覆盖：
//   1. LoadToon 返回完整 Material：name="toon"、shader handle 有效、
//      uniforms 与文档约定一致、textureSlots 为空；
//   2. LoadRimLight 同上，但 schema 不同；
//   3. 重复 LoadToon dedup 命中（同 path → 同 handle，loader 不再跑）；
//   4. MaterialInstance 绑 toon 模板时只能 SetUniform 在 toon 的 uniform
//      上（rim_light 的 uniform 名在 toon instance 上是 no-op）——验证
//      Task 01 silent-ignore 与 Task 02 schema 真实分离。
//
// 测试假定：
//   * orange_engine_builtin_shaders 自定义 target 已编出 4 个 .spv 落到
//     `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders/orange_engine/`；
//   * test 可执行体也落在同一 $<CONFIG>/ 子目录（CMake 默认行为），所
//     以 BuiltinMaterials 内部的 .exe-相对解析能命中 .spv。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/render/BuiltinMaterials.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialTypes.h>

#include <cassert>
#include <cstdio>
#include <memory>

using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Render::Material;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialUniformType;
namespace BuiltinMaterials = Orange::Engine::Render::BuiltinMaterials;

namespace
{

// 在 Material.uniforms 里按 name 查 type；返回 nullptr 表示未声明。
const MaterialUniformType* FindUniformType(const Material& m, const char* name)
{
    for (const auto& u : m.uniforms)
    {
        if (u.name == name) return &u.type;
    }
    return nullptr;
}

void TestLoadToon(AssetRegistry& registry)
{
    Material toon = BuiltinMaterials::LoadToon(registry);

    assert(toon.name == "toon");
    assert(toon.vertexShader.IsValid());
    assert(toon.fragmentShader.IsValid());

    // 五个 uniform，与 src/render/builtin_shaders/toon.{vert,frag}.glsl 的
    // push_constant block 字段顺序一致。
    assert(toon.uniforms.size() == 5);
    const auto* uMVP             = FindUniformType(toon, "uMVP");
    const auto* uColorWarm       = FindUniformType(toon, "uColorWarm");
    const auto* uColorCool       = FindUniformType(toon, "uColorCool");
    const auto* uLightDir        = FindUniformType(toon, "uLightDir");
    const auto* uShadowThreshold = FindUniformType(toon, "uShadowThreshold");
    assert(uMVP             && *uMVP             == MaterialUniformType::Mat4);
    assert(uColorWarm       && *uColorWarm       == MaterialUniformType::Vec3);
    assert(uColorCool       && *uColorCool       == MaterialUniformType::Vec3);
    assert(uLightDir        && *uLightDir        == MaterialUniformType::Vec3);
    assert(uShadowThreshold && *uShadowThreshold == MaterialUniformType::Float);

    assert(toon.textureSlots.empty());

    std::fprintf(stdout, "  [PASS] BuiltinMaterials::LoadToon descriptor\n");
}

void TestLoadRimLight(AssetRegistry& registry)
{
    Material rim = BuiltinMaterials::LoadRimLight(registry);

    assert(rim.name == "rim_light");
    assert(rim.vertexShader.IsValid());
    assert(rim.fragmentShader.IsValid());

    assert(rim.uniforms.size() == 5);
    const auto* uMVP          = FindUniformType(rim, "uMVP");
    const auto* uViewPos      = FindUniformType(rim, "uViewPos");
    const auto* uRimColor     = FindUniformType(rim, "uRimColor");
    const auto* uRimPower     = FindUniformType(rim, "uRimPower");
    const auto* uRimIntensity = FindUniformType(rim, "uRimIntensity");
    assert(uMVP          && *uMVP          == MaterialUniformType::Mat4);
    assert(uViewPos      && *uViewPos      == MaterialUniformType::Vec3);
    assert(uRimColor     && *uRimColor     == MaterialUniformType::Vec3);
    assert(uRimPower     && *uRimPower     == MaterialUniformType::Float);
    assert(uRimIntensity && *uRimIntensity == MaterialUniformType::Float);

    assert(rim.textureSlots.empty());

    // toon 与 rim_light schema 真实分离——toon 没有 uRimColor。
    Material toon = BuiltinMaterials::LoadToon(registry);
    assert(FindUniformType(toon, "uRimColor") == nullptr);
    assert(FindUniformType(rim,  "uColorWarm") == nullptr);

    std::fprintf(stdout, "  [PASS] BuiltinMaterials::LoadRimLight descriptor + schema isolation\n");
}

void TestDedup(AssetRegistry& registry)
{
    // 同 path 重复 Load → AssetRegistry dedup 缓存命中，handle 沿用。
    Material first  = BuiltinMaterials::LoadToon(registry);
    Material second = BuiltinMaterials::LoadToon(registry);

    assert(first.vertexShader.Value()   == second.vertexShader.Value());
    assert(first.fragmentShader.Value() == second.fragmentShader.Value());

    std::fprintf(stdout, "  [PASS] BuiltinMaterials::LoadToon dedup\n");
}

void TestSchemaIsolationOnInstance(AssetRegistry& registry)
{
    Material toon = BuiltinMaterials::LoadToon(registry);
    MaterialInstance inst(&toon);

    // toon 的 uniform → 命中
    inst.SetUniform("uShadowThreshold", 0.5f);
    assert(inst.HasUniformOverride("uShadowThreshold"));

    // rim_light 的 uniform 名 → 在 toon instance 上是 no-op
    inst.SetUniform("uRimColor",     glm::vec3(1.0f));
    inst.SetUniform("uRimIntensity", 1.0f);
    assert(!inst.HasUniformOverride("uRimColor"));
    assert(!inst.HasUniformOverride("uRimIntensity"));

    std::fprintf(stdout, "  [PASS] MaterialInstance enforces template schema\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[BuiltinMaterialsTest] running\n");

    AssetRegistry registry;
    auto reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    if (reg.IsErr())
    {
        std::fprintf(stderr,
                     "[BuiltinMaterialsTest] RegisterLoader<ShaderAsset> failed (code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
        return 1;
    }

    TestLoadToon(registry);
    TestLoadRimLight(registry);
    TestDedup(registry);
    TestSchemaIsolationOnInstance(registry);

    std::fprintf(stdout, "[BuiltinMaterialsTest] all tests passed.\n");
    return 0;
}
