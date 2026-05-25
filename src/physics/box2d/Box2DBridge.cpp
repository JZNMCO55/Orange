#include "Box2DBridge.h"

#include "orange/engine/core/Log.h"

#include <box2d/collision.h>

#include <algorithm>
#include <variant>
#include <vector>

namespace Orange::Engine::Physics::Box2DBridge
{

b2WorldDef MakeWorldDef(const PhysicsWorldDesc& desc) noexcept
{
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity    = ToB2(desc.gravity);
    return wd;
}

b2BodyDef MakeBodyDef(const RigidBodyComponent& rb) noexcept
{
    b2BodyDef bd        = b2DefaultBodyDef();
    bd.type             = ToB2BodyType(rb.type);
    bd.position         = ToB2(rb.initialPosition);
    bd.rotation         = b2MakeRot(rb.initialAngle);
    bd.linearVelocity   = ToB2(rb.linearVelocity);
    bd.angularVelocity  = rb.angularVelocity;
    bd.linearDamping    = rb.linearDamping;
    bd.angularDamping   = rb.angularDamping;
    bd.fixedRotation    = rb.fixedRotation;
    bd.gravityScale     = rb.gravityScale;
    return bd;
}

b2ShapeDef MakeShapeDef(const ColliderComponent& col) noexcept
{
    b2ShapeDef sd          = b2DefaultShapeDef();
    sd.density             = col.density;
    sd.material.friction   = col.friction;
    sd.material.restitution = col.restitution;
    sd.isSensor            = col.isSensor;
    return sd;
}

namespace
{

// 单 polygon shape 用 b2MakePolygon(b2Hull*) 构造（Box2D 自己求凸壳 +
// 处理边界）。下方 hull 直接拷调用方给的顶点序列；count 越界由 b2 内部
// assert 兜底。
b2Polygon BuildPolygonFromDesc(const PolygonDesc& desc) noexcept
{
    b2Hull hull{};
    hull.count = static_cast<int>(desc.count);
    for (std::uint32_t i = 0; i < desc.count && i < B2_MAX_POLYGON_VERTICES; ++i)
    {
        hull.points[i] = ToB2(desc.vertices[i]);
    }
    // radius = 0：sharp edges。多边形带圆角留待消费方需要时显式开。
    return b2MakePolygon(&hull, /*radius=*/0.0f);
}

}  // namespace

bool CreateShapeFor(b2BodyId bodyId, const ColliderComponent& col)
{
    b2ShapeDef sd = MakeShapeDef(col);

    return std::visit([&](const auto& shape) -> bool {
        using T = std::decay_t<decltype(shape)>;

        if constexpr (std::is_same_v<T, CircleDesc>)
        {
            b2Circle circle{};
            circle.center = ToB2(shape.center);
            circle.radius = shape.radius;
            (void)b2CreateCircleShape(bodyId, &sd, &circle);
            return true;
        }
        else if constexpr (std::is_same_v<T, BoxDesc>)
        {
            // b2MakeBox 出 b2Polygon（带 4 顶点 + 中心 / 旋转）。center
            // 偏移走 b2MakeOffsetBox。本期不带旋转——shape 旋转跟 body
            // 走，需要单独 shape rotation 时再扩。
            b2Polygon poly = (shape.center.x == 0.0f && shape.center.y == 0.0f)
                                 ? b2MakeBox(shape.halfExtents.x, shape.halfExtents.y)
                                 : b2MakeOffsetBox(shape.halfExtents.x,
                                                   shape.halfExtents.y,
                                                   ToB2(shape.center),
                                                   b2MakeRot(0.0f));
            (void)b2CreatePolygonShape(bodyId, &sd, &poly);
            return true;
        }
        else if constexpr (std::is_same_v<T, PolygonDesc>)
        {
            // 退化多边形（空 / 少于 3 顶点）→ b2ComputeHull / b2MakePolygon 会
            // 内部 assert 崩溃（GAP-2026-05-25 A3 反馈:collider 切到 Polygon 但
            // 未画出有效多边形 → 进 Play 崩 / 物体不动）。这里前置校验,无效则
            // 跳过 fixture（caller AddBody 会销毁该 body 并返回 invalid handle）。
            if (shape.count < 3u)
            {
                ORANGE_LOG_WARN("Box2DBridge: PolygonDesc count={} < 3，跳过 fixture"
                                "（凸多边形需 ≥3 顶点）", shape.count);
                return false;
            }
            b2Polygon poly = BuildPolygonFromDesc(shape);
            (void)b2CreatePolygonShape(bodyId, &sd, &poly);
            return true;
        }
        else if constexpr (std::is_same_v<T, EdgeChainDesc>)
        {
            if (shape.count < 2u)
            {
                ORANGE_LOG_WARN("Box2DBridge: EdgeChainDesc count={} < 2，跳过 fixture",
                                shape.count);
                return false;
            }
            // **关键**：Box2D 的 chain shape 只能挂在 static body —— 对非 static
            // body 调 b2CreateChain 直接 assert 崩溃（GAP-2026-05-25 A3 反馈:
            // dynamic 物体上的 EdgeChain 让编辑器进 Play 崩）。且 b2CreateChain
            // 要求 count >= 4。仅 static + count>=4 走 chain（terrain，平滑无 ghost
            // collision）；其余（非 static / count<4）降级为逐段 b2Segment fixture
            // —— segment 在任意 body type + 任意 count>=2 合法。
            const b2BodyType bt = b2Body_GetType(bodyId);
            if (bt == b2_staticBody && shape.count >= 4u)
            {
                b2ChainDef cd = b2DefaultChainDef();
                // chain 自己持 points 拷贝（文档承诺），本地 array 可栈分配。
                b2Vec2 pts[EdgeChainDesc::kMaxVertices];
                for (std::uint32_t i = 0; i < shape.count; ++i)
                {
                    pts[i] = ToB2(shape.vertices[i]);
                }
                cd.points        = pts;
                cd.count         = static_cast<int>(shape.count);
                b2SurfaceMaterial mat{};
                mat.friction     = col.friction;
                mat.restitution  = col.restitution;
                cd.materials     = &mat;
                cd.materialCount = 1;
                cd.isLoop        = shape.isLoop;
                (void)b2CreateChain(bodyId, &cd);
                return true;
            }
            // 降级:逐段 segment。isLoop 时末点连回首点。
            const std::uint32_t segCount =
                shape.isLoop ? shape.count : (shape.count - 1u);
            for (std::uint32_t i = 0; i < segCount; ++i)
            {
                b2Segment seg{};
                seg.point1 = ToB2(shape.vertices[i]);
                seg.point2 = ToB2(shape.vertices[(i + 1u) % shape.count]);
                (void)b2CreateSegmentShape(bodyId, &sd, &seg);
            }
            if (bt != b2_staticBody)
            {
                ORANGE_LOG_WARN("Box2DBridge: EdgeChain 在非 static body 上 → 降级为 segment "
                                "fixture（chain 仅 static；segment 零面积不产生质量,dynamic "
                                "物体请改用 Polygon / Box collider）");
            }
            return true;
        }
        else
        {
            return false;  // 未知 alternative —— variant 完备性兜底
        }
    }, col.shape);
}

void DestroyAllShapesOnBody(b2BodyId bodyId)
{
    if (!B2_IS_NON_NULL(bodyId))
    {
        return;
    }

    // 第一步：枚举 shape，挑出归属 chain 的 segment，去重收集 b2ChainId。
    // chain 持有自己的 segment shape，必须从 chain 入口销毁（直接 b2DestroyShape
    // 一个 chain segment 是非法操作）。独立 shape（非 chain segment）单独清。
    const int shapeCount = b2Body_GetShapeCount(bodyId);
    if (shapeCount > 0)
    {
        std::vector<b2ShapeId> shapes(static_cast<std::size_t>(shapeCount));
        const int gotShapes = b2Body_GetShapes(bodyId, shapes.data(), shapeCount);

        std::vector<b2ChainId> chains;
        chains.reserve(static_cast<std::size_t>(gotShapes));

        for (int i = 0; i < gotShapes; ++i)
        {
            const b2ChainId cid = b2Shape_GetParentChain(shapes[static_cast<std::size_t>(i)]);
            if (!B2_IS_NON_NULL(cid))
            {
                continue;
            }
            // 用 (index1, generation, world0) 三元组判等。b2 的 ID 都是值类型，
            // 没有运算符，手动比较。
            const auto same = [&cid](const b2ChainId& other) noexcept
            {
                return other.index1 == cid.index1
                    && other.generation == cid.generation
                    && other.world0 == cid.world0;
            };
            if (std::find_if(chains.begin(), chains.end(), same) == chains.end())
            {
                chains.push_back(cid);
            }
        }

        // 先清 chain（连同它的 segment shape），再清剩余的独立 shape。
        for (const b2ChainId& cid : chains)
        {
            b2DestroyChain(cid);
        }
    }

    // 重新拉一遍剩余 shape——chain 销毁后这一遍只会剩独立 shape。
    const int remaining = b2Body_GetShapeCount(bodyId);
    if (remaining <= 0)
    {
        return;
    }
    std::vector<b2ShapeId> rest(static_cast<std::size_t>(remaining));
    const int gotRest = b2Body_GetShapes(bodyId, rest.data(), remaining);
    for (int i = 0; i < gotRest; ++i)
    {
        // updateBodyMass=false：销毁阶段不让 b2 中途算 mass，由 ReplaceFixture
        // 调用方在新 shape 创建后统一 b2Body_ApplyMassFromShapes。
        b2DestroyShape(rest[static_cast<std::size_t>(i)], /*updateBodyMass=*/false);
    }
}

}  // namespace Orange::Engine::Physics::Box2DBridge
