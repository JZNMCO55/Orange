// Play-in-Editor world-clone 的 EntityGuid 稳定性（A2.3，见
// docs/a2-entity-guid-stable-identity-design.md §3 / ADR-018）。
//
// 锁住的不变量：编辑器 EnterPlay 进 Play 时把 editing world 经
// Scene::SaveToString(World&)（非 const 入口，SaveOptions.ensureGuids 默认 true）
// 序列化成内存快照串，Stop 时经 Scene::LoadFromString(blob, newWorld) 还原成一个
// **全新 World 实例** 替换 editing world。本测试用内存快照串精确复刻 EditorRenderLayer
// EnterPlay / Stop 的那条 SaveToString→LoadFromString 路径（M9 PIE 内存快照），断言：
//
//   * 快照序列化前 editing world 实体可能尚无 GuidComponent（用户从零搭的实体），
//     SaveToString(World&) 非 const 入口会 EnsureEntityGuids 普遍补全；
//   * Stop 还原出的 runtime/edit world 里，用 editing world 各实体的 guid 经
//     Scene::FindEntityByGuid 都能命中——即同一逻辑实体跨 Play snapshot/restore
//     身份不漂移（不像 clone 路径那样走 ReassignEntityGuids 换新身份）；
//   * 命中实体的 Transform / Name / 父子层级与原实体一致；
//   * 反复进出 Play（含 Play 期修改 ECS 后被快照还原丢弃）guid 仍稳定。
//
// 纯序列化 + World，无 Vulkan / GUI；走真实内存 SaveToString/LoadFromString 路径
// （与 EnterPlay 字面一致），不引入编辑器 TU 依赖。EnterPlay 的物理 / 音频 / VFX 接入
// 是运行时 backend，与 guid 身份正交，故不在本测试范围。

#include <orange/engine/core/Guid.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Core::Guid;
using Orange::Engine::Scene::GuidComponent;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::TransformComponent;

namespace Scene = Orange::Engine::Scene;

namespace
{

    // 模拟 EnterPlay 的"World 内存快照"：非 const SaveToString(World&) 入口 + 默认
    // SaveOptions（ensureGuids=true），会就地给 editing world 普遍补全 guid，返回
    // 快照 JSON 串（编辑器存进 EditorSceneContext.playSnapshotBlob）。
    std::string TakePlaySnapshot(World& editingWorld)
    {
        Orange::Engine::Scene::SaveOptions saveOpts; // ensureGuids 默认 true
        auto                               rc = Scene::SaveToString(editingWorld, saveOpts);
        assert(rc.IsOk());
        return std::move(rc.Value());
    }

    // 模拟 Stop 的"从快照还原成全新 World"：LoadFromString 到一个新建 World 实例
    // （编辑器用它替换 mHost.scene.pWorld）。返回还原后的 World。
    std::unique_ptr<World> RestoreFromSnapshot(const std::string& blob)
    {
        auto       pNew = std::make_unique<World>();
        const auto rc   = Scene::LoadFromString(blob, *pNew);
        assert(rc.IsOk());
        return pNew;
    }

    // 进 Play 快照 → 还原后，editing world 每个带 guid 的实体都能在还原 world 里被
    // FindEntityByGuid 命中（同一逻辑实体），且 Transform / Name / 父子链一致。
    void TestSnapshotRoundTripKeepsGuidStable()
    {
        // editing world：父 + 两个子，带 Transform / Name / 层级。刻意**不**预先
        // EnsureEntityGuids——模拟用户从零搭的实体可能整场无 guid，靠 Save 路径补全。
        World        editing;
        const Entity parent = editing.CreateEntity();
        const Entity childA = editing.CreateEntity();
        const Entity childB = editing.CreateEntity();

        editing.AddComponent<NameComponent>(parent, NameComponent{"Parent"});
        editing.AddComponent<NameComponent>(childA, NameComponent{"ChildA"});
        editing.AddComponent<NameComponent>(childB, NameComponent{"ChildB"});

        editing.AddComponent<TransformComponent>(
            parent, TransformComponent{glm::vec3{1.0f, 2.0f, 3.0f},
                                       glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
                                       glm::vec3{1.0f, 1.0f, 1.0f}});
        editing.AddComponent<TransformComponent>(
            childA, TransformComponent{glm::vec3{10.0f, 0.0f, 0.0f},
                                       glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
                                       glm::vec3{2.0f, 2.0f, 2.0f}});
        editing.AddComponent<TransformComponent>(
            childB, TransformComponent{glm::vec3{0.0f, -5.0f, 0.0f},
                                       glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
                                       glm::vec3{1.0f, 1.0f, 1.0f}});

        // parent → (childA, childB) 兄弟链。
        {
            HierarchyComponent ph;
            ph.firstChild = childA;
            editing.AddComponent<HierarchyComponent>(parent, ph);
            HierarchyComponent ah;
            ah.parent      = parent;
            ah.nextSibling = childB;
            editing.AddComponent<HierarchyComponent>(childA, ah);
            HierarchyComponent bh;
            bh.parent      = parent;
            bh.prevSibling = childA;
            editing.AddComponent<HierarchyComponent>(childB, bh);
        }

        // 进 Play：内存快照（非 const SaveToString 入口补 guid）。
        const std::string snap = TakePlaySnapshot(editing);

        // editing world 现在每个实体都被补了 guid（Save 路径 EnsureEntityGuids 的效果）。
        const auto* gp = editing.GetComponent<GuidComponent>(parent);
        const auto* ga = editing.GetComponent<GuidComponent>(childA);
        const auto* gb = editing.GetComponent<GuidComponent>(childB);
        assert(gp != nullptr && ga != nullptr && gb != nullptr);
        assert(gp->guid.IsValid() && ga->guid.IsValid() && gb->guid.IsValid());
        const Guid guidParent = gp->guid;
        const Guid guidChildA = ga->guid;
        const Guid guidChildB = gb->guid;

        // Stop：从快照还原成全新 World（编辑器用它替换 editing world）。
        std::unique_ptr<World> restored = RestoreFromSnapshot(snap);

        // 核心不变量：原 editing world 的 guid 在还原 world 里都能命中同一逻辑实体。
        const Entity rParent = Scene::FindEntityByGuid(*restored, guidParent);
        const Entity rChildA = Scene::FindEntityByGuid(*restored, guidChildA);
        const Entity rChildB = Scene::FindEntityByGuid(*restored, guidChildB);
        assert(rParent.IsValid());
        assert(rChildA.IsValid());
        assert(rChildB.IsValid());
        // 三个 guid 命中三个互异实体（身份没被坍缩 / 误共享）。
        assert(rParent != rChildA && rParent != rChildB && rChildA != rChildB);

        // 命中实体的 Name 与原一致（验证 guid 真锚到对应逻辑实体，而非随便一个）。
        assert(restored->GetComponent<NameComponent>(rParent)->name == "Parent");
        assert(restored->GetComponent<NameComponent>(rChildA)->name == "ChildA");
        assert(restored->GetComponent<NameComponent>(rChildB)->name == "ChildB");

        // Transform 一致（position / scale 逐字段比对）。
        {
            const auto* tp = restored->GetComponent<TransformComponent>(rParent);
            const auto* ta = restored->GetComponent<TransformComponent>(rChildA);
            assert(tp != nullptr && ta != nullptr);
            assert(tp->position == glm::vec3(1.0f, 2.0f, 3.0f));
            assert(ta->position == glm::vec3(10.0f, 0.0f, 0.0f));
            assert(ta->scale == glm::vec3(2.0f, 2.0f, 2.0f));
        }

        // 层级一致：还原 world 里 childA / childB 的 parent 链接（经 guid 主键解析）
        // 指向同一个 rParent；childA 的 nextSibling 指向 rChildB。
        {
            const auto* ha = restored->GetComponent<HierarchyComponent>(rChildA);
            const auto* hb = restored->GetComponent<HierarchyComponent>(rChildB);
            const auto* hp = restored->GetComponent<HierarchyComponent>(rParent);
            assert(ha != nullptr && hb != nullptr && hp != nullptr);
            assert(ha->parent == rParent);
            assert(hb->parent == rParent);
            assert(hp->firstChild == rChildA);
            assert(ha->nextSibling == rChildB);
            assert(hb->prevSibling == rChildA);
        }

        std::printf("  [ok] Play snapshot round-trip 保 guid 稳定 + Transform/Name/层级一致\n");
    }

    // 反复进出 Play：guid 不随每次 Play 漂移。第二次进 Play 前在还原 world 上改
    // 一个 Transform（模拟用户继续编辑），再 Play→Stop——guid 仍是同一套，证明
    // editing↔runtime 之间身份用 guid 锚定而非每次重分配。
    void TestRepeatedPlayStopDoesNotDriftGuid()
    {
        World        editing;
        const Entity e = editing.CreateEntity();
        editing.AddComponent<NameComponent>(e, NameComponent{"Solo"});
        editing.AddComponent<TransformComponent>(
            e, TransformComponent{glm::vec3{0.0f, 0.0f, 0.0f},
                                  glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
                                  glm::vec3{1.0f, 1.0f, 1.0f}});

        // 第 1 次进 Play：补 guid + 内存快照。
        std::string snap = TakePlaySnapshot(editing);
        const Guid  guid0 = editing.GetComponent<GuidComponent>(e)->guid;
        assert(guid0.IsValid());

        // 第 1 次 Stop：还原。命中同一逻辑实体。
        std::unique_ptr<World> world1 = RestoreFromSnapshot(snap);
        const Entity           e1     = Scene::FindEntityByGuid(*world1, guid0);
        assert(e1.IsValid());
        assert(world1->GetComponent<NameComponent>(e1)->name == "Solo");

        // 模拟 Play 期 / 还原后用户继续编辑：改 Transform（Play 期 ECS 改动语义上
        // 会被下一次快照覆盖，但实体身份 guid 不该因此改变）。
        world1->GetComponent<TransformComponent>(e1)->position =
            glm::vec3{42.0f, 0.0f, 0.0f};

        // 第 2 次进 Play：在 world1 上再快照。guid 幂等——已有 guid 不被 EnsureEntityGuids
        // 换新，所以快照里仍是 guid0。
        snap = TakePlaySnapshot(*world1);
        const Guid guid1 = world1->GetComponent<GuidComponent>(e1)->guid;
        assert(guid1 == guid0); // 跨多次 Play 不漂移

        // 第 2 次 Stop：还原。仍能用最初的 guid0 命中。
        std::unique_ptr<World> world2 = RestoreFromSnapshot(snap);
        const Entity           e2     = Scene::FindEntityByGuid(*world2, guid0);
        assert(e2.IsValid());
        assert(world2->GetComponent<NameComponent>(e2)->name == "Solo");
        // 第 2 次快照前改过的 Transform 被持久化进第 2 次快照，故还原可见。
        assert(world2->GetComponent<TransformComponent>(e2)->position == glm::vec3(42.0f, 0.0f, 0.0f));

        std::printf("  [ok] 反复进出 Play guid 不漂移（同一逻辑实体稳定可寻）\n");
    }

} // namespace

int main()
{
    std::printf("[scene_play_snapshot_guid_test]\n");
    TestSnapshotRoundTripKeepsGuidStable();
    TestRepeatedPlayStopDoesNotDriftGuid();
    std::printf("[scene_play_snapshot_guid_test] all passed\n");
    return 0;
}
