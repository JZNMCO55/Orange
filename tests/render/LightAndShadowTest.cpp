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
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::ShadowConfig;
namespace BuiltinShadowShaders = Orange::Engine::Render::BuiltinShadowShaders;

namespace
{

// 1. DirectionalLight 默认值合理
void TestDirectionalLightDefaults()
{
    DirectionalLight light;

    // direction 朝下方斜射（y 分量 < 0）—— 卡通主光约定
    assert(light.direction.y < 0.0f);
    // 默认值大致是单位长度（不强制 1.0，因为 (0.3, -1.0, 0.4) 长度
    // ≈1.115；调用方按需 normalize）。这里只验证非零长度且合理量级。
    const float len = glm::length(light.direction);
    assert(len > 0.5f && len < 2.0f);

    // 默认白光、强度 1、不投影
    assert(light.color.r == 1.0f);
    assert(light.color.g == 1.0f);
    assert(light.color.b == 1.0f);
    assert(light.intensity == 1.0f);
    assert(light.castsShadow == false);

    std::fprintf(stdout, "  [PASS] DirectionalLight 默认字段\n");
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
void TestDirectionalLightAsEcsComponent()
{
    World world;
    Entity e = world.CreateEntity();

    DirectionalLight light;
    light.direction   = glm::vec3{0.0f, -1.0f, 0.0f};
    light.color       = glm::vec3{1.0f, 0.8f, 0.6f};   // 暖光
    light.intensity   = 1.5f;
    light.castsShadow = true;

    world.AddComponent<DirectionalLight>(e, light);
    assert(world.HasComponent<DirectionalLight>(e));

    const DirectionalLight* readBack = world.GetComponent<DirectionalLight>(e);
    assert(readBack != nullptr);
    assert(readBack->direction.y == -1.0f);
    assert(readBack->color.r     == 1.0f);
    assert(readBack->color.g     == 0.8f);
    assert(readBack->color.b     == 0.6f);
    assert(readBack->intensity   == 1.5f);
    assert(readBack->castsShadow == true);

    std::fprintf(stdout, "  [PASS] DirectionalLight 作为 ECS component\n");
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

}  // namespace

int main()
{
    std::fprintf(stdout, "[LightAndShadowTest] running\n");

    TestDirectionalLightDefaults();
    TestShadowConfigDefaults();
    TestDirectionalLightAsEcsComponent();
    TestLoadShadowCaster();

    std::fprintf(stdout, "[LightAndShadowTest] all tests passed.\n");
    return 0;
}
