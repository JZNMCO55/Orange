// RenderScene 实现：单帧 World → drawable 收集。
//
// 直接走 entt::registry 的 view：
//   * Camera：取 view<Camera>() 的第一个；多相机场景的"哪个是 main"
//     语义留给后续（届时引入 ActiveCameraTag 之类的 marker component
//     来做选择，比 view 上的 first 更可靠）。
//   * Drawable：view<TransformComponent, RenderableComponent>()，对每
//     个命中实体把 TRS 合成 world matrix、过滤 visible=false。
//
// **Hierarchy 父子链的世界变换合成**（ADR-016 / maturity-roadmap A1.1 step 2）：
// Collect 顶部先跑 `Scene::PropagateWorldTransforms` 从 hierarchy 自顶向下累积
// 每个 entity 的 world matrix 进 WorldTransformComponent（cache），drawable loop
// 读它而非各自单实体合成——让 parenting 真正生效（移动父带动子）。flat / 原点
// 父的 entity 累积结果 == 单实体 local（零行为变化），只有非原点父的子才被父
// 变换带动。cache 缺失时退回单实体 local 兜底。

#include "orange/engine/render/RenderScene.h"

#include "orange/engine/render/Camera.h"
#include "orange/engine/render/RenderableComponent.h"
#include "orange/engine/render/SubMeshMaterialsComponent.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/TransformMath.h"  // ComposeLocalMatrix（单一真相源）
#include "orange/engine/scene/TransformSystem.h"
#include "orange/engine/scene/World.h"
#include "orange/engine/scene/WorldPartition.h"
#include "orange/engine/scene/WorldTransformComponent.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Orange::Engine::Render
{

void RenderScene::Clear() noexcept
{
    mHasCamera = false;
    mCamera    = Camera{};
    mDrawables.clear();
}

void RenderScene::Collect(const Orange::Engine::World& world,
                          const Orange::Engine::Scene::WorldPartition* partition)
{
    // entt::view 迭代 + 下面的 transform 传播都需要 mutable World——做一次内部
    // const_cast。RenderScene::Collect 对调用方的契约从"World 不被修改"放宽为
    // "只写 WorldTransformComponent 派生 cache（ADR-016）"——其余 component 仍只读。
    auto& mutableWorld = const_cast<Orange::Engine::World&>(world);
    auto& registry     = mutableWorld.Registry();

    // ADR-016：先从 hierarchy 自顶向下累积 world matrix 进各 entity 的
    // WorldTransformComponent。每帧全量重算（方案 A）；放在 Collect 顶部让所有
    // render 路径（window / offscreen / thumbnail）自动获得最新 world transform。
    Orange::Engine::Scene::PropagateWorldTransforms(mutableWorld);

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
    // partition 非空时再加一层 layer.visible 过滤——隐藏 layer 上的
    // entity 不进 drawable 列表，自然也不进 shadow pass。
    auto drawView = registry.view<Scene::TransformComponent, RenderableComponent>();
    for (auto e : drawView)
    {
        const auto& renderable = drawView.get<RenderableComponent>(e);
        if (!renderable.visible)
        {
            continue;
        }
        const auto entity = Orange::Engine::World::FromEntt(e);
        if (partition != nullptr && !partition->IsEntityVisible(world, entity))
        {
            continue;
        }
        const auto& xform = drawView.get<Scene::TransformComponent>(e);

        Drawable d;
        // world matrix 取 TransformSystem 累积的 cache（含父变换，ADR-016）；
        // PropagateWorldTransforms 已给每个 reachable entity 填好，cache 缺失
        // （hierarchy 链不一致等罕见情况）时退回单实体 local 合成兜底。
        const auto* wt = world.GetComponent<Scene::WorldTransformComponent>(entity);
        d.worldMatrix      = (wt != nullptr) ? wt->world : Scene::ComposeLocalMatrix(xform);
        d.mesh             = renderable.mesh;
        d.materialInstance = renderable.materialInstance;
        d.castsShadow      = renderable.castsShadow;
        // 可选的 SubMeshMaterialsComponent：有则把 slot → material 列表拷进
        // drawable（单 mesh 多 material）。没挂该组件的 entity 保持空列表，
        // 渲染端走整 mesh 单 material 路径。
        if (const auto* subMats = world.GetComponent<SubMeshMaterialsComponent>(entity))
        {
            d.subMeshMaterials = subMats->slots;
        }
        mDrawables.emplace_back(std::move(d));
    }
}

}  // namespace Orange::Engine::Render
