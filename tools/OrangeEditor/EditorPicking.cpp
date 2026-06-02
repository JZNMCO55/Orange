#include "EditorPicking.h"

#include "EditorCameraControl.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace
{

// ---- world matrix 合成（与 src/render/RenderScene.cpp 同款）-------------
// 复述一遍以保证 picking 几何与引擎渲染语义对齐；引擎那一份是 private 头，
// 编辑器无法直接 include，按 invariant"渲染器以外的模块不引私有头"自包含。
glm::mat4
ComposeWorldMatrix(const Orange::Engine::Scene::TransformComponent& xform) noexcept
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), xform.position);
    m *= glm::mat4_cast(xform.rotation);
    m  = glm::scale(m, xform.scale);
    return m;
}

// ---- mesh local AABB（min / max of positions）---------------------------
// 空 mesh 返回退化 AABB（{0,0,0} → {0,0,0}）。caller 看到这个 AABB 也能做
// ray test——退化 AABB 是一个点，命中概率极低，等价于"该 entity 不可
// picking"。
struct LocalAABB { glm::vec3 min; glm::vec3 max; };

LocalAABB
ComputeMeshLocalAABB(const Orange::Engine::Asset::MeshAsset& mesh) noexcept
{
    const auto& positions = mesh.Positions();
    if (positions.empty())
    {
        return LocalAABB{glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (const auto& p : positions)
    {
        mn.x = std::min(mn.x, p.x);  mx.x = std::max(mx.x, p.x);
        mn.y = std::min(mn.y, p.y);  mx.y = std::max(mx.y, p.y);
        mn.z = std::min(mn.z, p.z);  mx.z = std::max(mx.z, p.z);
    }
    return LocalAABB{mn, mx};
}

// ---- local AABB → world AABB（8 角点变换取新 min/max）-------------------
// 这是粗 AABB —— 不是最贴合 OBB，但对 picking 已经足够（hit 后取最近 t，
// 误差在 mesh AABB 内不影响"哪个实体被选"的最终结论）。
LocalAABB
TransformAABB(const LocalAABB& local, const glm::mat4& worldMat) noexcept
{
    const glm::vec3 corners[8] = {
        {local.min.x, local.min.y, local.min.z},
        {local.max.x, local.min.y, local.min.z},
        {local.min.x, local.max.y, local.min.z},
        {local.max.x, local.max.y, local.min.z},
        {local.min.x, local.min.y, local.max.z},
        {local.max.x, local.min.y, local.max.z},
        {local.min.x, local.max.y, local.max.z},
        {local.max.x, local.max.y, local.max.z},
    };
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (const auto& c : corners)
    {
        const glm::vec3 w = glm::vec3(worldMat * glm::vec4(c, 1.0f));
        mn = glm::min(mn, w);
        mx = glm::max(mx, w);
    }
    return LocalAABB{mn, mx};
}

// ---- ray-AABB（slab 算法）-----------------------------------------------
// 返回命中 t（ray.origin + t * ray.dir），未命中返回 nullopt。
// ray 起点在 AABB 内 → tMin < 0：取 tMax（射线沿 dir 出 AABB 的远端），
// 仍是有效正 t；只有起点在 AABB 内且 tMax < 0（ray 反向远离 AABB）才不命中。
std::optional<float>
RayAABBIntersect(const glm::vec3& origin, const glm::vec3& dir,
                 const glm::vec3& aabbMin, const glm::vec3& aabbMax) noexcept
{
    constexpr float kEpsilon = 1e-6f;
    float tMin = -std::numeric_limits<float>::infinity();
    float tMax =  std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i)
    {
        if (std::abs(dir[i]) < kEpsilon)
        {
            // 与该轴平行；起点必须在 slab 内才有命中机会
            if (origin[i] < aabbMin[i] || origin[i] > aabbMax[i])
            {
                return std::nullopt;
            }
        }
        else
        {
            const float invD = 1.0f / dir[i];
            float t1 = (aabbMin[i] - origin[i]) * invD;
            float t2 = (aabbMax[i] - origin[i]) * invD;
            if (t1 > t2) { std::swap(t1, t2); }
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) { return std::nullopt; }
        }
    }
    // 起点在 AABB 内的情况：tMin < 0，但 tMax > 0，取 tMax 作为出口点
    const float t = (tMin >= 0.0f) ? tMin : tMax;
    if (t < 0.0f) { return std::nullopt; }
    return t;
}

}  // anonymous namespace

Orange::Engine::Entity
PickEntityAt(EditorHost& host, glm::vec2 ndc, float aspect)
{
    using Orange::Engine::Entity;
    if (host.scene.pWorld == nullptr) { return Entity::Invalid(); }

    // ---- 反投影：NDC → world ray --------------------------------------
    // 用 BuildEditorCamera 取当前轨道相机的 view + projection，与渲染路径
    // 同款矩阵——picking 几何与玩家看到的图像一致，不需要再独立同步状态。
    const auto cam     = BuildEditorCamera(host.camera, aspect);
    const glm::mat4 vp = cam.projection * cam.view;
    const glm::mat4 invVP = glm::inverse(vp);

    // Vulkan NDC z ∈ [0, 1]：0 = near，1 = far。
    const glm::vec4 nearH = invVP * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
    const glm::vec4 farH  = invVP * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);
    if (std::abs(nearH.w) < 1e-9f || std::abs(farH.w) < 1e-9f)
    {
        return Entity::Invalid();
    }
    const glm::vec3 origin = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPt  = glm::vec3(farH)  / farH.w;
    const glm::vec3 diff   = farPt - origin;
    const float     len    = glm::length(diff);
    if (len < 1e-6f) { return Entity::Invalid(); }
    const glm::vec3 dir = diff / len;

    // ---- 遍历 (Transform, Renderable) 实体，取 t 最小命中 ---------------
    auto& reg = host.scene.pWorld->Registry();
    auto  view = reg.view<Orange::Engine::Scene::TransformComponent,
                          Orange::Engine::Render::RenderableComponent>();

    Entity bestEntity = Entity::Invalid();
    float  bestT      = std::numeric_limits<float>::max();

    for (auto e : view)
    {
        const auto& xform = view.get<Orange::Engine::Scene::TransformComponent>(e);
        const auto& rc    = view.get<Orange::Engine::Render::RenderableComponent>(e);
        if (!rc.visible) { continue; }

        // MaterialInstance 是否绑定不影响 picking——只判几何是否存在。mesh
        // handle 拿不到 asset 时跳过该实体（典型：mesh 句柄失效 / 未加载）。
        if (host.assets.pAssets == nullptr) { continue; }
        const auto* pMesh = host.assets.pAssets->Get(rc.mesh);
        if (pMesh == nullptr) { continue; }
        if (pMesh->Empty()) { continue; }

        const LocalAABB localAABB = ComputeMeshLocalAABB(*pMesh);
        // world matrix 取 TransformSystem 累积的 cache（含父变换，ADR-016 / A1.1
        // step 2）—— 与 RenderScene::Collect 渲染用的同一份，让 picking 命中
        // parented mesh 的真实世界位置（之前 picking 测 local，非原点父的 mesh
        // 选不中）。cache 缺失（首帧 / 未渲染）退回单实体 local 兜底。
        const auto entity = Orange::Engine::World::FromEntt(e);
        const auto* wt =
            host.scene.pWorld->GetComponent<Orange::Engine::Scene::WorldTransformComponent>(entity);
        const glm::mat4 worldMat  = (wt != nullptr) ? wt->world : ComposeWorldMatrix(xform);
        const LocalAABB worldAABB = TransformAABB(localAABB, worldMat);

        const auto t = RayAABBIntersect(origin, dir, worldAABB.min, worldAABB.max);
        if (!t.has_value()) { continue; }
        if (*t < bestT)
        {
            bestT      = *t;
            bestEntity = entity;
        }
    }

    return bestEntity;
}

glm::vec3
ScreenRayToGround(EditorHost& host, glm::vec2 ndc, float aspect, float groundY)
{
    // 反投影 NDC → world ray（与 PickEntityAt 同款 invVP）。
    const auto cam     = BuildEditorCamera(host.camera, aspect);
    const glm::mat4 vp = cam.projection * cam.view;
    const glm::mat4 invVP = glm::inverse(vp);

    const glm::vec4 nearH = invVP * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
    const glm::vec4 farH  = invVP * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);

    // 退化兜底点：相机轨道中心（pivot）—— 反投影病态（w≈0）时用它。
    const glm::vec3 fallbackPivot = host.camera.pivot;
    if (std::abs(nearH.w) < 1e-9f || std::abs(farH.w) < 1e-9f)
    {
        return fallbackPivot;
    }
    const glm::vec3 origin = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPt  = glm::vec3(farH)  / farH.w;
    const glm::vec3 diff   = farPt - origin;
    const float     len    = glm::length(diff);
    if (len < 1e-6f) { return fallbackPivot; }
    const glm::vec3 dir = diff / len;

    // ray-plane（y = groundY）：t = (groundY - origin.y) / dir.y。
    // dir.y ≈ 0（射线平行地面）或 t < 0（地面在相机后方 / 朝上看不交）→ 退化到
    // 相机前方固定距离的射线点，保证总有合理落点（参 Unity 拖资源到天空时的处理）。
    constexpr float kFallbackDist = 8.0f;
    if (std::abs(dir.y) < 1e-4f)
    {
        return origin + dir * kFallbackDist;
    }
    const float t = (groundY - origin.y) / dir.y;
    if (t <= 0.0f || t > 1e5f)
    {
        return origin + dir * kFallbackDist;
    }
    return origin + dir * t;
}
