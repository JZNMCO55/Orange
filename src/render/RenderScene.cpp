// RenderScene 实现：单帧 World → drawable 收集。
//
// 直接走 entt::registry 的 view：
//   * Camera：取 view<Camera>() 的第一个；多相机场景的"哪个是 main"
//     语义留给后续（届时引入 ActiveCameraTag 之类的 marker component
//     来做选择，比 view 上的 first 更可靠）。
//   * Drawable：view<TransformComponent, RenderableComponent>()，对每
//     个命中实体把 TRS 合成 world matrix、过滤 visible=false。
//
// **未做的事**：Hierarchy 父子链的世界变换合成。Phase 2 sample 都是
// 单层实体，flat=local 即可；引入复合 matrix 路径会同时拉进"depth-
// first 还是按拓扑序"、"是否缓存计算结果"等设计问题，留给 Phase 4
// 关卡复杂化时一并解决。

#include "orange/engine/render/RenderScene.h"

#include "orange/engine/render/Camera.h"
#include "orange/engine/render/RenderableComponent.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Orange::Engine::Render
{
namespace
{

glm::mat4 ComposeWorldMatrix(const Scene::TransformComponent& xform) noexcept
{
    // 标准 TRS 合成：T * R * S，按列向量惯例对应 `worldVec = M *
    // localVec`。glm::translate / scale 走原矩阵基础上后乘；旋转用
    // mat4_cast(quat) 得到 4x4 旋转矩阵后参与乘法。
    glm::mat4 m(1.0f);
    m = glm::translate(m, xform.position);
    m = m * glm::mat4_cast(xform.rotation);
    m = glm::scale(m, xform.scale);
    return m;
}

}  // namespace

void RenderScene::Clear() noexcept
{
    mHasCamera = false;
    mCamera    = Camera{};
    mDrawables.clear();
}

void RenderScene::Collect(const Orange::Engine::World& world)
{
    // 取 const reference 看似一致，但 entt::view 的迭代需要 mutable
    // registry 引用——本实现保证仅读 component，所以做一次内部
    // const_cast 在语义上是安全的。RenderScene::Collect 的契约是
    // "World 不被修改"，对调用方来说签名 `const World&` 仍然成立。
    auto& registry = const_cast<Orange::Engine::World&>(world).Registry();

    // 主相机：取首个挂 Camera 组件的实体。后续若需要"指定主相机"语
    // 义，引入 ActiveCameraTag 之类的 marker component 即可。
    // 用 view::front() 显式拿首个；MSVC 对 `for + break` 偶发误报
    // C4702（unreachable code），写成早退分支更稳。
    auto cameraView = registry.view<Camera>();
    if (const auto camEntity = cameraView.front(); camEntity != entt::null)
    {
        mCamera    = cameraView.get<Camera>(camEntity);
        mHasCamera = true;
    }

    // Drawable：必须同时有 Transform + Renderable，且 visible=true。
    auto drawView = registry.view<Scene::TransformComponent, RenderableComponent>();
    for (auto e : drawView)
    {
        const auto& renderable = drawView.get<RenderableComponent>(e);
        if (!renderable.visible)
        {
            continue;
        }
        const auto& xform = drawView.get<Scene::TransformComponent>(e);

        Drawable d;
        d.worldMatrix = ComposeWorldMatrix(xform);
        d.mesh        = renderable.mesh;
        d.texture     = renderable.texture;
        mDrawables.emplace_back(d);
    }
}

}  // namespace Orange::Engine::Render
