// dynamic body + 各 collider 形状的自由落体复现（GAP-2026-05-25 A3 反馈:
// "用 collider 视口顶点编辑后,play mode 里 dynamic 物体不掉落"）。
//
// 编辑器 EnterPlay 对每个 RigidBody+Collider 调 PhysicsWorld::AddBody(rb, cc),
// cc.shape 是 A3 编辑出的 Box/Circle/Polygon/EdgeChain 变体。本测试用同款
// 默认（gravity -9.81、type Dynamic、density 1）逐形状跑 1 秒,断言 dynamic
// body 掉落 —— 定位是否某种形状导致 AddBody 失败 / body 不被模拟。

#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>

#include <cassert>
#include <cstdio>

using namespace Orange::Engine::Physics;

namespace
{
// 返回掉落量（y0 - y1）。AddBody 失败返 -1。
float SimulateFall(const ColliderComponent& cc, const char* label)
{
    PhysicsWorld world;  // 默认 gravity (0, -9.81)
    RigidBodyComponent rb;  // 默认 type = Dynamic, gravityScale = 1
    rb.initialPosition = glm::vec2(0.0f, 10.0f);

    const BodyHandle h = world.AddBody(rb, cc);
    const bool valid = world.IsValid(h);
    const float y0 = valid ? world.GetBodyTransform(h).position.y : 0.0f;
    for (int i = 0; i < 60; ++i) { world.Step(1.0f / 60.0f); }
    const float y1 = valid ? world.GetBodyTransform(h).position.y : 0.0f;
    const float drop = valid ? (y0 - y1) : -1.0f;
    std::fprintf(stderr, "  [%s] AddBody valid=%d  y0=%.3f y1=%.3f  drop=%.3f\n",
                 label, valid ? 1 : 0, y0, y1, drop);
    return drop;
}
}  // namespace

int main()
{
    std::fprintf(stdout, "[ColliderShapeFallTest] running\n");

    // 基线:Box（编辑器默认 collider 之一）应自由落体 ~4.9m（0.5*g*t^2）。
    {
        ColliderComponent cc;
        cc.shape = BoxDesc{};
        const float d = SimulateFall(cc, "box");
        assert(d > 0.5f && "Box dynamic body 必须掉落（基线）");
    }
    // Circle 基线。
    {
        ColliderComponent cc;
        cc.shape = CircleDesc{};
        const float d = SimulateFall(cc, "circle");
        assert(d > 0.5f && "Circle dynamic body 必须掉落");
    }
    // Polygon（A3 编辑产出）—— 单位正方形 4 顶点。
    {
        ColliderComponent cc;
        PolygonDesc p;
        p.count = 4;
        p.vertices[0] = glm::vec2(-0.5f, -0.5f);
        p.vertices[1] = glm::vec2(0.5f, -0.5f);
        p.vertices[2] = glm::vec2(0.5f, 0.5f);
        p.vertices[3] = glm::vec2(-0.5f, 0.5f);
        cc.shape = p;
        const float d = SimulateFall(cc, "polygon");
        assert(d > 0.5f && "Polygon dynamic body 必须掉落");
    }
    // 退化多边形（count=2，如刚切换 ShapeType 后未画够顶点）—— 必须跳过、
    // 不崩（GAP-2026-05-25:之前 b2MakePolygon 对 <3 顶点内部 assert 崩）。
    {
        ColliderComponent cc;
        PolygonDesc p;
        p.count = 2;
        p.vertices[0] = glm::vec2(-0.5f, -0.5f);
        p.vertices[1] = glm::vec2(0.5f, -0.5f);
        cc.shape = p;
        const float d = SimulateFall(cc, "polygon-degenerate(count=2)");
        // 不崩即过;AddBody 应跳过 fixture → invalid handle → drop=-1。
        assert(d < 0.0f && "退化多边形应被跳过(invalid handle),且不崩");
    }
    // EdgeChain（A3 编辑产出）—— 3 顶点折线。在 dynamic body 上必须不崩
    // （之前 b2CreateChain 对非 static body assert 崩 → 编辑器进 Play 崩）。
    {
        ColliderComponent cc;
        EdgeChainDesc e;
        e.count = 3;
        e.vertices[0] = glm::vec2(-1.0f, 0.0f);
        e.vertices[1] = glm::vec2(0.0f, 0.0f);
        e.vertices[2] = glm::vec2(1.0f, 0.0f);
        e.isLoop = false;
        cc.shape = e;
        const float d = SimulateFall(cc, "edgechain");
        // 不 assert —— chain shape 在 dynamic body 上可能非法(Box2D 约定 chain
        // 仅 static)。先观察 drop / valid,据结果决定修法。
        std::fprintf(stderr, "  [edgechain 观察] drop=%.3f\n", d);
    }

    std::fprintf(stdout, "[ColliderShapeFallTest] done\n");
    return 0;
}
