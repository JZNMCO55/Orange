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

    // Task 07 重构：toon push-constant 收缩为 {uMVP, uModel} = 128 B；
    // 颜色 / threshold / light dir 等参数迁到 light UBO 或 hardcode。
    assert(toon.uniforms.size() == 2);
    const auto* uMVP   = FindUniformType(toon, "uMVP");
    const auto* uModel = FindUniformType(toon, "uModel");
    assert(uMVP   && *uMVP   == MaterialUniformType::Mat4);
    assert(uModel && *uModel == MaterialUniformType::Mat4);

    assert(toon.textureSlots.empty());

    std::fprintf(stdout, "  [PASS] BuiltinMaterials::LoadToon descriptor\n");
}

void TestLoadRimLight(AssetRegistry& registry)
{
    Material rim = BuiltinMaterials::LoadRimLight(registry);

    assert(rim.name == "rim_light");
    assert(rim.vertexShader.IsValid());
    assert(rim.fragmentShader.IsValid());

    // Task 07 重构：rim_light 与 toon 同形态，push-constant {uMVP, uModel}。
    assert(rim.uniforms.size() == 2);
    const auto* uMVP   = FindUniformType(rim, "uMVP");
    const auto* uModel = FindUniformType(rim, "uModel");
    assert(uMVP   && *uMVP   == MaterialUniformType::Mat4);
    assert(uModel && *uModel == MaterialUniformType::Mat4);

    assert(rim.textureSlots.empty());

    // toon 与 rim_light 当前 schema 一致；schema 隔离体现在 fragment
    // shader 内部参数 hardcode（warm/cool 配色 vs rim glow），与
    // BuiltinMaterials 描述符无关。下面这行验证两者旧字段都不再出现
    // —— 确保迁移彻底。
    Material toon = BuiltinMaterials::LoadToon(registry);
    assert(FindUniformType(toon, "uRimColor")  == nullptr);
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

    // toon 的 uniform → 命中（uMVP / uModel 都是 Pipeline 推送的，调用
    // 方一般不会 SetUniform 走 instance 覆盖；这里仅验证 silent-ignore
    // 之外的"name 命中即写入" 路径仍然好使）。
    inst.SetUniform("uMVP", glm::mat4(1.0f));
    assert(inst.HasUniformOverride("uMVP"));

    // 旧字段 / rim_light 字段 → 不在 toon schema 里，silent no-op。
    inst.SetUniform("uColorWarm",   glm::vec3(1.0f));
    inst.SetUniform("uRimColor",    glm::vec3(1.0f));
    inst.SetUniform("uRimPower",    1.0f);
    assert(!inst.HasUniformOverride("uColorWarm"));
    assert(!inst.HasUniformOverride("uRimColor"));
    assert(!inst.HasUniformOverride("uRimPower"));

    std::fprintf(stdout, "  [PASS] MaterialInstance enforces template schema\n");
}

void TestLoadDissolve(AssetRegistry& registry)
{
    Material dissolve = BuiltinMaterials::LoadDissolve(registry);

    assert(dissolve.name == "dissolve");
    assert(dissolve.vertexShader.IsValid());
    assert(dissolve.fragmentShader.IsValid());

    // 与 toon / rim_light 同模式：push-constant {uMVP, uModel}；其余
    // dissolve 参数（noise scale / edge width / edge color / dissolveT）
    // 全部 hardcode 在 fragment shader 内（dissolveT 由 light UBO 的
    // uFrameInfo.x 驱动）。
    assert(dissolve.uniforms.size() == 2);
    const auto* uMVP   = FindUniformType(dissolve, "uMVP");
    const auto* uModel = FindUniformType(dissolve, "uModel");
    assert(uMVP   && *uMVP   == MaterialUniformType::Mat4);
    assert(uModel && *uModel == MaterialUniformType::Mat4);

    assert(dissolve.textureSlots.empty());

    std::fprintf(stdout, "  [PASS] BuiltinMaterials::LoadDissolve descriptor\n");
}

void TestLoadEmissive(AssetRegistry& registry)
{
    Material emissive = BuiltinMaterials::LoadEmissive(registry);

    assert(emissive.name == "emissive");
    assert(emissive.vertexShader.IsValid());
    assert(emissive.fragmentShader.IsValid());

    // 与 dissolve 同形态：仅 push-constant {uMVP, uModel}；颜色 / intensity
    // hardcode 进 emissive.frag.glsl，HDR > 1 由 bloom 拾取。
    assert(emissive.uniforms.size() == 2);
    const auto* uMVP   = FindUniformType(emissive, "uMVP");
    const auto* uModel = FindUniformType(emissive, "uModel");
    assert(uMVP   && *uMVP   == MaterialUniformType::Mat4);
    assert(uModel && *uModel == MaterialUniformType::Mat4);

    assert(emissive.textureSlots.empty());

    // 验证 dissolve / emissive 的 vertex / fragment shader handle 是各
    // 自不同的——不是所有 builtin 模板都共享同一份 SPIR-V（避免误把
    // emissive 编译成走 dissolve frag 的"看似能跑"路径）。
    Material dissolve = BuiltinMaterials::LoadDissolve(registry);
    assert(emissive.fragmentShader.Value() != dissolve.fragmentShader.Value());

    std::fprintf(stdout, "  [PASS] BuiltinMaterials::LoadEmissive descriptor + shader isolation\n");
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
    TestLoadDissolve(registry);
    TestLoadEmissive(registry);

    std::fprintf(stdout, "[BuiltinMaterialsTest] all tests passed.\n");
    return 0;
}
