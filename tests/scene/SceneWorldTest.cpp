// SceneWorld 端到端单元测试。
//
// 覆盖：
//   * 实体生命周期（Create / Destroy / IsValid / Size）
//   * 组件 CRUD（AddComponent / HasComponent / GetComponent /
//     RemoveComponent）
//   * 销毁实体后挂在其上的组件被一并卸载
//   * Hierarchy 链：父-子-兄弟双向链的常见操作
//   * 直接拿 entt::registry 跑 view 验证 EnTT 路径完整可用

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cstdio>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    void TestEntityLifecycle()
    {
        World world;
        assert(world.Empty());

        Entity a = world.CreateEntity();
        Entity b = world.CreateEntity();
        Entity c = world.CreateEntity();

        assert(a.IsValid() && b.IsValid() && c.IsValid());
        assert(a != b && b != c && a != c);
        assert(world.Size() == 3);

        assert(world.IsValid(a));
        world.DestroyEntity(b);
        assert(world.Size() == 2);
        assert(!world.IsValid(b));
        assert(world.IsValid(a));
        assert(world.IsValid(c));

        // 销毁无效 handle 不应崩溃 / 改变状态。
        world.DestroyEntity(b);
        world.DestroyEntity(Entity::Invalid());
        assert(world.Size() == 2);

        std::fprintf(stdout, "  [PASS] entity lifecycle\n");
    }

    void TestComponentCrud()
    {
        World  world;
        Entity e = world.CreateEntity();

        // 初始：无 component。
        assert(!world.HasComponent<TransformComponent>(e));
        assert(world.GetComponent<TransformComponent>(e) == nullptr);

        TransformComponent tx;
        tx.position = {1.0f, 2.0f, 3.0f};
        auto& added = world.AddComponent(e, tx);
        assert(added.position.x == 1.0f);

        assert(world.HasComponent<TransformComponent>(e));
        auto* fetched = world.GetComponent<TransformComponent>(e);
        assert(fetched != nullptr);
        assert(fetched->position.y == 2.0f);

        // const 路径
        const World& cworld   = world;
        const auto*  cfetched = cworld.GetComponent<TransformComponent>(e);
        assert(cfetched != nullptr);
        assert(cfetched->position.z == 3.0f);

        // RemoveComponent → 再 Get 应得 nullptr。
        world.RemoveComponent<TransformComponent>(e);
        assert(!world.HasComponent<TransformComponent>(e));
        assert(world.GetComponent<TransformComponent>(e) == nullptr);

        // Get 在无效 entity 上：稳定返回 nullptr，不崩。
        assert(world.GetComponent<TransformComponent>(Entity::Invalid()) == nullptr);
        assert(!world.HasComponent<TransformComponent>(Entity::Invalid()));

        std::fprintf(stdout, "  [PASS] component CRUD\n");
    }

    void TestDestroyClearsComponents()
    {
        World  world;
        Entity e = world.CreateEntity();
        world.AddComponent(e, TransformComponent{});
        assert(world.HasComponent<TransformComponent>(e));

        world.DestroyEntity(e);
        // 同 handle 现在无效；HasComponent 安全返回 false。
        assert(!world.IsValid(e));
        assert(!world.HasComponent<TransformComponent>(e));

        std::fprintf(stdout, "  [PASS] destroy clears components\n");
    }

    void TestHierarchyChain()
    {
        // 构造：root → [a, b, c]
        World  world;
        Entity root = world.CreateEntity();
        Entity a    = world.CreateEntity();
        Entity b    = world.CreateEntity();
        Entity c    = world.CreateEntity();

        world.AddComponent<HierarchyComponent>(root, {});
        world.AddComponent<HierarchyComponent>(a, {});
        world.AddComponent<HierarchyComponent>(b, {});
        world.AddComponent<HierarchyComponent>(c, {});

        auto* rootH = world.GetComponent<HierarchyComponent>(root);
        auto* aH    = world.GetComponent<HierarchyComponent>(a);
        auto* bH    = world.GetComponent<HierarchyComponent>(b);
        auto* cH    = world.GetComponent<HierarchyComponent>(c);
        assert(rootH && aH && bH && cH);

        // a → b → c, parent 都指向 root。
        rootH->firstChild = a;
        aH->parent        = root;
        aH->nextSibling   = b;
        bH->parent        = root;
        bH->prevSibling   = a;
        bH->nextSibling   = c;
        cH->parent        = root;
        cH->prevSibling   = b;

        // O(1) 摘除 b：链由双向 sibling 维护。
        auto* prev = world.GetComponent<HierarchyComponent>(bH->prevSibling);
        auto* next = world.GetComponent<HierarchyComponent>(bH->nextSibling);
        assert(prev && next);
        prev->nextSibling = bH->nextSibling;
        next->prevSibling = bH->prevSibling;
        bH->parent        = Entity::Invalid();
        bH->prevSibling   = Entity::Invalid();
        bH->nextSibling   = Entity::Invalid();

        // 验证链：root.firstChild == a；a.next == c；c.prev == a；
        assert(rootH->firstChild == a);
        assert(aH->nextSibling == c);
        assert(cH->prevSibling == a);

        std::fprintf(stdout, "  [PASS] hierarchy chain manipulation\n");
    }

    void TestRegistryViewEscapeHatch()
    {
        // 验证 Registry() 暴露的逃生舱口可以直接跑 EnTT view —— 这是
        // Render / Physics 后续做高效 batch query 的必要前提。
        World  world;
        Entity a = world.CreateEntity();
        Entity b = world.CreateEntity();
        world.AddComponent(a, TransformComponent{});
        world.AddComponent(a, HierarchyComponent{});
        world.AddComponent(b, TransformComponent{});
        // b 没有 HierarchyComponent，所以 (Transform, Hierarchy) view 只
        // 应命中 a 一个。

        auto&  reg  = world.Registry();
        auto   view = reg.view<TransformComponent, HierarchyComponent>();
        int    hits = 0;
        Entity hit  = Entity::Invalid();
        for (auto e : view)
        {
            ++hits;
            hit = World::FromEntt(e);
        }
        assert(hits == 1);
        assert(hit == a);

        std::fprintf(stdout, "  [PASS] entt::registry view escape hatch\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[SceneWorldTest] running\n");
    TestEntityLifecycle();
    TestComponentCrud();
    TestDestroyClearsComponents();
    TestHierarchyChain();
    TestRegistryViewEscapeHatch();
    std::fprintf(stdout, "[SceneWorldTest] all tests passed.\n");
    return 0;
}
