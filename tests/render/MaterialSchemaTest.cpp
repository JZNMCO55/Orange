// MaterialSchema 单元测试 —— Task 06.01 子任务的验收门槛之一。
//
// 覆盖两条新增表面：
//   1. BuiltinMaterials::LoadTextured 返回的 Material 描述符与 textured_mesh
//      shader 对齐：name="textured"、shader handle 有效、uniforms 仅 uMVP、
//      textureSlots 仅 binding 0；
//   2. MaterialInstance 的读回 API（GetUniformXxx / GetTextureBinding）
//      在 SetUniform / SetTexture 之后能拿回原值；type 不匹配的读回返回
//      nullopt；name / binding 不存在时同样返回 nullopt / 无效 handle。
//
// 测试假定：
//   * orange_engine_builtin_shaders 自定义 target 已经把 textured_mesh.vert
//     / .frag 编出 .spv 落到 .exe 同目录的 shaders/orange_engine/。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/render/BuiltinMaterials.h>
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
#include <memory>

using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Asset::TextureAsset;
using Orange::Engine::Render::Material;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialUniformType;
namespace BuiltinMaterials = Orange::Engine::Render::BuiltinMaterials;

namespace
{

    bool ApproxEqual(float a, float b) noexcept
    {
        return std::fabs(a - b) < 1e-5f;
    }

    const MaterialUniformType* FindUniformType(const Material& m, const char* name)
    {
        for (const auto& u : m.uniforms)
        {
            if (u.name == name)
                return &u.type;
        }
        return nullptr;
    }

    // 1. LoadTextured 描述符与 textured_mesh shader 对齐
    void TestLoadTexturedDescriptor(AssetRegistry& registry)
    {
        Material textured = BuiltinMaterials::LoadTextured(registry);

        assert(textured.name == "textured");
        assert(textured.vertexShader.IsValid());
        assert(textured.fragmentShader.IsValid());

        // Task 07 起 textured 与 toon / rim_light 同 push schema = {uMVP, uModel}。
        assert(textured.uniforms.size() == 2);
        const auto* uMVP   = FindUniformType(textured, "uMVP");
        const auto* uModel = FindUniformType(textured, "uModel");
        assert(uMVP && *uMVP == MaterialUniformType::Mat4);
        assert(uModel && *uModel == MaterialUniformType::Mat4);

        // 一个纹理槽 binding=0，name="uTexture"。当前 fragment shader 不实
        // 际采样它，但 schema 已经稳定。
        assert(textured.textureSlots.size() == 1);
        assert(textured.textureSlots[0].binding == 0u);
        assert(textured.textureSlots[0].name == "uTexture");

        // 重复 Load 同 path → AssetRegistry dedup 命中。
        Material again = BuiltinMaterials::LoadTextured(registry);
        assert(again.vertexShader.Value() == textured.vertexShader.Value());
        assert(again.fragmentShader.Value() == textured.fragmentShader.Value());

        std::fprintf(stdout, "  [PASS] BuiltinMaterials::LoadTextured descriptor + dedup\n");
    }

    // 2. MaterialInstance 读回：SetUniform → GetUniform 往返
    void TestUniformRoundTrip()
    {
        // 自构 Material：textureSlot binding 0、几个 uniform 覆盖所有类型。
        Material mat;
        mat.name     = "schema_test";
        mat.uniforms = {
            {"uFloat", MaterialUniformType::Float},
            {"uInt", MaterialUniformType::Int},
            {"uVec2", MaterialUniformType::Vec2},
            {"uVec3", MaterialUniformType::Vec3},
            {"uVec4", MaterialUniformType::Vec4},
            {"uMat4", MaterialUniformType::Mat4},
        };
        mat.textureSlots = {{0, "uMain"}};

        MaterialInstance inst(&mat);

        // 未 Set 时所有 GetUniform 返回 nullopt。
        assert(!inst.GetUniformFloat("uFloat").has_value());
        assert(!inst.GetUniformInt("uInt").has_value());
        assert(!inst.GetUniformVec2("uVec2").has_value());
        assert(!inst.GetUniformVec3("uVec3").has_value());
        assert(!inst.GetUniformVec4("uVec4").has_value());
        assert(!inst.GetUniformMat4("uMat4").has_value());

        inst.SetUniform("uFloat", 1.25f);
        inst.SetUniform("uInt", std::int32_t{-42});
        inst.SetUniform("uVec2", glm::vec2(0.1f, 0.2f));
        inst.SetUniform("uVec3", glm::vec3(0.3f, 0.4f, 0.5f));
        inst.SetUniform("uVec4", glm::vec4(0.6f, 0.7f, 0.8f, 0.9f));
        inst.SetUniform("uMat4", glm::mat4(2.0f));

        assert(ApproxEqual(*inst.GetUniformFloat("uFloat"), 1.25f));
        assert(*inst.GetUniformInt("uInt") == -42);
        {
            auto v = inst.GetUniformVec2("uVec2");
            assert(v && ApproxEqual(v->x, 0.1f) && ApproxEqual(v->y, 0.2f));
        }
        {
            auto v = inst.GetUniformVec3("uVec3");
            assert(v && ApproxEqual(v->x, 0.3f) && ApproxEqual(v->y, 0.4f) && ApproxEqual(v->z, 0.5f));
        }
        {
            auto v = inst.GetUniformVec4("uVec4");
            assert(v && ApproxEqual(v->x, 0.6f) && ApproxEqual(v->y, 0.7f) && ApproxEqual(v->z, 0.8f) && ApproxEqual(v->w, 0.9f));
        }
        {
            auto m = inst.GetUniformMat4("uMat4");
            assert(m && ApproxEqual((*m)[0][0], 2.0f) && ApproxEqual((*m)[3][3], 2.0f));
        }

        std::fprintf(stdout, "  [PASS] MaterialInstance uniform round-trip\n");
    }

    // 3. 读回 type 不匹配 → nullopt；name 不存在 → nullopt
    void TestUniformTypeMismatchAndMissing()
    {
        Material mat;
        mat.name     = "mismatch_test";
        mat.uniforms = {
            {"uFloat", MaterialUniformType::Float},
            {"uVec3", MaterialUniformType::Vec3},
        };

        MaterialInstance inst(&mat);

        // SetUniform 已 silent-ignore 类型不匹配，所以下面这条 SetUniform
        // 是 no-op；GetUniformFloat("uFloat") 因为没设置过，依旧 nullopt。
        inst.SetUniform("uFloat", glm::vec3(1.0f)); // 类型不匹配 → no-op
        assert(!inst.GetUniformFloat("uFloat").has_value());

        inst.SetUniform("uFloat", 7.0f); // 类型匹配 → 落库
        assert(inst.GetUniformFloat("uFloat").has_value());
        // 用错误的 GetUniformXxx 取已落库的 uFloat → 应为 nullopt（type 检查
        // 在读回路径上同样生效）。
        assert(!inst.GetUniformVec3("uFloat").has_value());

        // name 不存在 → nullopt。
        assert(!inst.GetUniformFloat("uMissing").has_value());
        assert(!inst.GetUniformVec3("uMissing").has_value());

        std::fprintf(stdout, "  [PASS] MaterialInstance type mismatch / missing → nullopt\n");
    }

    // 4. SetTexture / GetTextureBinding 往返
    void TestTextureRoundTrip()
    {
        Material mat;
        mat.name         = "texture_test";
        mat.textureSlots = {{0, "uAlbedo"}, {3, "uNoise"}};

        MaterialInstance inst(&mat);

        // 未 Set 时 GetTextureBinding 返回无效 handle（IsValid()==false）。
        assert(!inst.GetTextureBinding(0).IsValid());
        assert(!inst.GetTextureBinding(3).IsValid());
        assert(!inst.GetTextureBinding(99).IsValid()); // binding 不存在

        inst.SetTexture(0, AssetHandle<TextureAsset>{1234});
        inst.SetTexture(3, AssetHandle<TextureAsset>{5678});
        // 给一个不存在的 binding 设值 → SetTexture 应 no-op。
        inst.SetTexture(99, AssetHandle<TextureAsset>{9999});

        assert(inst.GetTextureBinding(0).IsValid());
        assert(inst.GetTextureBinding(0).Value() == 1234);
        assert(inst.GetTextureBinding(3).Value() == 5678);
        assert(!inst.GetTextureBinding(99).IsValid()); // 仍为无效

        std::fprintf(stdout, "  [PASS] MaterialInstance texture binding round-trip\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[MaterialSchemaTest] running\n");

    AssetRegistry registry;
    auto          reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    if (reg.IsErr())
    {
        std::fprintf(stderr,
                     "[MaterialSchemaTest] RegisterLoader<ShaderAsset> failed (code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
        return 1;
    }

    TestLoadTexturedDescriptor(registry);
    TestUniformRoundTrip();
    TestUniformTypeMismatchAndMissing();
    TestTextureRoundTrip();

    std::fprintf(stdout, "[MaterialSchemaTest] all tests passed.\n");
    return 0;
}
