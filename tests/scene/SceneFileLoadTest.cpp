// 仓内 .scene.json 资产经真实 Scene::Load 解析的冒烟测试。
//
// 重点验证 assets/scenes/light_family_shadows.scene.json（GAP-2026-05-26 光源
// 族演示场景）能被引擎真实加载路径解析，且三种光源组件（Directional /
// Spot / Point）+ 关键字段都被正确反序列化 —— 即编辑器 File>Open 打开它时
// 不会因 schema / 组件键不匹配而失败。
//
// 不渲染、不需要 Vulkan：光源都是 PureData 组件，无 GPU / 资产依赖；
// Renderable 的 mesh / material 缺失时 Scene::Load 走 graceful 留空路径，不
// 阻断加载，故本测试只关心组件解析正确性。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using Orange::Engine::World;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::PointLight;
using Orange::Engine::Render::SpotLight;
namespace Scene = Orange::Engine::Scene;

namespace
{

    bool FloatEq(float a, float b)
    {
        return std::fabs(a - b) < 1e-4f;
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[SceneFileLoadTest] running\n");

    const std::string scenePath =
        std::string(ORANGE_ENGINE_REPO_ASSETS_DIR) + "/scenes/light_family_shadows.scene.json";

    World         world;
    AssetRegistry assets; // mesh/material 缺 loader → Renderable graceful 留空，不阻断

    Scene::LoadOptions opt;
    opt.assetRegistry = &assets;
    auto rc           = Scene::Load(scenePath, world, opt);
    if (rc.IsErr())
    {
        std::fprintf(stderr, "[SceneFileLoadTest] Scene::Load 失败 (code=%u) path=%s\n",
                     static_cast<unsigned>(rc.Error()), scenePath.c_str());
        return 1;
    }

    auto& reg = world.Registry();

    // 三种光源各恰好 1 盏。
    std::uint32_t nDir = 0, nSpot = 0, nPoint = 0;
    for (auto e : reg.view<DirectionalLight>())
    {
        (void)e;
        ++nDir;
    }
    for (auto e : reg.view<SpotLight>())
    {
        (void)e;
        ++nSpot;
    }
    for (auto e : reg.view<PointLight>())
    {
        (void)e;
        ++nPoint;
    }
    std::fprintf(stderr, "  lights: directional=%u spot=%u point=%u\n", nDir, nSpot, nPoint);
    assert(nDir == 1 && "DirectionalLight 数量不符");
    assert(nSpot == 1 && "SpotLight 数量不符 —— 组件键 / schema 解析失败？");
    assert(nPoint == 1 && "PointLight 数量不符");

    // SpotLight 关键字段 round-trip（与 scene 文件值一致）。
    {
        auto        view = reg.view<SpotLight>();
        const auto& sl   = view.get<SpotLight>(view.front());
        assert(sl.castsShadow == true);
        assert(FloatEq(sl.range, 14.0f));
        assert(FloatEq(sl.innerConeAngle, 0.30f));
        assert(FloatEq(sl.outerConeAngle, 0.45f));
        assert(FloatEq(sl.color.b, 1.0f));
        std::fprintf(stderr, "  [PASS] SpotLight 字段 round-trip\n");
    }

    // PointLight 关键字段。
    {
        auto        view = reg.view<PointLight>();
        const auto& pl   = view.get<PointLight>(view.front());
        assert(pl.castsShadow == true);
        assert(FloatEq(pl.range, 10.0f));
        assert(FloatEq(pl.intensity, 45.0f));
        std::fprintf(stderr, "  [PASS] PointLight 字段 round-trip\n");
    }

    // DirectionalLight castsShadow（direction 字段经 migrator 转 Transform.rotation）。
    {
        auto        view = reg.view<DirectionalLight>();
        const auto& dl   = view.get<DirectionalLight>(view.front());
        assert(dl.castsShadow == true);
        std::fprintf(stderr, "  [PASS] DirectionalLight 字段\n");
    }

    std::fprintf(stdout, "[SceneFileLoadTest] all checks passed.\n");
    return 0;
}
