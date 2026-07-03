// samples/02_ecs_basics —— 控制台 sample，演示 OrangeEngine ECS 的最
// 小心智模型：实体生命周期、组件 CRUD、Hierarchy 链构造、entt 视图
// 遍历。
//
// 刻意不开窗口、不接 Pipeline——本 sample 把 ECS 数据流形单独呈现。
// 真正用 Pipeline 渲染的 sample 在后续任务里（textured_quad、3d_mesh
// 之类）。

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cstdio>

using namespace Orange::Engine;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    void DumpEntity(const World& world, const char* label, Entity entity)
    {
        if (!world.IsValid(entity))
        {
            std::printf("  %-7s = (invalid handle %llu)\n",
                        label, static_cast<unsigned long long>(entity.Value()));
            return;
        }
        const auto* xform = world.GetComponent<TransformComponent>(entity);
        if (xform)
        {
            std::printf("  %-7s = entity #%llu  pos=(%.1f, %.1f, %.1f)  scale=(%.1f, %.1f, %.1f)\n",
                        label,
                        static_cast<unsigned long long>(entity.Value()),
                        xform->position.x, xform->position.y, xform->position.z,
                        xform->scale.x, xform->scale.y, xform->scale.z);
        }
        else
        {
            std::printf("  %-7s = entity #%llu  (no TransformComponent)\n",
                        label, static_cast<unsigned long long>(entity.Value()));
        }
    }

    void DumpHierarchy(const World& world, Entity entity, const char* label)
    {
        const auto* h = world.GetComponent<HierarchyComponent>(entity);
        if (h == nullptr)
        {
            std::printf("  %-7s = (no HierarchyComponent)\n", label);
            return;
        }
        auto valueOrNull = [](Entity e) -> long long
        {
            return e.IsValid() ? static_cast<long long>(e.Value()) : -1;
        };
        std::printf("  %-7s = parent=%lld  firstChild=%lld  prev=%lld  next=%lld\n",
                    label,
                    valueOrNull(h->parent),
                    valueOrNull(h->firstChild),
                    valueOrNull(h->prevSibling),
                    valueOrNull(h->nextSibling));
    }

} // namespace

int main()
{
    std::printf("=== OrangeEngine ECS basics ===\n\n");

    World world;
    std::printf("[1] 创建 World：Size=%zu, Empty=%s\n",
                world.Size(), world.Empty() ? "true" : "false");

    // ----- 创建 3 个实体并加 Transform ------------------------------------
    Entity hero   = world.CreateEntity();
    Entity sword  = world.CreateEntity();
    Entity shield = world.CreateEntity();

    {
        TransformComponent t;
        t.position = {0.0f, 0.0f, 0.0f};
        world.AddComponent(hero, t);
    }
    {
        TransformComponent t;
        t.position = {0.5f, 0.0f, 0.0f};
        t.scale    = {0.4f, 0.4f, 0.4f};
        world.AddComponent(sword, t);
    }
    {
        TransformComponent t;
        t.position = {-0.5f, 0.0f, 0.0f};
        t.scale    = {0.5f, 0.5f, 0.5f};
        world.AddComponent(shield, t);
    }
    std::printf("\n[2] 创建 3 个实体（hero / sword / shield）并 AddComponent<TransformComponent>：\n");
    DumpEntity(world, "hero", hero);
    DumpEntity(world, "sword", sword);
    DumpEntity(world, "shield", shield);
    std::printf("    World::Size=%zu\n", world.Size());

    // ----- Has / Get 检查 ------------------------------------------------
    std::printf("\n[3] HasComponent / GetComponent 查询：\n");
    std::printf("    HasComponent<Transform>(hero) = %s\n",
                world.HasComponent<TransformComponent>(hero) ? "true" : "false");
    std::printf("    HasComponent<Hierarchy>(hero) = %s  （还没加）\n",
                world.HasComponent<HierarchyComponent>(hero) ? "true" : "false");

    // ----- 构造 Hierarchy 链：hero 是 sword/shield 的父节点 ----------------
    {
        HierarchyComponent h{};
        h.firstChild = sword;
        world.AddComponent(hero, h);
    }
    {
        HierarchyComponent h{};
        h.parent      = hero;
        h.nextSibling = shield;
        world.AddComponent(sword, h);
    }
    {
        HierarchyComponent h{};
        h.parent      = hero;
        h.prevSibling = sword;
        world.AddComponent(shield, h);
    }
    std::printf("\n[4] 用 HierarchyComponent 把 hero 串成 sword + shield 的父：\n");
    DumpHierarchy(world, hero, "hero");
    DumpHierarchy(world, sword, "sword");
    DumpHierarchy(world, shield, "shield");

    // ----- entt::view 遍历：所有挂 Transform 的实体 ------------------------
    std::printf("\n[5] 遍历 view<TransformComponent>（顺序由 EnTT 内部存储决定）：\n");
    auto&       reg     = world.Registry();
    std::size_t visited = 0;
    auto        view    = reg.view<TransformComponent>();
    for (auto e : view)
    {
        const auto& t = view.get<TransformComponent>(e);
        std::printf("    entity #%llu  pos=(%.1f, %.1f, %.1f)\n",
                    static_cast<unsigned long long>(World::FromEntt(e).Value()),
                    t.position.x, t.position.y, t.position.z);
        ++visited;
    }
    std::printf("    遍历到 %zu 个实体\n", visited);

    // ----- RemoveComponent：sword 卸下 Transform --------------------------
    world.RemoveComponent<TransformComponent>(sword);
    std::printf("\n[6] RemoveComponent<Transform>(sword) 之后：\n");
    DumpEntity(world, "sword", sword);
    std::printf("    HasComponent<Transform>(sword) = %s\n",
                world.HasComponent<TransformComponent>(sword) ? "true" : "false");

    // ----- DestroyEntity：shield 销毁后挂在它上的组件被一并卸下 ------------
    world.DestroyEntity(shield);
    std::printf("\n[7] DestroyEntity(shield) 之后：\n");
    DumpEntity(world, "shield", shield);
    std::printf("    World::IsValid(shield) = %s\n",
                world.IsValid(shield) ? "true" : "false");
    std::printf("    World::Size=%zu\n", world.Size());

    std::printf("\n=== 完成。剩余实体：");
    auto remainView = reg.view<TransformComponent>();
    bool first      = true;
    for (auto e : remainView)
    {
        std::printf("%s#%llu",
                    first ? "" : ", ",
                    static_cast<unsigned long long>(World::FromEntt(e).Value()));
        first = false;
    }
    if (first)
    {
        std::printf("(无 Transform 的实体)");
    }
    std::printf(" ===\n");

    return 0;
}
