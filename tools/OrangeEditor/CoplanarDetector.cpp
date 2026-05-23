#include "CoplanarDetector.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <entt/entt.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Orange::Editor::Coplanar
{
namespace
{

// ---- AABB helpers ------------------------------------------------------
// 这三个函数与 tools/OrangeEditor/EditorPicking.cpp 的同名 helper 在语义上
// 一致；目前两处用到（picking + coplanar），按 CLAUDE.md "Three similar lines
// is better than a premature abstraction" 暂保留两份复制，第三处需要时再
// 抽公共 EditorAabb.{h,cpp}。
struct LocalAABB { glm::vec3 min; glm::vec3 max; };

glm::mat4
ComposeWorldMatrix(const Orange::Engine::Scene::TransformComponent& xform) noexcept
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), xform.position);
    m *= glm::mat4_cast(xform.rotation);
    m  = glm::scale(m, xform.scale);
    return m;
}

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

// 计算 entity world AABB；mesh / handle / asset 任一环节缺失返回 nullopt。
struct EntityAabb
{
    LocalAABB aabb;
    glm::vec3 center;
    glm::vec3 halfExtents;
};

bool
TryComputeEntityAabb(const Orange::Engine::Scene::TransformComponent& xform,
                     const Orange::Engine::Render::RenderableComponent& rc,
                     const Orange::Engine::Asset::AssetRegistry& assets,
                     EntityAabb& outAabb) noexcept
{
    if (!rc.visible) { return false; }
    const auto* pMesh = assets.Get(rc.mesh);
    if (pMesh == nullptr || pMesh->Empty()) { return false; }
    const LocalAABB local = ComputeMeshLocalAABB(*pMesh);
    const glm::mat4 m     = ComposeWorldMatrix(xform);
    const LocalAABB world = TransformAABB(local, m);
    outAabb.aabb        = world;
    outAabb.center      = (world.min + world.max) * 0.5f;
    outAabb.halfExtents = (world.max - world.min) * 0.5f;
    return true;
}

// 取 AABB 在某轴某面的坐标（self 的 selfFace 与 other 的 otherFace 比较时用）。
float FaceCoord(const LocalAABB& aabb, Face f) noexcept
{
    switch (f)
    {
        case Face::Right:  return aabb.max.x;
        case Face::Left:   return aabb.min.x;
        case Face::Top:    return aabb.max.y;
        case Face::Bottom: return aabb.min.y;
        case Face::Front:  return aabb.max.z;
        case Face::Back:   return aabb.min.z;
    }
    return 0.0f;
}

// 与某 face 共线的"对偶面"在 other AABB 上的语义：self.bottom 与 other.top
// 共面（同一 y 平面）、self.top 与 other.bottom 共面，依此类推。
Face Opposite(Face f) noexcept
{
    switch (f)
    {
        case Face::Right:  return Face::Left;
        case Face::Left:   return Face::Right;
        case Face::Top:    return Face::Bottom;
        case Face::Bottom: return Face::Top;
        case Face::Front:  return Face::Back;
        case Face::Back:   return Face::Front;
    }
    return f;
}

// 检测 self 的某轴某面（face）是否与 other 的对偶面 ε-共面 + 另外两轴 AABB
// 有重叠（仅同轴贴边但两 AABB 在该轴外完全错开 ≠ 视觉 z-fight；要求另外两轴
// 投影有交集才算"真撞上面对面 z-fight 风险"）。
bool
FacesOverlap(const LocalAABB& a, const LocalAABB& b, Face axisFace) noexcept
{
    // axisFace 决定哪个轴是"共面轴"；其余两轴需要 a 与 b 区间相交。
    auto intervalsOverlap = [](float aMin, float aMax, float bMin, float bMax)
    {
        return aMin <= bMax && bMin <= aMax;
    };
    switch (axisFace)
    {
        case Face::Right:
        case Face::Left:
            return intervalsOverlap(a.min.y, a.max.y, b.min.y, b.max.y)
                && intervalsOverlap(a.min.z, a.max.z, b.min.z, b.max.z);
        case Face::Top:
        case Face::Bottom:
            return intervalsOverlap(a.min.x, a.max.x, b.min.x, b.max.x)
                && intervalsOverlap(a.min.z, a.max.z, b.min.z, b.max.z);
        case Face::Front:
        case Face::Back:
            return intervalsOverlap(a.min.x, a.max.x, b.min.x, b.max.x)
                && intervalsOverlap(a.min.y, a.max.y, b.min.y, b.max.y);
    }
    return false;
}

}  // namespace

const char* FaceName(Face f) noexcept
{
    switch (f)
    {
        case Face::Right:  return "Right";
        case Face::Left:   return "Left";
        case Face::Top:    return "Top";
        case Face::Bottom: return "Bottom";
        case Face::Front:  return "Front";
        case Face::Back:   return "Back";
    }
    return "?";
}

std::vector<Hit>
DetectCoplanar(EditorHost&             host,
               Orange::Engine::Entity  selected,
               float                   eps,
               float                   maxDist)
{
    std::vector<Hit> hits;
    if (host.scene.pWorld == nullptr || !selected.IsValid() || host.assets.pAssets == nullptr)
    {
        return hits;
    }
    auto& world = *host.scene.pWorld;
    if (!world.IsValid(selected)) { return hits; }

    using TC = Orange::Engine::Scene::TransformComponent;
    using RC = Orange::Engine::Render::RenderableComponent;
    using NC = Orange::Engine::Scene::NameComponent;

    const auto* pSelfXform = world.GetComponent<TC>(selected);
    const auto* pSelfRc    = world.GetComponent<RC>(selected);
    if (pSelfXform == nullptr || pSelfRc == nullptr) { return hits; }

    EntityAabb selfBox;
    if (!TryComputeEntityAabb(*pSelfXform, *pSelfRc, *host.assets.pAssets, selfBox))
    {
        return hits;
    }

    constexpr Face kAllFaces[6] = {
        Face::Right, Face::Left, Face::Top, Face::Bottom, Face::Front, Face::Back
    };

    auto& reg = world.Registry();
    auto view = reg.view<TC, RC>();

    for (auto e : view)
    {
        const auto entity = Orange::Engine::World::FromEntt(e);
        if (entity == selected) { continue; }

        const auto& otherXform = view.get<TC>(e);
        const auto& otherRc    = view.get<RC>(e);

        EntityAabb otherBox;
        if (!TryComputeEntityAabb(otherXform, otherRc, *host.assets.pAssets, otherBox))
        {
            continue;
        }

        // 距离剪枝：两 AABB 中心距离 > maxDist + 各自半径之和 → 不可能共面贴上。
        const glm::vec3 centerDelta = otherBox.center - selfBox.center;
        if (glm::length(centerDelta) > maxDist + glm::length(selfBox.halfExtents)
                                                + glm::length(otherBox.halfExtents))
        {
            continue;
        }

        for (Face selfFace : kAllFaces)
        {
            const Face  otherFace  = Opposite(selfFace);
            const float selfCoord  = FaceCoord(selfBox.aabb,  selfFace);
            const float otherCoord = FaceCoord(otherBox.aabb, otherFace);
            const float gap        = std::abs(selfCoord - otherCoord);
            if (gap > eps) { continue; }
            if (!FacesOverlap(selfBox.aabb, otherBox.aabb, selfFace)) { continue; }

            std::string otherName;
            if (const auto* pNc = world.GetComponent<NC>(entity); pNc != nullptr)
            {
                otherName = pNc->name;
            }
            else
            {
                otherName = "Entity #" + std::to_string(
                    static_cast<unsigned>(static_cast<std::uint32_t>(entity.Value())));
            }

            hits.push_back(Hit{selfFace, entity, std::move(otherName), otherFace, gap});
        }
    }

    return hits;
}

}  // namespace Orange::Editor::Coplanar
