// EditorHierarchy 实现 —— 见 EditorHierarchy.h 的注释。

#include "EditorHierarchy.h"

#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/World.h>

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{

using ::Orange::Engine::Entity;
using ::Orange::Engine::World;
using HC = ::Orange::Engine::Scene::HierarchyComponent;

HC& GetOrAdd(World& world, Entity e)
{
    if (auto* p = world.GetComponent<HC>(e)) { return *p; }
    return world.AddComponent<HC>(e, HC{});
}

}  // namespace

namespace EditorHierarchy
{

void LinkAsLastChild(World& world, Entity parent, Entity child)
{
    HC& pc = GetOrAdd(world, parent);
    HC& cc = GetOrAdd(world, child);
    cc.parent = parent;
    if (!pc.firstChild.IsValid()) {
        pc.firstChild = child;
        return;
    }
    // 走到尾兄弟
    Entity cur = pc.firstChild;
    while (true) {
        HC* h = world.GetComponent<HC>(cur);
        if (h == nullptr || !h->nextSibling.IsValid()) { break; }
        cur = h->nextSibling;
    }
    HC* tail = world.GetComponent<HC>(cur);
    tail->nextSibling = child;
    cc.prevSibling    = cur;
}

void Detach(World& world, Entity e)
{
    HC* h = world.GetComponent<HC>(e);
    if (h == nullptr) { return; }
    if (!h->parent.IsValid()) { return; }  // 已经是 root

    Entity parent = h->parent;
    Entity prev   = h->prevSibling;
    Entity next   = h->nextSibling;

    // 修兄弟链
    if (prev.IsValid()) {
        if (HC* ph = world.GetComponent<HC>(prev)) { ph->nextSibling = next; }
    } else {
        // 自己是 firstChild —— 让父亲的 firstChild 指向 next
        if (HC* pp = world.GetComponent<HC>(parent)) { pp->firstChild = next; }
    }
    if (next.IsValid()) {
        if (HC* nh = world.GetComponent<HC>(next)) { nh->prevSibling = prev; }
    }

    h->parent      = Entity::Invalid();
    h->prevSibling = Entity::Invalid();
    h->nextSibling = Entity::Invalid();
}

bool IsAncestorOf(World& world, Entity ancestor, Entity descendant)
{
    if (!ancestor.IsValid() || !descendant.IsValid()) { return false; }
    Entity cur = descendant;
    while (cur.IsValid()) {
        if (cur == ancestor) { return true; }
        const HC* h = world.GetComponent<HC>(cur);
        if (h == nullptr) { return false; }
        cur = h->parent;
    }
    return false;
}

void ReparentTo(World& world, Entity child, Entity newParent)
{
    Detach(world, child);
    if (newParent.IsValid()) {
        LinkAsLastChild(world, newParent, child);
    }
}

void MoveToPosition(World& world, Entity child, Entity parent, Entity afterSibling)
{
    Detach(world, child);                  // 先让出原位置，child 变 root
    if (!parent.IsValid()) { return; }     // 提到 root —— afterSibling 对根无意义

    HC& cc = GetOrAdd(world, child);
    HC& pc = GetOrAdd(world, parent);
    cc.parent = parent;

    if (!afterSibling.IsValid()) {
        // 插到子链最前（新 firstChild）
        const Entity oldFirst = pc.firstChild;
        pc.firstChild  = child;
        cc.prevSibling = Entity::Invalid();
        cc.nextSibling = oldFirst;
        if (oldFirst.IsValid()) {
            if (HC* oh = world.GetComponent<HC>(oldFirst)) { oh->prevSibling = child; }
        }
        return;
    }

    HC* ah = world.GetComponent<HC>(afterSibling);
    if (ah == nullptr || ah->parent != parent) {
        // 防御：afterSibling 不在 / 不属于 parent —— 退化为挂尾，保证不破链
        LinkAsLastChild(world, parent, child);
        return;
    }
    const Entity next = ah->nextSibling;
    ah->nextSibling = child;
    cc.prevSibling  = afterSibling;
    cc.nextSibling  = next;
    if (next.IsValid()) {
        if (HC* nh = world.GetComponent<HC>(next)) { nh->prevSibling = child; }
    }
}

void MoveBefore(World& world, Entity child, Entity target)
{
    if (child == target) { return; }
    HC* th = world.GetComponent<HC>(target);
    if (th == nullptr) { return; }
    if (th->prevSibling == child) { return; }   // 已紧邻 target 之前，no-op
    // th->parent / th->prevSibling 按值传入；MoveToPosition 内 Detach(child)
    // 不会改动这两个捕获值（child 与它们非同一节点——相邻情形已上面 no-op）。
    MoveToPosition(world, child, th->parent, th->prevSibling);
}

void MoveAfter(World& world, Entity child, Entity target)
{
    if (child == target) { return; }
    HC* th = world.GetComponent<HC>(target);
    if (th == nullptr) { return; }
    if (th->nextSibling == child) { return; }   // 已紧邻 target 之后，no-op
    MoveToPosition(world, child, th->parent, target);
}

void DestroySubtree(World& world, Entity e)
{
    if (!e.IsValid()) { return; }
    if (HC* h = world.GetComponent<HC>(e)) {
        // 收集 children 快照（不能边遍历边 destroy —— DestroyEntity 会让
        // 后续 GetComponent 返回 null，sibling 字段失效）。这里用 vector
        // 而非定长数组：编辑器允许任意 fan-out，没必要硬上限。
        std::vector<Entity> kids;
        Entity child = h->firstChild;
        while (child.IsValid()) {
            kids.push_back(child);
            const HC* ch = world.GetComponent<HC>(child);
            child = (ch != nullptr) ? ch->nextSibling : Entity::Invalid();
        }
        for (Entity k : kids) {
            DestroySubtree(world, k);
        }
    }
    Detach(world, e);
    world.DestroyEntity(e);
}

bool MoveRootRelative(World& world, Entity root, int delta, bool dryRun)
{
    if (delta == 0) { return false; }
    const HC* rh = world.GetComponent<HC>(root);
    // 有 HC 且 parent 有效 = 非根，不参与根序 reorder。
    if (rh != nullptr && rh->parent.IsValid()) { return false; }
    // root 无 HC（典型：编辑器新建的 root 实体只挂 Name/Transform，未挂
    // HierarchyComponent）也按根处理——下面收集阶段"无 HC 视为根"纳入，规整
    // 阶段统一给**所有**参与根补 HC + 写 sortIndex（见末段）。这样新建节点能
    // 参与 Move Up/Down（BUG-2026-06-01-new-root-entity-move-noop），且多个无
    // HC 新建根混在一起 reorder 也稳定（不会因只规整了部分根而排序错乱）。

    // 收集所有根，按当前 (sortIndex, entity id) 排序——id 作为 sortIndex 相同时
    // 的稳定 tie-break（与 EntityTreePanel 根枚举一致）。
    struct RootEntry { Entity e; int sortIndex; std::uint32_t id; };
    std::vector<RootEntry> roots;
    for (auto ent : world.Registry().view<entt::entity>()) {
        const Entity e = World::FromEntt(ent);
        const HC*    h = world.GetComponent<HC>(e);
        if (h == nullptr || !h->parent.IsValid()) {
            roots.push_back({e, (h != nullptr) ? h->sortIndex : 0,
                             static_cast<std::uint32_t>(entt::to_integral(ent))});
        }
    }
    if (roots.size() < 2) { return false; }
    std::sort(roots.begin(), roots.end(),
              [](const RootEntry& a, const RootEntry& b) {
                  if (a.sortIndex != b.sortIndex) { return a.sortIndex < b.sortIndex; }
                  return a.id < b.id;
              });

    std::size_t idx = roots.size();
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (roots[i].e == root) { idx = i; break; }
    }
    if (idx == roots.size()) { return false; }

    long target = static_cast<long>(idx) + delta;
    if (target < 0) { target = 0; }
    if (target > static_cast<long>(roots.size()) - 1) {
        target = static_cast<long>(roots.size()) - 1;
    }
    if (static_cast<std::size_t>(target) == idx) { return false; }  // 已在边界

    // dryRun：到这里说明"能移动"（非根 / 单根 / 边界都已在上面 return false 排
    // 除）。不改任何状态直接返回 true，让调用方把真正的移动交给命令栈 Execute
    // 执行一次——避免"判断时执行一次 + 命令栈再执行一次"导致移两位（跳顶/底）。
    if (dryRun) { return true; }

    // 在排序列表里把 root 从 idx 挪到 target，再把所有根 sortIndex 规整为
    // 0..n-1——规整确保反复 reorder 不让 sortIndex 漂移，且相邻 Move 可逆。
    const RootEntry moved = roots[idx];
    roots.erase(roots.begin() + static_cast<long>(idx));
    roots.insert(roots.begin() + target, moved);
    for (std::size_t i = 0; i < roots.size(); ++i) {
        HC* h = world.GetComponent<HC>(roots[i].e);
        if (h == nullptr) {
            // 无 HC 的根（新建节点）一并补默认 HC（parent invalid 合法根），
            // 使其与其它根一样持有 sortIndex——否则混合根（部分有 HC、部分新
            // 建无 HC）只规整了有 HC 的那些，无 HC 根 sortIndex 恒为 0，被 Move
            // 的根排序错乱（看似跳到顶/底或 Move 无效）。
            world.AddComponent<HC>(roots[i].e, HC{});
            h = world.GetComponent<HC>(roots[i].e);
        }
        if (h != nullptr) { h->sortIndex = static_cast<int>(i); }
    }
    return true;
}

}  // namespace EditorHierarchy
