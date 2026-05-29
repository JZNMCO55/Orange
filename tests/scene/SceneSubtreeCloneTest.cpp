// 子树 clone 往返单测 —— SaveSubtreeToString + LoadFromString。
//
// 这是 Duplicate / Copy-Paste / delete-undo 的共同基建：把一棵子树序列化成
// 内存 JSON，再加载回去得到一份**内部引用已重映射到新实体**的克隆。本测试
// 锁住最关键的数据正确性——克隆子节点的 parent 指向克隆根（而非原始根），
// 克隆根的 firstChild 指向克隆子节点（remap 错 = 引用错位/悬挂，是 gap 报告
// 反复警告的风险）。纯逻辑、headless 可测。

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::TransformComponent;

namespace Scene = Orange::Engine::Scene;

namespace
{

void TestSubtreeCloneRemapsInternalRefs()
{
    World w;
    const Entity root  = w.CreateEntity();
    const Entity child = w.CreateEntity();

    w.AddComponent<NameComponent>(root, NameComponent{"Root"});
    w.AddComponent<NameComponent>(child, NameComponent{"Child"});
    {
        TransformComponent tc;
        tc.position = {1.0f, 2.0f, 3.0f};
        w.AddComponent<TransformComponent>(root, tc);
    }
    w.AddComponent<TransformComponent>(child, TransformComponent{});

    // hierarchy：root.firstChild = child，child.parent = root。
    {
        HierarchyComponent rh;
        rh.firstChild = child;
        w.AddComponent<HierarchyComponent>(root, rh);
        HierarchyComponent ch;
        ch.parent = root;
        w.AddComponent<HierarchyComponent>(child, ch);
    }

    const std::array<Entity, 1> roots{root};
    auto blob = Scene::SaveSubtreeToString(w, roots);
    assert(blob.IsOk());
    assert(!blob.Value().empty());

    std::vector<Entity> created;
    auto lr = Scene::LoadFromString(blob.Value(), w, {}, &created);
    assert(lr.IsOk());
    assert(created.size() == 2);  // root + child 的克隆

    // 找克隆根（subtree 根的 parent 在子树外 → 未序列化 → 克隆根 parent 失效）
    // 与克隆子节点。
    Entity cloneRoot  = Entity::Invalid();
    Entity cloneChild = Entity::Invalid();
    for (const Entity e : created)
    {
        const auto* h = w.GetComponent<HierarchyComponent>(e);
        assert(h != nullptr);
        if (!h->parent.IsValid()) { cloneRoot = e; }
        else                      { cloneChild = e; }
    }
    assert(cloneRoot.IsValid() && cloneChild.IsValid());
    // 克隆是全新实体，不等于原件。
    assert(cloneRoot != root && cloneRoot != child);
    assert(cloneChild != root && cloneChild != child);

    // 核心断言：克隆内部引用重映射到克隆自身，而非原始实体。
    const auto* crh = w.GetComponent<HierarchyComponent>(cloneRoot);
    const auto* cch = w.GetComponent<HierarchyComponent>(cloneChild);
    assert(crh != nullptr && cch != nullptr);
    assert(crh->firstChild == cloneChild);  // 指向克隆子，不是原 child
    assert(cch->parent == cloneRoot);        // 指向克隆根，不是原 root

    // 字段保真。
    assert(w.GetComponent<NameComponent>(cloneRoot)->name == "Root");
    assert(w.GetComponent<NameComponent>(cloneChild)->name == "Child");
    const auto* crtc = w.GetComponent<TransformComponent>(cloneRoot);
    assert(crtc != nullptr);
    assert(crtc->position.x == 1.0f && crtc->position.y == 2.0f && crtc->position.z == 3.0f);

    // 原始子树未被克隆操作改动。
    assert(w.GetComponent<HierarchyComponent>(root)->firstChild == child);
    assert(w.GetComponent<HierarchyComponent>(child)->parent == root);

    std::fprintf(stdout, "  [PASS] 子树 clone 内部引用 remap + 字段保真 + 原件不变\n");
}

void TestEmptyRootsYieldsNoClone()
{
    World w;
    const Entity e = w.CreateEntity();
    w.AddComponent<NameComponent>(e, NameComponent{"Solo"});

    // 空 roots → 空子树 → 序列化出 0 实体；LoadFromString 不新建。
    const std::array<Entity, 0> noRoots{};
    auto blob = Scene::SaveSubtreeToString(w, noRoots);
    assert(blob.IsOk());
    std::vector<Entity> created;
    auto lr = Scene::LoadFromString(blob.Value(), w, {}, &created);
    assert(lr.IsOk());
    assert(created.empty());

    std::fprintf(stdout, "  [PASS] 空 roots → 无克隆\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[SceneSubtreeCloneTest] running\n");
    TestSubtreeCloneRemapsInternalRefs();
    TestEmptyRootsYieldsNoClone();
    std::fprintf(stdout, "[SceneSubtreeCloneTest] all tests passed.\n");
    return 0;
}
