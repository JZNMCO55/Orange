// RenderScene::Collect 的端到端单元测试。
//
// 不接 OrangeRender 也不画像素——这一层负责的就是"World → drawable
// 列表"的纯数据转换，全部测项都是构造一个 World、跑一次 Collect、
// 检查 mDrawables / mCamera 状态。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/RenderScene.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cmath>
#include <cstdio>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::Drawable;
using Orange::Engine::Render::Material;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::RenderScene;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    bool ApproxEqual(float a, float b) noexcept
    {
        return std::fabs(a - b) < 1e-5f;
    }

    void TestEmptyWorld()
    {
        World       world;
        RenderScene scene;
        scene.Collect(world);

        assert(!scene.HasCamera());
        assert(scene.Empty());
        assert(scene.DrawableCount() == 0);

        std::fprintf(stdout, "  [PASS] empty world\n");
    }

    void TestCameraOnly()
    {
        World  world;
        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));

        RenderScene scene;
        scene.Collect(world);

        assert(scene.HasCamera());
        assert(scene.DrawableCount() == 0);
        // Y-flip 由工厂内置：[1][1] 应为 -1。
        assert(ApproxEqual(scene.MainCamera().projection[1][1], -1.0f));

        std::fprintf(stdout, "  [PASS] camera-only world\n");
    }

    void TestCameraAndRenderables()
    {
        World world;

        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));

        // 三个可渲染实体：一个完全 default、一个改 mesh handle、一个 visible=false。
        Entity a = world.CreateEntity();
        world.AddComponent(a, TransformComponent{});
        world.AddComponent(a, RenderableComponent{});

        // 给 b 关一个真实的 MaterialInstance（绑空 Material 也行——Drawable
        // 收集只透传指针，不解引用）；用栈上 dummy material + instance 让
        // Drawable.materialInstance 拿到非 null 指针、可以在循环里识别。
        Material         bMaterial;
        MaterialInstance bInstance(&bMaterial);

        Entity b = world.CreateEntity();
        world.AddComponent(b, TransformComponent{});
        RenderableComponent br;
        br.mesh             = AssetHandle<MeshAsset>{42};
        br.materialInstance = &bInstance;
        world.AddComponent(b, br);

        Entity c = world.CreateEntity();
        world.AddComponent(c, TransformComponent{});
        RenderableComponent cr;
        cr.visible = false;
        world.AddComponent(c, cr);

        // 还有一个只有 RenderableComponent、没 Transform——不应进 drawable。
        Entity d = world.CreateEntity();
        world.AddComponent(d, RenderableComponent{});

        RenderScene scene;
        scene.Collect(world);

        assert(scene.HasCamera());
        assert(scene.DrawableCount() == 2); // a + b，c (invisible) / d (no transform) 被滤掉

        // 找到 b 对应的 drawable（通过 mesh handle 值）；
        // Drawable.materialInstance 应被 Collect 透传过来，与 RenderableComponent
        // 的指针字面相等。a 那条 drawable 的 materialInstance 应仍为 nullptr。
        bool foundB = false;
        bool foundA = false;
        for (const Drawable& dw : scene.Drawables())
        {
            if (dw.mesh.Value() == 42)
            {
                assert(dw.materialInstance == &bInstance);
                foundB = true;
            }
            else
            {
                assert(dw.materialInstance == nullptr);
                foundA = true;
            }
        }
        assert(foundB);
        assert(foundA);

        std::fprintf(stdout, "  [PASS] camera + filtered renderables\n");
    }

    void TestWorldMatrixComposition()
    {
        World  world;
        Entity e = world.CreateEntity();

        TransformComponent t;
        t.position = {3.0f, 4.0f, 5.0f};
        t.scale    = {2.0f, 2.0f, 2.0f};
        // rotation 默认 (1,0,0,0) = identity quaternion
        world.AddComponent(e, t);
        world.AddComponent(e, RenderableComponent{});

        RenderScene scene;
        scene.Collect(world);
        assert(scene.DrawableCount() == 1);

        const auto& m = scene.Drawables()[0].worldMatrix;

        // 列向量惯例下 m[3] 是平移列，应等于 (3, 4, 5, 1)。
        assert(ApproxEqual(m[3][0], 3.0f));
        assert(ApproxEqual(m[3][1], 4.0f));
        assert(ApproxEqual(m[3][2], 5.0f));
        assert(ApproxEqual(m[3][3], 1.0f));

        // 对角缩放：rotation 是 identity，所以 m[0][0]/m[1][1]/m[2][2] 等
        // 于 scale 各分量。
        assert(ApproxEqual(m[0][0], 2.0f));
        assert(ApproxEqual(m[1][1], 2.0f));
        assert(ApproxEqual(m[2][2], 2.0f));

        std::fprintf(stdout, "  [PASS] TRS world matrix composition\n");
    }

    void TestClearAndRecollect()
    {
        World  world;
        Entity camE = world.CreateEntity();
        world.AddComponent(camE, Camera::Orthographic(-1, 1, -1, 1, 0, 1));
        Entity a = world.CreateEntity();
        world.AddComponent(a, TransformComponent{});
        world.AddComponent(a, RenderableComponent{});

        RenderScene scene;
        scene.Collect(world);
        assert(scene.HasCamera());
        assert(scene.DrawableCount() == 1);

        // Clear 完之后再 Collect 一个空 World：状态应彻底回退。
        World empty;
        scene.Clear();
        scene.Collect(empty);
        assert(!scene.HasCamera());
        assert(scene.Empty());

        // 多次 Collect 同一非空 World 不应叠加 drawable。
        scene.Clear();
        scene.Collect(world);
        scene.Clear();
        scene.Collect(world);
        assert(scene.DrawableCount() == 1);

        std::fprintf(stdout, "  [PASS] clear + recollect\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[RenderSceneTest] running\n");
    TestEmptyWorld();
    TestCameraOnly();
    TestCameraAndRenderables();
    TestWorldMatrixComposition();
    TestClearAndRecollect();
    std::fprintf(stdout, "[RenderSceneTest] all tests passed.\n");
    return 0;
}
