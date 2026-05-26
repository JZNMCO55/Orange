// LightComponent / ShadowConfig / BuiltinShadowShaders 单元测试。Phase 3 /
// Task 05 仅交付公共面 + 着色侧；Pipeline 真接 shadow pass 推到 Task 07。
//
// 4 条路径：
//   1. DirectionalLight 默认构造字段合理；
//   2. ShadowConfig 默认值合理 + 字段独立可改不串；
//   3. 通过 World::AddComponent 挂 DirectionalLight、HasComponent 命中、
//      GetComponent 取出后字段一致——验证 component 真可用作 ECS
//      component；
//   4. BuiltinShadowShaders::LoadShadowCaster 返回 vertex / fragment 都
//      有效；重复调用 dedup 命中（与 BuiltinMaterials 同语义）。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/render/BuiltinShadowShaders.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/ShadowConfig.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <glm/geometric.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Render::ComputeDirectionalLightWorldDir;
using Orange::Engine::Render::ComputeSpotLightWorldDir;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MakeDirectionalLightRotationFromDir;
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Render::SpotLight;
namespace BuiltinShadowShaders = Orange::Engine::Render::BuiltinShadowShaders;

namespace
{

// 1. DirectionalLight 默认值合理
//
// 方向字段已搬到 entity 的 TransformComponent.rotation —— 此测试只覆盖
// component 本身的默认 color / intensity / castsShadow；方向的默认（光向
// -Y）由 LightComponent.h 的 kDirectionalLightLocalForward 独立校验。
void TestDirectionalLightDefaults()
{
    DirectionalLight light;

    // 默认白光、强度 1、不投影
    assert(light.color.r == 1.0f);
    assert(light.color.g == 1.0f);
    assert(light.color.b == 1.0f);
    assert(light.intensity == 1.0f);
    assert(light.castsShadow == false);

    // identity rotation 派生的方向 = kDirectionalLightLocalForward = (0,-1,0)
    const glm::quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 dir = ComputeDirectionalLightWorldDir(identity);
    assert(std::abs(dir.x - 0.0f) < 1e-5f);
    assert(std::abs(dir.y - (-1.0f)) < 1e-5f);
    assert(std::abs(dir.z - 0.0f) < 1e-5f);

    std::fprintf(stdout, "  [PASS] DirectionalLight 默认字段 + identity rotation 派生方向\n");
}

// 2. ShadowConfig 默认值合理且字段独立
void TestShadowConfigDefaults()
{
    ShadowConfig cfg;

    assert(cfg.mapResolution   == 1024u);
    assert(cfg.pcfKernelRadius == 1u);
    assert(cfg.depthBias       == 0.005f);
    assert(cfg.normalBias      == 0.01f);

    // 改一个字段不影响其它
    cfg.mapResolution = 2048;
    assert(cfg.mapResolution   == 2048u);
    assert(cfg.pcfKernelRadius == 1u);
    assert(cfg.depthBias       == 0.005f);
    assert(cfg.normalBias      == 0.01f);

    cfg.pcfKernelRadius = 2;
    cfg.depthBias       = 0.01f;
    assert(cfg.mapResolution   == 2048u);
    assert(cfg.pcfKernelRadius == 2u);
    assert(cfg.depthBias       == 0.01f);
    assert(cfg.normalBias      == 0.01f);

    std::fprintf(stdout, "  [PASS] ShadowConfig 默认值与独立字段\n");
}

// 3. DirectionalLight 真可用作 ECS component
//
// 方向由 entity Transform.rotation 派生 —— 本测试覆盖 (a) component 字段
// round-trip、(b) MakeDirectionalLightRotationFromDir →
// ComputeDirectionalLightWorldDir 反推回原方向（数值精度内）。
void TestDirectionalLightAsEcsComponent()
{
    World world;
    Entity e = world.CreateEntity();

    DirectionalLight light;
    light.color       = glm::vec3{1.0f, 0.8f, 0.6f};   // 暖光
    light.intensity   = 1.5f;
    light.castsShadow = true;

    world.AddComponent<DirectionalLight>(e, light);
    assert(world.HasComponent<DirectionalLight>(e));

    const DirectionalLight* readBack = world.GetComponent<DirectionalLight>(e);
    assert(readBack != nullptr);
    assert(readBack->color.r     == 1.0f);
    assert(readBack->color.g     == 0.8f);
    assert(readBack->color.b     == 0.6f);
    assert(readBack->intensity   == 1.5f);
    assert(readBack->castsShadow == true);

    // 方向 round-trip：把世界方向编入 rotation，再从 rotation 派生回方向。
    const glm::vec3 desired = glm::vec3{0.0f, -1.0f, 0.0f};
    const glm::quat rot     = MakeDirectionalLightRotationFromDir(desired);
    const glm::vec3 derived = ComputeDirectionalLightWorldDir(rot);
    assert(std::abs(derived.x - desired.x) < 1e-5f);
    assert(std::abs(derived.y - desired.y) < 1e-5f);
    assert(std::abs(derived.z - desired.z) < 1e-5f);

    std::fprintf(stdout, "  [PASS] DirectionalLight 作为 ECS component + rotation 派生方向\n");
}

// 4. BuiltinShadowShaders::LoadShadowCaster + dedup
void TestLoadShadowCaster()
{
    AssetRegistry registry;
    auto reg = registry.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
    assert(reg.IsOk());

    auto pair = BuiltinShadowShaders::LoadShadowCaster(registry);
    assert(pair.vertex.IsValid());
    assert(pair.fragment.IsValid());

    // 同 path 重复 Load → AssetRegistry dedup 缓存命中，handle 沿用
    auto pair2 = BuiltinShadowShaders::LoadShadowCaster(registry);
    assert(pair.vertex.Value()   == pair2.vertex.Value());
    assert(pair.fragment.Value() == pair2.fragment.Value());

    std::fprintf(stdout, "  [PASS] LoadShadowCaster + dedup\n");
}

// 5. SpotLight 默认值合理 + identity rotation 派生方向
void TestSpotLightDefaults()
{
    SpotLight light;

    assert(light.color.r == 1.0f);
    assert(light.color.g == 1.0f);
    assert(light.color.b == 1.0f);
    assert(light.intensity == 1.0f);
    assert(light.range == 15.0f);
    assert(light.innerConeAngle < light.outerConeAngle);  // 内锥 ≤ 外锥
    assert(light.castsShadow == false);

    // identity rotation 派生的锥光方向 = kSpotLightLocalForward = (0,-1,0)
    const glm::quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 dir = ComputeSpotLightWorldDir(identity);
    assert(std::abs(dir.x - 0.0f) < 1e-5f);
    assert(std::abs(dir.y - (-1.0f)) < 1e-5f);
    assert(std::abs(dir.z - 0.0f) < 1e-5f);

    std::fprintf(stdout, "  [PASS] SpotLight 默认字段 + identity rotation 派生方向\n");
}

// 6. SpotLight 真可用作 ECS component（字段 round-trip）
void TestSpotLightAsEcsComponent()
{
    World world;
    Entity e = world.CreateEntity();

    SpotLight light;
    light.color          = glm::vec3{0.2f, 0.6f, 1.0f};  // 冷蓝聚光
    light.intensity      = 3.0f;
    light.range          = 22.0f;
    light.innerConeAngle = 0.25f;
    light.outerConeAngle = 0.40f;
    light.castsShadow    = true;

    world.AddComponent<SpotLight>(e, light);
    assert(world.HasComponent<SpotLight>(e));

    const SpotLight* readBack = world.GetComponent<SpotLight>(e);
    assert(readBack != nullptr);
    assert(readBack->color.b        == 1.0f);
    assert(readBack->intensity      == 3.0f);
    assert(readBack->range          == 22.0f);
    assert(readBack->innerConeAngle == 0.25f);
    assert(readBack->outerConeAngle == 0.40f);
    assert(readBack->castsShadow    == true);

    std::fprintf(stdout, "  [PASS] SpotLight 作为 ECS component round-trip\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[LightAndShadowTest] running\n");

    TestDirectionalLightDefaults();
    TestShadowConfigDefaults();
    TestDirectionalLightAsEcsComponent();
    TestLoadShadowCaster();
    TestSpotLightDefaults();
    TestSpotLightAsEcsComponent();

    std::fprintf(stdout, "[LightAndShadowTest] all tests passed.\n");
    return 0;
}
