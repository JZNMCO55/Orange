// TransformSystem 测试（ADR-016 / maturity-roadmap A1.1 step 1）——锁住
// "从 HierarchyComponent 自顶向下累积 world matrix" 的数值正确性。本阶段是
// additive cache（零消费者），所以验的就是 PropagateWorldTransforms 产出的
// WorldTransformComponent 数值对不对：多层累积 / 旋转父真矩阵乘 / flat entity /
// 幂等重跑。

#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/TransformSystem.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <glm/gtc/quaternion.hpp> // glm::quat / glm::angleAxis（gtc，稳定）

#include <cassert>
#include <cmath>
#include <cstdio>

using ::Orange::Engine::Entity;
using ::Orange::Engine::World;
using ::Orange::Engine::Scene::HierarchyComponent;
using ::Orange::Engine::Scene::PropagateWorldTransforms;
using ::Orange::Engine::Scene::TransformComponent;
using ::Orange::Engine::Scene::WorldTransformComponent;

namespace
{
    glm::vec3 WorldPos(World& w, Entity e)
    {
        const auto* wt = w.GetComponent<WorldTransformComponent>(e);
        assert(wt != nullptr && "entity 应有 WorldTransformComponent（pass 跑过）");
        return glm::vec3(wt->world[3]); // 第 4 列 = 平移（列主序）
    }

    bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps &&
               std::fabs(a.z - b.z) < eps;
    }

    TransformComponent MakeTC(const glm::vec3& pos,
                              const glm::quat& rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                              const glm::vec3& scl = glm::vec3(1.0f))
    {
        return TransformComponent{pos, rot, scl};
    }
} // namespace

int main()
{
    // ===== 1. 3 层链 A→B→C 平移累积 =====
    World        w;
    const Entity a = w.CreateEntity();
    w.AddComponent<TransformComponent>(a, MakeTC({1.0f, 0.0f, 0.0f}));
    w.AddComponent<HierarchyComponent>(a, HierarchyComponent{}); // 根

    const Entity b = w.CreateEntity();
    w.AddComponent<TransformComponent>(b, MakeTC({0.0f, 1.0f, 0.0f}));
    HierarchyComponent hb;
    hb.parent = a;
    w.AddComponent<HierarchyComponent>(b, hb);

    const Entity c = w.CreateEntity();
    w.AddComponent<TransformComponent>(c, MakeTC({0.0f, 0.0f, 1.0f}));
    HierarchyComponent hc;
    hc.parent = b;
    w.AddComponent<HierarchyComponent>(c, hc);

    // 链接子（firstChild）。
    w.GetComponent<HierarchyComponent>(a)->firstChild = b;
    w.GetComponent<HierarchyComponent>(b)->firstChild = c;

    PropagateWorldTransforms(w);

    assert(Near(WorldPos(w, a), {1.0f, 0.0f, 0.0f}) && "A world = local (1,0,0)");
    assert(Near(WorldPos(w, b), {1.0f, 1.0f, 0.0f}) && "B world = A(1,0,0)+local(0,1,0)");
    assert(Near(WorldPos(w, c), {1.0f, 1.0f, 1.0f}) && "C world 穿 2 层累积 (1,1,1)");
    std::fprintf(stdout, "  [PASS] 3 层链平移累积 A(1,0,0)→B(1,1,0)→C(1,1,1)\n");

    // ===== 2. 旋转父：验真矩阵乘（不是简单位置相加）=====
    // 父 P 绕 Y 转 90°，子 Q 在 P 本地 +X (1,0,0)。绕 +Y 转 90° 把 +X 转到 -Z。
    // 故 Q world ≈ (0,0,-1)。
    World           w2;
    const Entity    p      = w2.CreateEntity();
    const glm::quat rotY90 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    w2.AddComponent<TransformComponent>(p, MakeTC({0.0f, 0.0f, 0.0f}, rotY90));
    w2.AddComponent<HierarchyComponent>(p, HierarchyComponent{});

    const Entity q = w2.CreateEntity();
    w2.AddComponent<TransformComponent>(q, MakeTC({1.0f, 0.0f, 0.0f}));
    HierarchyComponent hq;
    hq.parent = p;
    w2.AddComponent<HierarchyComponent>(q, hq);
    w2.GetComponent<HierarchyComponent>(p)->firstChild = q;

    PropagateWorldTransforms(w2);
    assert(Near(WorldPos(w2, q), {0.0f, 0.0f, -1.0f}, 1e-3f) &&
           "旋转父 90°(Y) → 子本地 +X 被转到世界 -Z（真矩阵乘，非位置相加）");
    std::fprintf(stdout, "  [PASS] 旋转父真矩阵乘：子 +X → world (0,0,-1)\n");

    // ===== 3. flat entity（无 HierarchyComponent）→ world = local =====
    const Entity d = w.CreateEntity();
    w.AddComponent<TransformComponent>(d, MakeTC({5.0f, 5.0f, 5.0f})); // 不挂 Hierarchy
    PropagateWorldTransforms(w);
    assert(Near(WorldPos(w, d), {5.0f, 5.0f, 5.0f}) &&
           "无 Hierarchy 的 flat entity 视为根，world = local (5,5,5)");
    std::fprintf(stdout, "  [PASS] flat entity world = local (5,5,5)\n");

    // ===== 4. 幂等：再跑一次，已有值稳定（emplace_or_replace 不漂移）=====
    assert(Near(WorldPos(w, c), {1.0f, 1.0f, 1.0f}) && "重跑后 C 仍稳定 (1,1,1)");
    PropagateWorldTransforms(w);
    assert(Near(WorldPos(w, a), {1.0f, 0.0f, 0.0f}) &&
           Near(WorldPos(w, c), {1.0f, 1.0f, 1.0f}) &&
           Near(WorldPos(w, d), {5.0f, 5.0f, 5.0f}) &&
           "PropagateWorldTransforms 幂等（重跑数值不漂移）");
    std::fprintf(stdout, "  [PASS] 幂等重跑数值稳定\n");

    // ===== 5. 非均匀 scale 传播：父缩放影响子的世界位置 =====
    // 父 sp scale (2,1,1) 在原点；子 sq 本地 (1,0,0) → sq 世界 = sp 缩放后 = (2,0,0)。
    {
        World  ws;
        Entity sp = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(
            sp, MakeTC({0.0f, 0.0f, 0.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                       glm::vec3(2.0f, 1.0f, 1.0f)));
        ws.AddComponent<HierarchyComponent>(sp, HierarchyComponent{});
        Entity sq = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(sq, MakeTC({1.0f, 0.0f, 0.0f}));
        HierarchyComponent hsq;
        hsq.parent = sp;
        ws.AddComponent<HierarchyComponent>(sq, hsq);
        ws.GetComponent<HierarchyComponent>(sp)->firstChild = sq;

        PropagateWorldTransforms(ws);
        assert(Near(WorldPos(ws, sq), {2.0f, 0.0f, 0.0f}) &&
               "父 scale (2,1,1) → 子本地 (1,0,0) 世界 = (2,0,0)（scale 沿 hierarchy 传播）");
        std::fprintf(stdout, "  [PASS] 非均匀 scale 传播：子世界位置被父缩放\n");
    }

    // ===== 6. sibling 独立：同父的两个子各自累积、互不影响 =====
    {
        World  ws;
        Entity bp = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(bp, MakeTC({10.0f, 0.0f, 0.0f}));
        ws.AddComponent<HierarchyComponent>(bp, HierarchyComponent{});
        Entity c1 = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(c1, MakeTC({1.0f, 0.0f, 0.0f}));
        Entity c2 = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(c2, MakeTC({0.0f, 2.0f, 0.0f}));
        HierarchyComponent h1;
        h1.parent      = bp;
        h1.nextSibling = c2;
        ws.AddComponent<HierarchyComponent>(c1, h1);
        HierarchyComponent h2;
        h2.parent      = bp;
        h2.prevSibling = c1;
        ws.AddComponent<HierarchyComponent>(c2, h2);
        ws.GetComponent<HierarchyComponent>(bp)->firstChild = c1;

        PropagateWorldTransforms(ws);
        assert(Near(WorldPos(ws, c1), {11.0f, 0.0f, 0.0f}) &&
               "c1 世界 = 父(10,0,0)+本地(1,0,0) = (11,0,0)");
        assert(Near(WorldPos(ws, c2), {10.0f, 2.0f, 0.0f}) &&
               "c2 世界 = 父(10,0,0)+本地(0,2,0) = (10,2,0)，与 c1 互不影响");
        std::fprintf(stdout, "  [PASS] sibling 独立：两子各自累积互不影响\n");
    }

    // ===== 7. 组合 TRS 穿层级：父旋转+平移作用于子的 local 偏移 =====
    // 这是所有 consumer（render/picking/光源/reparent）依赖的核心：父的旋转必须
    // 作用于子的 local 偏移，再叠父平移。父 P 在 (5,0,0) 绕 Y 转 90°；子 C 本地
    // (1,0,0)。绕 +Y 90° 把 (1,0,0) 转到 (0,0,-1)，再叠父平移 → C 世界 (5,0,-1)。
    {
        World           ws;
        Entity          tp      = ws.CreateEntity();
        const glm::quat rotY90b = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
        ws.AddComponent<TransformComponent>(tp, MakeTC({5.0f, 0.0f, 0.0f}, rotY90b));
        ws.AddComponent<HierarchyComponent>(tp, HierarchyComponent{});
        Entity tc = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(tc, MakeTC({1.0f, 0.0f, 0.0f}));
        HierarchyComponent htc;
        htc.parent = tp;
        ws.AddComponent<HierarchyComponent>(tc, htc);
        ws.GetComponent<HierarchyComponent>(tp)->firstChild = tc;

        PropagateWorldTransforms(ws);
        assert(Near(WorldPos(ws, tc), {5.0f, 0.0f, -1.0f}, 1e-3f) &&
               "组合 TRS：父旋转 90°(Y) 作用于子 local(1,0,0)=(0,0,-1) 再叠父平移(5,0,0) "
               "→ 子世界 (5,0,-1)（真矩阵复合，非位置/方向各自相加）");
        std::fprintf(stdout,
                     "  [PASS] 组合 TRS 穿层级：父旋转作用于子偏移 + 父平移 → (5,0,-1)\n");
    }

    // ===== 8. 环兜底：root 可达的 firstChild 环不应栈溢出 =====
    // 畸形/损坏 scene 可能有 R→A→B→A 的 firstChild 环（编辑器 IsAncestorOf 禁环，
    // 但手改场景文件能造出）。无 kMaxHierarchyDepth 守卫时 DFS 会无限递归直至栈溢出。
    // 本例锁住：PropagateWorldTransforms 应正常返回（截断递归、不崩不挂）。
    {
        World  ws;
        Entity cr = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(cr, MakeTC({0.0f, 0.0f, 0.0f}));
        Entity ca = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(ca, MakeTC({1.0f, 0.0f, 0.0f}));
        Entity cb = ws.CreateEntity();
        ws.AddComponent<TransformComponent>(cb, MakeTC({0.0f, 1.0f, 0.0f}));

        // R（root，无 parent）.firstChild=A；A.parent=R, A.firstChild=B；
        // B.parent=A, B.firstChild=A（制造 A↔B 环）。
        HierarchyComponent cyhR;
        cyhR.firstChild = ca;
        ws.AddComponent<HierarchyComponent>(cr, cyhR);
        HierarchyComponent cyhA;
        cyhA.parent     = cr;
        cyhA.firstChild = cb;
        ws.AddComponent<HierarchyComponent>(ca, cyhA);
        HierarchyComponent cyhB;
        cyhB.parent     = ca;
        cyhB.firstChild = ca; // 环：B 的子又指回 A
        ws.AddComponent<HierarchyComponent>(cb, cyhB);

        // 不崩不挂即通过（kMaxHierarchyDepth 截断递归）。环中实体在截断前已写过
        // world cache。
        PropagateWorldTransforms(ws);
        assert(ws.GetComponent<WorldTransformComponent>(ca) != nullptr &&
               ws.GetComponent<WorldTransformComponent>(cb) != nullptr &&
               "环中实体应拿到 world cache（截断前已写），且整体不栈溢出");
        std::fprintf(stdout,
                     "  [PASS] 环兜底：root 可达 firstChild 环不栈溢出，正常返回\n");
    }

    std::fprintf(stdout, "[TransformSystemTest] all tests passed.\n");
    return 0;
}
