// Render 公共接口的最小单元测试。
//
// Pipeline 当前是 no-op 占位（Task 06 / 07 才接通），所以本测试只覆
// 盖：
//   * Camera 的两个工厂——投影矩阵的关键元素位置正确（Y-flip、z 0..1）
//   * RenderableComponent 在 World 里 add / get / remove 一遍，确认
//     handle 字段值能往返传递、visible 默认 true
//   * Pipeline::Render(world) 不崩、可与空 World / 多组件 World 共
//     存（占位语义验证）

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cmath>
#include <cstdio>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::RenderableComponent;

namespace
{

bool ApproxEqual(float a, float b) noexcept
{
    return std::fabs(a - b) < 1e-5f;
}

void TestCameraOrthographic()
{
    // 单位立方 ortho：[-1,1] x [-1,1] x [0,1]（z 已是 Vulkan-style）。
    Camera cam = Camera::Orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f);

    // 缩放对角元素：x 1.0、y -1.0（Y-flip）、z = 1/(near-far) = -1。
    assert(ApproxEqual(cam.projection[0][0], 1.0f));
    assert(ApproxEqual(cam.projection[1][1], -1.0f));
    assert(ApproxEqual(cam.projection[2][2], -1.0f));

    // 平移：left/right/bottom/top 对称 + zNear=0 → 平移项全为 0。
    assert(ApproxEqual(cam.projection[3][0], 0.0f));
    assert(ApproxEqual(cam.projection[3][1], 0.0f));
    assert(ApproxEqual(cam.projection[3][2], 0.0f));

    std::fprintf(stdout, "  [PASS] Camera::Orthographic\n");
}

void TestCameraPerspective()
{
    // fov=90°、aspect=1、near=0.1、far=100。
    constexpr float kPi = 3.14159265358979323846f;
    Camera cam = Camera::Perspective(kPi * 0.5f, 1.0f, 0.1f, 100.0f);

    // f = 1 / tan(45°) = 1。所以 [0][0]=1、[1][1]=-1（Y-flip）。
    assert(ApproxEqual(cam.projection[0][0], 1.0f));
    assert(ApproxEqual(cam.projection[1][1], -1.0f));

    // [2][3] 必须是 -1（让 w 等于 -view-z）。
    assert(ApproxEqual(cam.projection[2][3], -1.0f));

    // [2][2] = far / (near - far) = 100 / -99.9 ≈ -1.001
    assert(ApproxEqual(cam.projection[2][2], 100.0f / (0.1f - 100.0f)));

    std::fprintf(stdout, "  [PASS] Camera::Perspective\n");
}

void TestRenderableComponentInWorld()
{
    World world;
    Entity e = world.CreateEntity();

    RenderableComponent r;
    r.mesh             = AssetHandle<MeshAsset>{42};
    r.materialInstance = nullptr;  // 显式 nullptr 表示走 Pipeline fallback
    r.visible          = false;

    world.AddComponent(e, r);
    auto* fetched = world.GetComponent<RenderableComponent>(e);
    assert(fetched != nullptr);
    assert(fetched->mesh.Value() == 42);
    assert(fetched->materialInstance == nullptr);
    assert(fetched->visible == false);

    // 默认构造的 visible 应为 true，materialInstance 应为 nullptr。
    Entity e2 = world.CreateEntity();
    world.AddComponent(e2, RenderableComponent{});
    auto* defaulted = world.GetComponent<RenderableComponent>(e2);
    assert(defaulted->visible == true);
    assert(!defaulted->mesh.IsValid());
    assert(defaulted->materialInstance == nullptr);

    world.RemoveComponent<RenderableComponent>(e);
    assert(world.GetComponent<RenderableComponent>(e) == nullptr);

    std::fprintf(stdout, "  [PASS] RenderableComponent CRUD\n");
}

void TestPipelinePlaceholderRender()
{
    Pipeline pipeline;
    World empty;
    pipeline.Render(empty);  // 空 World 不应崩

    World world;
    Entity e = world.CreateEntity();
    world.AddComponent(e, RenderableComponent{});
    pipeline.Render(world);  // 有可渲染组件的 World 也不应崩
    pipeline.Render(world);  // 重复调用稳定

    std::fprintf(stdout, "  [PASS] Pipeline::Render placeholder\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[RenderInterfaceTest] running\n");
    TestCameraOrthographic();
    TestCameraPerspective();
    TestRenderableComponentInWorld();
    TestPipelinePlaceholderRender();
    std::fprintf(stdout, "[RenderInterfaceTest] all tests passed.\n");
    return 0;
}
