#include "ColliderDebugDraw.h"

#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/render/DebugDrawScene.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>

namespace Orange::Editor
{

    namespace
    {

        // 与 ScenePanel.cpp DebugDraw 选中 sphere 同款配色：unselected 走浅绿
        // （区别于"标准 v0.9 调试色"红/绿/蓝坐标轴），selected 走橙黄（与 v0.9
        // 选中 sphere kHi = 0xFF00FFFFu 相同）。颜色编码：ABGR packed，低 8 位
        // 是 R 分量。
        constexpr std::uint32_t kColorUnselected = 0xFF40C040u; // 浅绿
        constexpr std::uint32_t kColorSelected   = 0xFF00FFFFu; // 橙黄

        // 2D circle 用 N 段线段近似。32 段对编辑视觉已足够（半径 1m 在 1920×1080
        // 1m grid 下相邻段长 ~0.2m，没有可见折线感）。
        constexpr int kCircleSegments = 32;

        // 旋转一个 2D 本地点（local.xy 平面）经 entity quaternion 到世界坐标。
        // Z 保持来自 worldZ —— Box2D collider 是 XY 平面 + Z 不变，绕 Z 轴 yaw
        // 是平面跳跃 typical entity rotation；其它轴旋转下 Box2D 物理本身就不准
        // 确（shape 没有真正 3D 碰撞），可视化保持几何对位即可。
        glm::vec3 RotateLocalXY(const glm::quat& rot,
                                const glm::vec2& local,
                                const glm::vec3& worldOrigin)
        {
            const glm::vec3 rotated = rot * glm::vec3{local.x, local.y, 0.0f};
            return worldOrigin + glm::vec3{rotated.x, rotated.y, 0.0f};
        }

        void DrawCircle(Orange::Engine::Render::DebugDrawScene& dbg,
                        const glm::vec3&                        center,
                        float                                   radius,
                        std::uint32_t                           color)
        {
            glm::vec3 prev = center + glm::vec3{radius, 0.0f, 0.0f};
            for (int i = 1; i <= kCircleSegments; ++i)
            {
                const float     t   = (static_cast<float>(i) / static_cast<float>(kCircleSegments)) * 6.28318530718f;
                const glm::vec3 cur = center + glm::vec3{
                                                   std::cos(t) * radius, std::sin(t) * radius, 0.0f};
                dbg.AddLine(prev, cur, color);
                prev = cur;
            }
        }

        void DrawBox(Orange::Engine::Render::DebugDrawScene& dbg,
                     const glm::vec3&                        center,
                     const glm::vec2&                        halfExtents,
                     const glm::quat&                        rotation,
                     std::uint32_t                           color)
        {
            // 4 本地角点（CCW）：(-hx,-hy) → (+hx,-hy) → (+hx,+hy) → (-hx,+hy)
            const glm::vec2 local[4] = {
                {-halfExtents.x, -halfExtents.y},
                {+halfExtents.x, -halfExtents.y},
                {+halfExtents.x, +halfExtents.y},
                {-halfExtents.x, +halfExtents.y},
            };
            glm::vec3 world[4];
            for (int i = 0; i < 4; ++i)
            {
                world[i] = RotateLocalXY(rotation, local[i], center);
            }
            dbg.AddLine(world[0], world[1], color);
            dbg.AddLine(world[1], world[2], color);
            dbg.AddLine(world[2], world[3], color);
            dbg.AddLine(world[3], world[0], color);
        }

        void DrawPolygon(Orange::Engine::Render::DebugDrawScene&     dbg,
                         const Orange::Engine::Physics::PolygonDesc& desc,
                         const glm::vec3&                            origin,
                         const glm::quat&                            rotation,
                         std::uint32_t                               color)
        {
            if (desc.count < 2)
            {
                return;
            }
            for (std::uint32_t i = 0; i < desc.count; ++i)
            {
                const std::uint32_t j = (i + 1) % desc.count; // 闭环
                const glm::vec3     a = RotateLocalXY(rotation, desc.vertices[i], origin);
                const glm::vec3     b = RotateLocalXY(rotation, desc.vertices[j], origin);
                dbg.AddLine(a, b, color);
            }
        }

        void DrawEdgeChain(Orange::Engine::Render::DebugDrawScene&       dbg,
                           const Orange::Engine::Physics::EdgeChainDesc& desc,
                           const glm::vec3&                              origin,
                           const glm::quat&                              rotation,
                           std::uint32_t                                 color)
        {
            if (desc.count < 2)
            {
                return;
            }
            const std::uint32_t segCount = desc.isLoop ? desc.count : (desc.count - 1);
            for (std::uint32_t i = 0; i < segCount; ++i)
            {
                const std::uint32_t j = (i + 1) % desc.count;
                const glm::vec3     a = RotateLocalXY(rotation, desc.vertices[i], origin);
                const glm::vec3     b = RotateLocalXY(rotation, desc.vertices[j], origin);
                dbg.AddLine(a, b, color);
            }
        }

    } // anonymous namespace

    void DrawColliders(Orange::Engine::Render::DebugDrawScene&    dbg,
                       Orange::Engine::World&                     world,
                       Orange::Engine::Entity                     selectedEntity,
                       const std::vector<Orange::Engine::Entity>& additionalSelectedEntities)
    {
        using Orange::Engine::Physics::BoxDesc;
        using Orange::Engine::Physics::CircleDesc;
        using Orange::Engine::Physics::ColliderComponent;
        using Orange::Engine::Physics::EdgeChainDesc;
        using Orange::Engine::Physics::PolygonDesc;
        using TC = Orange::Engine::Scene::TransformComponent;

        auto isSelected = [&](Orange::Engine::Entity e) -> bool
        {
            if (e == selectedEntity)
            {
                return true;
            }
            return std::find(additionalSelectedEntities.begin(),
                             additionalSelectedEntities.end(), e) != additionalSelectedEntities.end();
        };

        auto& reg  = world.Registry();
        auto  view = reg.view<TC, ColliderComponent>();
        for (auto e : view)
        {
            const auto&         xf     = view.get<TC>(e);
            const auto&         cc     = view.get<ColliderComponent>(e);
            const glm::vec3     origin = xf.position;
            const glm::quat     rot    = xf.rotation;
            const std::uint32_t color  = isSelected(Orange::Engine::World::FromEntt(e))
                                             ? kColorSelected
                                             : kColorUnselected;

            std::visit([&](const auto& shape)
                       {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, CircleDesc>)
            {
                const glm::vec3 center =
                    RotateLocalXY(rot, shape.center, origin);
                DrawCircle(dbg, center, shape.radius, color);
            }
            else if constexpr (std::is_same_v<T, BoxDesc>)
            {
                const glm::vec3 center =
                    RotateLocalXY(rot, shape.center, origin);
                DrawBox(dbg, center, shape.halfExtents, rot, color);
            }
            else if constexpr (std::is_same_v<T, PolygonDesc>)
            {
                DrawPolygon(dbg, shape, origin, rot, color);
            }
            else if constexpr (std::is_same_v<T, EdgeChainDesc>)
            {
                DrawEdgeChain(dbg, shape, origin, rot, color);
            } }, cc.shape);
        }
    }

} // namespace Orange::Editor
