#include <orange/engine/scene/TransformSystem.h>

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <glm/gtc/matrix_transform.hpp>  // glm::translate / glm::scale
#include <glm/gtc/quaternion.hpp>        // glm::mat4_cast

#include <vector>

namespace Orange::Engine::Scene
{
namespace
{

// 单实体 local TRS → mat4，与 RenderScene::ComposeWorldMatrix 同款合成顺序
// T * R * S（列向量惯例：worldVec = M * localVec）。A1.1 step 2 切消费者后
// RenderScene 那份 file-local helper 可删，统一走本 pass。
glm::mat4 LocalMatrix(const TransformComponent& t)
{
    glm::mat4 m(1.0f);
    m = glm::translate(m, t.position);
    m = m * glm::mat4_cast(t.rotation);
    m = glm::scale(m, t.scale);
    return m;
}

// 层级递归深度上限——畸形/损坏 scene.json 可能含 firstChild 环（A→B→A），DFS
// 会无限递归直至栈溢出硬崩。编辑器 reparent 有 IsAncestorOf 禁环，正常用不会出
// 环；本上限是对"手改/损坏场景文件"的兜底（成熟引擎不该被畸形数据栈溢出）。
// 真实场景层级远浅于此（深嵌套骨架/group 也罕见过百），撞上限即停、不递归更深，
// 避免硬崩；无堆分配，每帧 hot-path 仅多一个 int 比较。
constexpr int kMaxHierarchyDepth = 1024;

// 递归：本 entity world = parentWorld * local，写 cache，再递归子节点。
// 不跨 AddComponent 持有 HierarchyComponent 指针——firstChild 先读进局部，
// 子循环每轮重取 nextSibling（AddComponent<WorldTransform> 不动 Hierarchy
// pool，但稳妥从严）。
void PropagateRecursive(World& world, Entity e, const glm::mat4& parentWorld, int depth)
{
    // 超深（含环）兜底：停止递归，避免栈溢出。本 entity 的 world cache 仍写入
    //（按当前 parentWorld 累积），只是不再向更深子节点传播。
    if (depth >= kMaxHierarchyDepth)
    {
        return;
    }

    const auto* t = world.GetComponent<TransformComponent>(e);
    const glm::mat4 local  = (t != nullptr) ? LocalMatrix(*t) : glm::mat4(1.0f);
    const glm::mat4 worldM = parentWorld * local;
    world.AddComponent<WorldTransformComponent>(e, WorldTransformComponent{worldM});

    const auto* h = world.GetComponent<HierarchyComponent>(e);
    if (h == nullptr)
    {
        return;
    }
    Entity child = h->firstChild;
    while (world.IsValid(child))
    {
        PropagateRecursive(world, child, worldM, depth + 1);
        const auto* hc = world.GetComponent<HierarchyComponent>(child);
        child = (hc != nullptr) ? hc->nextSibling : Entity::Invalid();
    }
}

}  // namespace

void PropagateWorldTransforms(World& world)
{
    auto& reg = world.Registry();

    // 先收集根（parent==Invalid 或无 HierarchyComponent）——在 AddComponent 之前
    // 完成 view 迭代，避免迭代中改 component pool。WorldTransformComponent 与
    // TransformComponent 是不同 pool，加 world cache 不影响本 view，但收集后再
    // 处理最干净。
    std::vector<Entity> roots;
    for (auto e : reg.view<TransformComponent>())
    {
        const Entity ent = World::FromEntt(e);
        const auto*  h   = world.GetComponent<HierarchyComponent>(ent);
        if (h == nullptr || h->parent == Entity::Invalid())
        {
            roots.push_back(ent);
        }
    }

    for (const Entity r : roots)
    {
        PropagateRecursive(world, r, glm::mat4(1.0f), 0);
    }
}

}  // namespace Orange::Engine::Scene
