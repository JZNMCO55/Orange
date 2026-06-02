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

#include <glm/gtc/quaternion.hpp>  // glm::quat / glm::angleAxis（gtc，稳定）

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
    return glm::vec3(wt->world[3]);  // 第 4 列 = 平移（列主序）
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
}  // namespace

int main()
{
    // ===== 1. 3 层链 A→B→C 平移累积 =====
    World w;
    const Entity a = w.CreateEntity();
    w.AddComponent<TransformComponent>(a, MakeTC({1.0f, 0.0f, 0.0f}));
    w.AddComponent<HierarchyComponent>(a, HierarchyComponent{});  // 根

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
    World w2;
    const Entity p = w2.CreateEntity();
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
    w.AddComponent<TransformComponent>(d, MakeTC({5.0f, 5.0f, 5.0f}));  // 不挂 Hierarchy
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

    std::fprintf(stdout, "[TransformSystemTest] all tests passed.\n");
    return 0;
}
