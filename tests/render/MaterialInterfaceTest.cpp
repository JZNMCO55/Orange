// Material / MaterialInstance 公共接口的最小单元测试。
//
// 覆盖 6 条路径：
//   1. Material 默认构造的 uniforms / textureSlots 为空
//   2. Material 显式构造（含 uniform + texture 槽）后字段读回正确
//   3. MaterialInstance 绑定 Material：GetMaterial 正确，has-overrides 全 false
//   4. SetUniform 命中已声明 + type 匹配的 name → HasUniformOverride = true
//   5. SetUniform 喂未声明 name 或类型错配 → 仍是 no-op（HasUniformOverride 仍 false）
//   6. SetTexture 命中已声明 binding → HasTextureOverride = true；
//      未声明 binding → no-op；MaterialInstance 移动构造保留 override

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialTypes.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::TextureAsset;
using Orange::Engine::Render::Material;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialTextureSlotDesc;
using Orange::Engine::Render::MaterialUniformDesc;
using Orange::Engine::Render::MaterialUniformType;

namespace
{

    void TestMaterialDefaultEmpty()
    {
        Material m;
        assert(m.name.empty());
        assert(!m.vertexShader.IsValid());
        assert(!m.fragmentShader.IsValid());
        assert(m.uniforms.empty());
        assert(m.textureSlots.empty());

        std::fprintf(stdout, "  [PASS] Material default-constructed empty\n");
    }

    void TestMaterialExplicitFields()
    {
        Material m;
        m.name           = "toon";
        m.vertexShader   = AssetHandle<ShaderAsset>{11};
        m.fragmentShader = AssetHandle<ShaderAsset>{12};
        m.uniforms.push_back({"uTime", MaterialUniformType::Float});
        m.uniforms.push_back({"uRimColor", MaterialUniformType::Vec3});
        m.textureSlots.push_back({0, "uAlbedo"});

        assert(m.name == "toon");
        assert(m.vertexShader.Value() == 11);
        assert(m.fragmentShader.Value() == 12);
        assert(m.uniforms.size() == 2);
        assert(m.uniforms[0].name == "uTime");
        assert(m.uniforms[0].type == MaterialUniformType::Float);
        assert(m.uniforms[1].type == MaterialUniformType::Vec3);
        assert(m.textureSlots.size() == 1);
        assert(m.textureSlots[0].binding == 0);
        assert(m.textureSlots[0].name == "uAlbedo");

        std::fprintf(stdout, "  [PASS] Material explicit fields\n");
    }

    void TestInstanceBindsMaterial()
    {
        Material m;
        m.uniforms.push_back({"uTime", MaterialUniformType::Float});
        m.textureSlots.push_back({3, "uNoise"});

        MaterialInstance inst(&m);
        assert(inst.GetMaterial() == &m);
        assert(!inst.HasUniformOverride("uTime"));
        assert(!inst.HasTextureOverride(3));

        // null 退化态：所有 SetXxx / has-query 仍可调用
        MaterialInstance nullInst(nullptr);
        assert(nullInst.GetMaterial() == nullptr);
        nullInst.SetUniform("anything", 1.0f);
        assert(!nullInst.HasUniformOverride("anything"));

        std::fprintf(stdout, "  [PASS] MaterialInstance binds Material\n");
    }

    void TestSetUniformHit()
    {
        Material m;
        m.uniforms.push_back({"uTime", MaterialUniformType::Float});
        m.uniforms.push_back({"uRimColor", MaterialUniformType::Vec3});
        m.uniforms.push_back({"uMVP", MaterialUniformType::Mat4});

        MaterialInstance inst(&m);
        inst.SetUniform("uTime", 1.5f);
        inst.SetUniform("uRimColor", glm::vec3(0.4f, 0.8f, 1.0f));
        inst.SetUniform("uMVP", glm::mat4(1.0f));

        assert(inst.HasUniformOverride("uTime"));
        assert(inst.HasUniformOverride("uRimColor"));
        assert(inst.HasUniformOverride("uMVP"));

        std::fprintf(stdout, "  [PASS] SetUniform hits declared name + type\n");
    }

    void TestSetUniformMissAndMismatch()
    {
        Material m;
        m.uniforms.push_back({"uTime", MaterialUniformType::Float});

        MaterialInstance inst(&m);

        // 未声明 name → no-op
        inst.SetUniform("uUnknown", 1.0f);
        assert(!inst.HasUniformOverride("uUnknown"));

        // 已声明但 type 错（uTime 是 Float，给 vec3）→ no-op
        inst.SetUniform("uTime", glm::vec3(0.0f));
        assert(!inst.HasUniformOverride("uTime"));

        // 同 name 用正确类型 → 命中
        inst.SetUniform("uTime", 0.7f);
        assert(inst.HasUniformOverride("uTime"));

        std::fprintf(stdout, "  [PASS] SetUniform miss / type mismatch is no-op\n");
    }

    void TestSetTextureAndMove()
    {
        Material m;
        m.textureSlots.push_back({0, "uAlbedo"});
        m.textureSlots.push_back({2, "uNormal"});

        MaterialInstance inst(&m);

        // 未声明 binding → no-op
        inst.SetTexture(7, AssetHandle<TextureAsset>{99});
        assert(!inst.HasTextureOverride(7));

        // 已声明 binding → 命中
        inst.SetTexture(0, AssetHandle<TextureAsset>{42});
        inst.SetTexture(2, AssetHandle<TextureAsset>{43});
        assert(inst.HasTextureOverride(0));
        assert(inst.HasTextureOverride(2));

        // 移动构造：override 状态保留
        MaterialInstance moved(std::move(inst));
        assert(moved.GetMaterial() == &m);
        assert(moved.HasTextureOverride(0));
        assert(moved.HasTextureOverride(2));

        std::fprintf(stdout, "  [PASS] SetTexture + move-construction preserves overrides\n");
    }

    // 枚举 override API（GAP-2026-05-16 G2）：
    //   * 空 instance → 三个 enumerate 都返回空
    //   * 设置 3 个 uniform + 2 个 texture override → 列表长度匹配 + 内容匹配
    //   * GetUniformOverrideType 命中返回正确类型；未命中 nullopt
    void TestEnumerateOverrides()
    {
        Material m;
        m.uniforms.push_back({"uTime", MaterialUniformType::Float});
        m.uniforms.push_back({"uRimColor", MaterialUniformType::Vec3});
        m.uniforms.push_back({"uMVP", MaterialUniformType::Mat4});
        m.textureSlots.push_back({0, "uAlbedo"});
        m.textureSlots.push_back({2, "uNormal"});

        MaterialInstance inst(&m);

        // 空 instance：三个 enumerate 全空
        assert(inst.GetUniformOverrideNames().empty());
        assert(inst.GetTextureOverrideBindings().empty());
        assert(!inst.GetUniformOverrideType("uTime").has_value());

        // 设置 override 后正确返回
        inst.SetUniform("uTime", 1.5f);
        inst.SetUniform("uRimColor", glm::vec3(0.4f, 0.8f, 1.0f));
        inst.SetUniform("uMVP", glm::mat4(1.0f));
        inst.SetTexture(0, AssetHandle<TextureAsset>{42});
        inst.SetTexture(2, AssetHandle<TextureAsset>{43});

        std::vector<std::string> uniformNames = inst.GetUniformOverrideNames();
        assert(uniformNames.size() == 3);
        std::sort(uniformNames.begin(), uniformNames.end());
        assert(uniformNames[0] == "uMVP");
        assert(uniformNames[1] == "uRimColor");
        assert(uniformNames[2] == "uTime");

        std::vector<std::uint32_t> bindings = inst.GetTextureOverrideBindings();
        assert(bindings.size() == 2);
        std::sort(bindings.begin(), bindings.end());
        assert(bindings[0] == 0u);
        assert(bindings[1] == 2u);

        // 类型查询
        auto t1 = inst.GetUniformOverrideType("uTime");
        assert(t1.has_value() && *t1 == MaterialUniformType::Float);
        auto t2 = inst.GetUniformOverrideType("uRimColor");
        assert(t2.has_value() && *t2 == MaterialUniformType::Vec3);
        auto t3 = inst.GetUniformOverrideType("uMVP");
        assert(t3.has_value() && *t3 == MaterialUniformType::Mat4);
        auto tMiss = inst.GetUniformOverrideType("nope");
        assert(!tMiss.has_value());

        // null instance：enumerate 全空
        MaterialInstance nullInst(nullptr);
        assert(nullInst.GetUniformOverrideNames().empty());
        assert(nullInst.GetTextureOverrideBindings().empty());
        assert(!nullInst.GetUniformOverrideType("uTime").has_value());

        std::fprintf(stdout, "  [PASS] enumerate uniform / texture override\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[MaterialInterfaceTest] running\n");
    TestMaterialDefaultEmpty();
    TestMaterialExplicitFields();
    TestInstanceBindsMaterial();
    TestSetUniformHit();
    TestSetUniformMissAndMismatch();
    TestSetTextureAndMove();
    TestEnumerateOverrides();
    std::fprintf(stdout, "[MaterialInterfaceTest] all tests passed.\n");
    return 0;
}
