// EditorHierarchy 图操作单元测试。
//
// EditorHierarchy（tools/OrangeEditor）维护 HierarchyComponent 的双向兄弟链，是
// Entity Tree 拖拽 reparent / reorder 的底座。这套逻辑是纯 World + HierarchyComponent
// 运算、零 GUI 依赖，故可像 material_file_io_test 一样直接把 helper 源编进测试 exe
// 验证（编辑器侧的 ImGui DnD 交互无法 headless 驱动，但这层图逻辑能在此被完整覆盖）。
//
// 覆盖：
//   * LinkAsLastChild / Detach（头/中/尾）/ ReparentTo
//   * MoveToPosition（插最前 / 插某兄弟后 / parent==Invalid 提 root / afterSibling
//     不属 parent 的防御性挂尾）
//   * MoveBefore / MoveAfter（同父重排 + 跨父定位 + 相邻 no-op）
//   * IsAncestorOf（自身 / 直接 / 传递 / 无关）
//   * Undo 往返：MoveToPosition(oldParent, oldPrev) 精确复位
//   * DestroySubtree 递归销毁 + 从父链摘除

#include "EditorHierarchy.h"

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using Orange::Engine::Entity;
using Orange::Engine::World;
using HC = Orange::Engine::Scene::HierarchyComponent;
namespace Scene = Orange::Engine::Scene;

namespace
{

// 走兄弟链收集 parent 的子节点顺序，同时**正反向**校验：每个子的 parent 指回
// parent、prevSibling 指向上一个（首子为 Invalid）。任何链断裂 / 反向不一致都
// 会在此 assert 失败，故各测试用例只需对比返回的顺序向量。
std::vector<Entity> ChildrenOf(World& world, Entity parent)
{
    std::vector<Entity> out;
    const HC* ph = world.GetComponent<HC>(parent);
    Entity    cur  = (ph != nullptr) ? ph->firstChild : Entity::Invalid();
    Entity    prev = Entity::Invalid();
    while (cur.IsValid())
    {
        const HC* ch = world.GetComponent<HC>(cur);
        assert(ch != nullptr);
        assert(ch->parent == parent);       // 子的 parent 必须指回
        assert(ch->prevSibling == prev);     // 双向链反向一致
        out.push_back(cur);
        prev = cur;
        cur  = ch->nextSibling;
    }
    return out;
}

bool IsDetachedRoot(World& world, Entity e)
{
    const HC* h = world.GetComponent<HC>(e);
    if (h == nullptr) { return true; }   // 无 HC 视为 root
    return !h->parent.IsValid()
        && !h->prevSibling.IsValid()
        && !h->nextSibling.IsValid();
}

// 收集所有根（parent==Invalid），按 (sortIndex, entity id) 排序——与
// EditorHierarchy::MoveRootRelative / EntityTreePanel 根枚举同序（ADR-014）。
std::vector<Entity> RootOrder(World& world)
{
    struct RE { Entity e; int s; std::uint32_t id; };
    std::vector<RE> rs;
    for (auto ent : world.Registry().view<entt::entity>())
    {
        const Entity e = World::FromEntt(ent);
        const HC*    h = world.GetComponent<HC>(e);
        if (h == nullptr || !h->parent.IsValid())
        {
            rs.push_back({e, (h != nullptr) ? h->sortIndex : 0,
                          static_cast<std::uint32_t>(entt::to_integral(ent))});
        }
    }
    std::sort(rs.begin(), rs.end(), [](const RE& a, const RE& b) {
        if (a.s != b.s) { return a.s < b.s; }
        return a.id < b.id;
    });
    std::vector<Entity> out;
    out.reserve(rs.size());
    for (const auto& r : rs) { out.push_back(r.e); }
    return out;
}

// 造一个 root 下挂 n 个子（按传入顺序）的场景，返回 [root, child0, child1, ...]。
std::vector<Entity> MakeRootWithChildren(World& world, int n)
{
    std::vector<Entity> ids;
    Entity root = world.CreateEntity();
    ids.push_back(root);
    for (int i = 0; i < n; ++i)
    {
        Entity c = world.CreateEntity();
        EditorHierarchy::LinkAsLastChild(world, root, c);
        ids.push_back(c);
    }
    return ids;
}

void TestLinkAsLastChild()
{
    World world;
    auto   ids  = MakeRootWithChildren(world, 3);   // root, a, b, c
    Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];

    assert(ChildrenOf(world, root) == (std::vector<Entity>{a, b, c}));
    assert(world.GetComponent<HC>(root)->firstChild == a);
    assert(IsDetachedRoot(world, root));   // root 自身仍是 root

    std::fprintf(stdout, "  [PASS] LinkAsLastChild builds ordered chain\n");
}

void TestDetachHeadMiddleTail()
{
    {   // 摘中间
        World world;
        auto   ids = MakeRootWithChildren(world, 3);
        Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];
        EditorHierarchy::Detach(world, b);
        assert(ChildrenOf(world, root) == (std::vector<Entity>{a, c}));
        assert(IsDetachedRoot(world, b));
    }
    {   // 摘头
        World world;
        auto   ids = MakeRootWithChildren(world, 3);
        Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];
        EditorHierarchy::Detach(world, a);
        assert(ChildrenOf(world, root) == (std::vector<Entity>{b, c}));
        assert(world.GetComponent<HC>(root)->firstChild == b);
        assert(IsDetachedRoot(world, a));
    }
    {   // 摘尾
        World world;
        auto   ids = MakeRootWithChildren(world, 3);
        Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];
        EditorHierarchy::Detach(world, c);
        assert(ChildrenOf(world, root) == (std::vector<Entity>{a, b}));
        assert(IsDetachedRoot(world, c));
    }
    {   // 摘已是 root 的实体：no-op，不崩
        World  world;
        Entity r = world.CreateEntity();
        world.AddComponent<HC>(r, {});
        EditorHierarchy::Detach(world, r);
        assert(IsDetachedRoot(world, r));
    }
    std::fprintf(stdout, "  [PASS] Detach head/middle/tail/root\n");
}

void TestReparentTo()
{
    World world;
    auto   ids  = MakeRootWithChildren(world, 2);   // root, a, b
    Entity root = ids[0], a = ids[1], b = ids[2];
    Entity p2   = world.CreateEntity();
    world.AddComponent<HC>(p2, {});

    EditorHierarchy::ReparentTo(world, a, p2);   // a 挂到 p2 末尾
    assert(ChildrenOf(world, root) == (std::vector<Entity>{b}));
    assert(ChildrenOf(world, p2) == (std::vector<Entity>{a}));
    assert(world.GetComponent<HC>(a)->parent == p2);

    EditorHierarchy::ReparentTo(world, b, Entity::Invalid());   // b 提到 root
    assert(ChildrenOf(world, root).empty());
    assert(IsDetachedRoot(world, b));

    std::fprintf(stdout, "  [PASS] ReparentTo (to parent tail / to root)\n");
}

void TestMoveToPosition()
{
    {   // afterSibling == Invalid → 插到子链最前
        World world;
        auto   ids = MakeRootWithChildren(world, 2);   // root, a, b
        Entity root = ids[0], a = ids[1], b = ids[2];
        Entity x = world.CreateEntity();
        EditorHierarchy::MoveToPosition(world, x, root, Entity::Invalid());
        assert(ChildrenOf(world, root) == (std::vector<Entity>{x, a, b}));
    }
    {   // afterSibling 有效 → 插到它之后
        World world;
        auto   ids = MakeRootWithChildren(world, 2);
        Entity root = ids[0], a = ids[1], b = ids[2];
        Entity x = world.CreateEntity();
        EditorHierarchy::MoveToPosition(world, x, root, a);   // a 之后
        assert(ChildrenOf(world, root) == (std::vector<Entity>{a, x, b}));
    }
    {   // parent == Invalid → 仅摘成 root（afterSibling 忽略）
        World world;
        auto   ids = MakeRootWithChildren(world, 2);
        Entity root = ids[0], a = ids[1];
        EditorHierarchy::MoveToPosition(world, a, Entity::Invalid(), Entity::Invalid());
        assert(IsDetachedRoot(world, a));
        assert(world.GetComponent<HC>(root)->firstChild == ids[2]);  // b 顶上来
    }
    {   // afterSibling 不属于 parent → 防御性挂尾，不破链
        World world;
        auto   idsA = MakeRootWithChildren(world, 2);   // pA, m, n
        Entity pA = idsA[0], m = idsA[1], n = idsA[2];
        Entity pB = world.CreateEntity();
        world.AddComponent<HC>(pB, {});
        Entity stray = world.CreateEntity();
        EditorHierarchy::LinkAsLastChild(world, pB, stray);   // stray 属 pB
        Entity x = world.CreateEntity();
        EditorHierarchy::MoveToPosition(world, x, pA, stray);  // afterSibling 不属 pA
        assert(ChildrenOf(world, pA) == (std::vector<Entity>{m, n, x}));  // 挂到 pA 尾
        (void)m; (void)n;
    }
    std::fprintf(stdout, "  [PASS] MoveToPosition (front/after/root/defensive-tail)\n");
}

void TestMoveBeforeAfter()
{
    {   // MoveBefore：[a,b,c] → c 移到 a 前 → [c,a,b]
        World world;
        auto   ids = MakeRootWithChildren(world, 3);
        Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];
        EditorHierarchy::MoveBefore(world, c, a);
        assert(ChildrenOf(world, root) == (std::vector<Entity>{c, a, b}));
    }
    {   // MoveAfter：[a,b,c] → a 移到 c 后 → [b,c,a]
        World world;
        auto   ids = MakeRootWithChildren(world, 3);
        Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];
        EditorHierarchy::MoveAfter(world, a, c);
        assert(ChildrenOf(world, root) == (std::vector<Entity>{b, c, a}));
    }
    {   // 相邻 no-op：[a,b,c]，MoveBefore(b, c) b 本就在 c 前 → 链不变
        World world;
        auto   ids = MakeRootWithChildren(world, 3);
        Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];
        EditorHierarchy::MoveBefore(world, b, c);
        assert(ChildrenOf(world, root) == (std::vector<Entity>{a, b, c}));
    }
    {   // 相邻 no-op：[a,b,c]，MoveAfter(b, a) b 本就在 a 后 → 链不变
        World world;
        auto   ids = MakeRootWithChildren(world, 3);
        Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];
        EditorHierarchy::MoveAfter(world, b, a);
        assert(ChildrenOf(world, root) == (std::vector<Entity>{a, b, c}));
    }
    {   // 跨父定位：pA=[x]，pB=[t]，MoveBefore(x, t) → x 改挂 pB、在 t 前
        World world;
        Entity pA = world.CreateEntity(); world.AddComponent<HC>(pA, {});
        Entity pB = world.CreateEntity(); world.AddComponent<HC>(pB, {});
        Entity x = world.CreateEntity(); EditorHierarchy::LinkAsLastChild(world, pA, x);
        Entity t = world.CreateEntity(); EditorHierarchy::LinkAsLastChild(world, pB, t);
        EditorHierarchy::MoveBefore(world, x, t);
        assert(ChildrenOf(world, pA).empty());
        assert(ChildrenOf(world, pB) == (std::vector<Entity>{x, t}));
        assert(world.GetComponent<HC>(x)->parent == pB);
    }
    std::fprintf(stdout, "  [PASS] MoveBefore/MoveAfter (reorder/cross-parent/adjacent no-op)\n");
}

void TestUndoRoundTrip()
{
    // 模拟 EntityTreePanel 帧末 apply 的 undo：记录旧 (parent, prevSibling)，
    // 一次 reorder 后用 MoveToPosition 原位复位，链必须逐位还原。
    World world;
    auto   ids = MakeRootWithChildren(world, 3);   // root, a, b, c
    Entity root = ids[0], a = ids[1], b = ids[2], c = ids[3];

    // 记录 b 的旧位置
    const HC*    bh        = world.GetComponent<HC>(b);
    const Entity oldParent = bh->parent;        // root
    const Entity oldPrev   = bh->prevSibling;   // a

    EditorHierarchy::MoveAfter(world, b, c);                  // [a,c,b]
    assert(ChildrenOf(world, root) == (std::vector<Entity>{a, c, b}));

    EditorHierarchy::MoveToPosition(world, b, oldParent, oldPrev);  // undo
    assert(ChildrenOf(world, root) == (std::vector<Entity>{a, b, c}));

    std::fprintf(stdout, "  [PASS] undo round-trip restores exact sibling position\n");
}

void TestIsAncestorOf()
{
    // root → mid → leaf；sib 是 root 的另一个子
    World  world;
    Entity root = world.CreateEntity(); world.AddComponent<HC>(root, {});
    Entity mid  = world.CreateEntity(); EditorHierarchy::LinkAsLastChild(world, root, mid);
    Entity leaf = world.CreateEntity(); EditorHierarchy::LinkAsLastChild(world, mid, leaf);
    Entity sib  = world.CreateEntity(); EditorHierarchy::LinkAsLastChild(world, root, sib);

    assert(EditorHierarchy::IsAncestorOf(world, root, root));   // 含自身
    assert(EditorHierarchy::IsAncestorOf(world, root, mid));    // 直接
    assert(EditorHierarchy::IsAncestorOf(world, root, leaf));   // 传递
    assert(EditorHierarchy::IsAncestorOf(world, mid, leaf));
    assert(!EditorHierarchy::IsAncestorOf(world, leaf, root));  // 反向不成立
    assert(!EditorHierarchy::IsAncestorOf(world, sib, leaf));   // 无关
    assert(!EditorHierarchy::IsAncestorOf(world, Entity::Invalid(), leaf));

    std::fprintf(stdout, "  [PASS] IsAncestorOf (self/direct/transitive/negatives)\n");
}

void TestDestroySubtree()
{
    // root → [c1 → gc, c2]；DestroySubtree(c1) 应销毁 c1+gc、root 只剩 c2
    World  world;
    Entity root = world.CreateEntity(); world.AddComponent<HC>(root, {});
    Entity c1   = world.CreateEntity(); EditorHierarchy::LinkAsLastChild(world, root, c1);
    Entity gc   = world.CreateEntity(); EditorHierarchy::LinkAsLastChild(world, c1, gc);
    Entity c2   = world.CreateEntity(); EditorHierarchy::LinkAsLastChild(world, root, c2);

    EditorHierarchy::DestroySubtree(world, c1);

    assert(!world.IsValid(c1));
    assert(!world.IsValid(gc));
    assert(world.IsValid(c2));
    assert(ChildrenOf(world, root) == (std::vector<Entity>{c2}));

    std::fprintf(stdout, "  [PASS] DestroySubtree (recursive + detach from parent)\n");
}

void TestMoveRootRelative()
{
    // 3 个根 A,B,C（sortIndex 全默认 0 → 初始按 entity id 序 = 创建序 A,B,C）。
    World  world;
    Entity A = world.CreateEntity(); world.AddComponent<HC>(A, {});
    Entity B = world.CreateEntity(); world.AddComponent<HC>(B, {});
    Entity C = world.CreateEntity(); world.AddComponent<HC>(C, {});
    assert(RootOrder(world) == (std::vector<Entity>{A, B, C}));

    // C 上移一位 → A,C,B
    assert(EditorHierarchy::MoveRootRelative(world, C, -1));
    assert(RootOrder(world) == (std::vector<Entity>{A, C, B}));

    // C 再上移 → C,A,B
    assert(EditorHierarchy::MoveRootRelative(world, C, -1));
    assert(RootOrder(world) == (std::vector<Entity>{C, A, B}));

    // C 已在最前，再上移 → clamp no-op、返回 false、序不变
    assert(!EditorHierarchy::MoveRootRelative(world, C, -1));
    assert(RootOrder(world) == (std::vector<Entity>{C, A, B}));

    // A 下移一位 → C,B,A
    assert(EditorHierarchy::MoveRootRelative(world, A, +1));
    assert(RootOrder(world) == (std::vector<Entity>{C, B, A}));

    // undo 语义（编辑器 LambdaCommand 反向）：MoveRootRelative(A,-1) 还原 → C,A,B
    assert(EditorHierarchy::MoveRootRelative(world, A, -1));
    assert(RootOrder(world) == (std::vector<Entity>{C, A, B}));

    // 非根不可移：把 A 挂到 C 下成非根，MoveRootRelative(A,...) → false
    EditorHierarchy::ReparentTo(world, A, C);
    assert(!EditorHierarchy::MoveRootRelative(world, A, -1));

    // 单根：no-op、false
    {
        World  w2;
        Entity only = w2.CreateEntity(); w2.AddComponent<HC>(only, {});
        assert(!EditorHierarchy::MoveRootRelative(w2, only, -1));
    }

    std::fprintf(stdout, "  [PASS] MoveRootRelative (up/down/clamp/undo/non-root/single)\n");
}

void TestMoveRootRelativeLazyHierarchy()
{
    // 编辑器 Create Entity 的真实路径：新建 root 实体只挂 Name/Transform，
    // **不挂 HierarchyComponent**——它是 root 但无 HC。历史 bug：
    // MoveRootRelative 首行 GetComponent<HC> 取空直接 return false → 新建节点
    // Move Up/Down 静默 no-op（BUG-2026-06-01-new-root-entity-move-noop）。
    // 修复后：MoveRootRelative 惰性补默认 HC（parent invalid 合法根）并正常
    // reorder。
    World  world;
    Entity A = world.CreateEntity(); world.AddComponent<HC>(A, {});
    Entity B = world.CreateEntity(); world.AddComponent<HC>(B, {});
    Entity N = world.CreateEntity();                 // 模拟新建：无 HC
    assert(world.GetComponent<HC>(N) == nullptr);    // 前置：确实无 HC
    // 全 sortIndex 0（N 无 HC 视 0）→ 按 id 序 = 创建序 A,B,N。
    assert(RootOrder(world) == (std::vector<Entity>{A, B, N}));

    // 对无 HC 的 N 上移一位 → 不再 no-op：返回 true、惰性补 HC、序变 A,N,B。
    assert(EditorHierarchy::MoveRootRelative(world, N, -1));
    assert(world.GetComponent<HC>(N) != nullptr);    // 惰性补了 HC
    assert(RootOrder(world) == (std::vector<Entity>{A, N, B}));

    // 再上移 → N,A,B；已在最前再上移 clamp no-op、false、序不变。
    assert(EditorHierarchy::MoveRootRelative(world, N, -1));
    assert(RootOrder(world) == (std::vector<Entity>{N, A, B}));
    assert(!EditorHierarchy::MoveRootRelative(world, N, -1));
    assert(RootOrder(world) == (std::vector<Entity>{N, A, B}));

    std::fprintf(stdout, "  [PASS] MoveRootRelative 无 HC root 惰性补 HC + reorder\n");
}

void TestMoveRootRelativeMixedHierarchy()
{
    // 混合根：A 有 HC，N1/N2 是"新建未挂 HC"的根（编辑器 Create Entity 路径）。
    // 历史 bug（BUG-2026-06-01，dogfood 逮到"Move 直接跳到顶/底"）：规整 sortIndex
    // 时只写有 HC 的根，N1/N2 不被规整、sortIndex 恒 0，导致 Move 一个时排序错乱。
    // 修复后：规整给**所有**参与根补 HC + 统一写 sortIndex，reorder 是稳定的
    // "移一位"。
    World  world;
    Entity A  = world.CreateEntity(); world.AddComponent<HC>(A, {});
    Entity N1 = world.CreateEntity();   // 无 HC（新建）
    Entity N2 = world.CreateEntity();   // 无 HC（新建）
    // 全 sortIndex 0（N1/N2 无 HC 视 0）→ 按 id 序 = A, N1, N2。
    assert(RootOrder(world) == (std::vector<Entity>{A, N1, N2}));

    // N2 上移一位 → A, N2, N1（而非跳到顶/底）。修复前 N1 未被规整会致错乱。
    assert(EditorHierarchy::MoveRootRelative(world, N2, -1));
    assert(RootOrder(world) == (std::vector<Entity>{A, N2, N1}));
    // 修复关键：所有根现在都有 HC 且 sortIndex 一致 0,1,2。
    assert(world.GetComponent<HC>(A)  != nullptr && world.GetComponent<HC>(A)->sortIndex  == 0);
    assert(world.GetComponent<HC>(N2) != nullptr && world.GetComponent<HC>(N2)->sortIndex == 1);
    assert(world.GetComponent<HC>(N1) != nullptr && world.GetComponent<HC>(N1)->sortIndex == 2);

    // 再 N2 上移一位 → N2, A, N1（稳定移一位，不跳）。
    assert(EditorHierarchy::MoveRootRelative(world, N2, -1));
    assert(RootOrder(world) == (std::vector<Entity>{N2, A, N1}));

    // N2 已在最前，再上移 → clamp no-op、false、序不变。
    assert(!EditorHierarchy::MoveRootRelative(world, N2, -1));
    assert(RootOrder(world) == (std::vector<Entity>{N2, A, N1}));

    std::fprintf(stdout, "  [PASS] MoveRootRelative 混合 HC 根（多新建节点）reorder 稳定\n");
}

void TestMoveRootRelativeDryRun()
{
    // dryRun=true：做同样的"能否移动"判断，但**不改任何状态**。编辑器 rootMove
    // 用它预检，真正移动只交给命令栈 Execute 一次——否则"判断时执行一次 + 命令栈
    // Execute 再执行一次" = 移两位（BUG-2026-06-01-root-reorder-double-apply，
    // dogfood 见 reorder 直接跳顶/底）。
    World  world;
    Entity A = world.CreateEntity(); world.AddComponent<HC>(A, {});
    Entity B = world.CreateEntity(); world.AddComponent<HC>(B, {});
    Entity C = world.CreateEntity(); world.AddComponent<HC>(C, {});
    assert(RootOrder(world) == (std::vector<Entity>{A, B, C}));

    // 能移：dryRun 返回 true 但顺序不变；连续多次 dryRun 不累积。
    assert(EditorHierarchy::MoveRootRelative(world, C, -1, /*dryRun*/ true));
    assert(RootOrder(world) == (std::vector<Entity>{A, B, C}));
    assert(EditorHierarchy::MoveRootRelative(world, C, -1, /*dryRun*/ true));
    assert(RootOrder(world) == (std::vector<Entity>{A, B, C}));

    // 边界：A 已在最前，dryRun 上移返回 false、不改状态。
    assert(!EditorHierarchy::MoveRootRelative(world, A, -1, /*dryRun*/ true));
    assert(RootOrder(world) == (std::vector<Entity>{A, B, C}));

    // 真执行（dryRun=false）才移一位 —— 模拟命令栈 Execute 的单次应用。
    assert(EditorHierarchy::MoveRootRelative(world, C, -1, /*dryRun*/ false));
    assert(RootOrder(world) == (std::vector<Entity>{A, C, B}));

    std::fprintf(stdout, "  [PASS] MoveRootRelative dryRun 只判断不改状态（防双重应用）\n");
}

void TestSortIndexSerializationRoundTrip()
{
    // reorder 后 sortIndex 必须随 HierarchyComponent 序列化往返保持（schema 1.12）。
    World  world;
    Entity A = world.CreateEntity(); world.AddComponent<HC>(A, {});
    Entity B = world.CreateEntity(); world.AddComponent<HC>(B, {});
    Entity C = world.CreateEntity(); world.AddComponent<HC>(C, {});
    EditorHierarchy::MoveRootRelative(world, C, -1);
    EditorHierarchy::MoveRootRelative(world, C, -1);  // 规整后 C=0, A=1, B=2

    const std::vector<Entity> roots{A, B, C};
    auto blob = Scene::SaveSubtreeToString(world, roots);
    assert(!blob.IsErr());

    World               world2;
    std::vector<Entity> created;
    auto rc = Scene::LoadFromString(blob.Value(), world2, {}, &created);
    assert(!rc.IsErr());
    assert(created.size() == 3);

    // 新 world 根序按 sortIndex：第 i 个根的 sortIndex == i（0,1,2 原样保持）。
    const auto order = RootOrder(world2);
    assert(order.size() == 3);
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        const HC* h = world2.GetComponent<HC>(order[i]);
        assert(h != nullptr);
        assert(h->sortIndex == static_cast<int>(i));
    }

    std::fprintf(stdout, "  [PASS] sortIndex 序列化往返 + 根序保持\n");
}

// ReparentToKeepWorld / MoveToPositionKeepWorld（ADR-016 / A1.3）——reparent 改父
// 链前后保持 child 世界位姿不变（A1.1 累积 hierarchy 后防跳位）。自逆：undo 复位
// 也保 world、还原原始 local。
void TestReparentKeepWorld()
{
    using TC = Orange::Engine::Scene::TransformComponent;
    World world;

    // A 在世界 (5,0,0)，B 在世界 (0,0,0)，都是 root。
    Entity a = world.CreateEntity();
    world.AddComponent<TC>(a, TC{glm::vec3(5.0f, 0.0f, 0.0f),
                                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                 glm::vec3(1.0f)});
    Entity b = world.CreateEntity();
    world.AddComponent<TC>(b, TC{glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                 glm::vec3(1.0f)});

    // keep-world reparent B 到 A 下：B 世界仍 (0,0,0) → B.local = inv(A 世界
    // (5,0,0)) * (0,0,0) = (-5,0,0)。
    EditorHierarchy::ReparentToKeepWorld(world, b, a);
    const TC* bt = world.GetComponent<TC>(b);
    assert(bt != nullptr);
    assert(std::fabs(bt->position.x - (-5.0f)) < 1e-4f &&
           std::fabs(bt->position.y - 0.0f) < 1e-4f &&
           std::fabs(bt->position.z - 0.0f) < 1e-4f &&
           "keep-world reparent：B.local 应 = (-5,0,0)，使 A(5,0,0)×local = world(0,0,0) 不变");
    const HC* bh = world.GetComponent<HC>(b);
    assert(bh != nullptr && bh->parent == a && "B 应已 parent 到 A");

    // 自逆：reparent B 回 root（MoveToPositionKeepWorld parent=Invalid）→ B.local
    // 应还原原始 (0,0,0)（world 仍 (0,0,0)）。
    EditorHierarchy::MoveToPositionKeepWorld(world, b, Entity::Invalid(), Entity::Invalid());
    bt = world.GetComponent<TC>(b);
    assert(bt != nullptr &&
           std::fabs(bt->position.x - 0.0f) < 1e-4f &&
           std::fabs(bt->position.y - 0.0f) < 1e-4f &&
           std::fabs(bt->position.z - 0.0f) < 1e-4f &&
           "keep-world 自逆：reparent 回 root 后 B.local 应还原 (0,0,0)");

    std::fprintf(stdout,
                 "  [PASS] keep-world reparent：B 世界位姿保持 (B.local (-5,0,0)) + 自逆还原\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[EditorHierarchyTest] running\n");
    TestReparentKeepWorld();
    TestLinkAsLastChild();
    TestDetachHeadMiddleTail();
    TestReparentTo();
    TestMoveToPosition();
    TestMoveBeforeAfter();
    TestUndoRoundTrip();
    TestIsAncestorOf();
    TestDestroySubtree();
    TestMoveRootRelative();
    TestMoveRootRelativeLazyHierarchy();
    TestMoveRootRelativeMixedHierarchy();
    TestMoveRootRelativeDryRun();
    TestSortIndexSerializationRoundTrip();
    std::fprintf(stdout, "[EditorHierarchyTest] all tests passed.\n");
    return 0;
}
