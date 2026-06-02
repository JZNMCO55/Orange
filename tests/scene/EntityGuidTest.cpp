// EntityGuid 单测 —— Core::Guid 生成/字符串往返 + GuidComponent 的
// EnsureEntityGuids（惰性分配 + 幂等）/ ReassignEntityGuids（clone 换新）+
// 序列化往返保真 + clone 工作流（往返复制 GUID → Reassign 分离身份）。
// 纯逻辑、headless 可测（见 ADR-013）。

#include <orange/engine/core/Guid.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/PrefabInstanceComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Core::Guid;
using Orange::Engine::Scene::GuidComponent;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::PrefabInstanceComponent;

namespace Scene = Orange::Engine::Scene;

namespace
{

void TestGuidGenerateAndStringRoundTrip()
{
    const Guid a = Guid::Generate();
    const Guid b = Guid::Generate();
    assert(a.IsValid());
    assert(b.IsValid());
    assert(a != b);  // 两次随机 128-bit 实际上不可能相等

    // 全 0 = Invalid
    assert(Guid{}.IsValid() == false);

    // ToString → FromString 往返保真（32 hex）
    Guid parsed{};
    assert(Guid::FromString(a.ToString(), parsed));
    assert(parsed == a);
    assert(a.ToString().size() == 32);

    // 坏输入：非法字符 / 长度不对 → false，不改 out
    Guid dummy{1, 2};
    assert(!Guid::FromString("not-a-guid", dummy));
    assert(!Guid::FromString(std::string(31, '0'), dummy));
    assert(!Guid::FromString(std::string(33, '0'), dummy));
    assert(dummy == (Guid{1, 2}));  // 失败不写 out

    std::printf("  [ok] Guid generate + string round-trip\n");
}

void TestEnsureAssignsUniqueAndIdempotent()
{
    World w;
    const Entity e1 = w.CreateEntity();
    const Entity e2 = w.CreateEntity();
    const Entity e3 = w.CreateEntity();

    // 首次 Ensure：3 个全补上
    assert(Scene::EnsureEntityGuids(w) == 3);
    const auto* g1 = w.GetComponent<GuidComponent>(e1);
    const auto* g2 = w.GetComponent<GuidComponent>(e2);
    const auto* g3 = w.GetComponent<GuidComponent>(e3);
    assert(g1 != nullptr && g2 != nullptr && g3 != nullptr);
    assert(g1->guid.IsValid() && g2->guid.IsValid() && g3->guid.IsValid());
    // 互不相同
    assert(g1->guid != g2->guid);
    assert(g2->guid != g3->guid);
    assert(g1->guid != g3->guid);

    // 幂等：再 Ensure 返回 0，已有 GUID 不变
    const Guid saved = g1->guid;
    assert(Scene::EnsureEntityGuids(w) == 0);
    assert(w.GetComponent<GuidComponent>(e1)->guid == saved);

    // 新增实体只补新的那一个
    const Entity e4 = w.CreateEntity();
    assert(Scene::EnsureEntityGuids(w) == 1);
    assert(w.GetComponent<GuidComponent>(e4) != nullptr);
    assert(w.GetComponent<GuidComponent>(e1)->guid == saved);  // 老的仍不变

    std::printf("  [ok] EnsureEntityGuids assigns unique + idempotent\n");
}

void TestReassignChangesGuid()
{
    World w;
    const Entity e = w.CreateEntity();
    Scene::EnsureEntityGuids(w);
    const Guid before = w.GetComponent<GuidComponent>(e)->guid;

    const std::vector<Entity> targets{e};
    assert(Scene::ReassignEntityGuids(w, targets) == 1);
    const Guid after = w.GetComponent<GuidComponent>(e)->guid;
    assert(after.IsValid());
    assert(after != before);

    // 无效实体被跳过、不计数
    const std::vector<Entity> invalid{Entity::Invalid()};
    assert(Scene::ReassignEntityGuids(w, invalid) == 0);

    std::printf("  [ok] ReassignEntityGuids forces a fresh GUID\n");
}

void TestSerializationRoundTripAndCloneReassign()
{
    World w;
    const Entity root = w.CreateEntity();
    w.AddComponent<NameComponent>(root, NameComponent{"Root"});
    Scene::EnsureEntityGuids(w);
    const Guid original = w.GetComponent<GuidComponent>(root)->guid;

    // 子树序列化 → 内存 JSON → 反序列化追加回**同一个** world。
    const std::vector<Entity> roots{root};
    auto blob = Scene::SaveSubtreeToString(w, roots);
    assert(!blob.IsErr());

    std::vector<Entity> created;
    auto loadRc = Scene::LoadFromString(blob.Value(), w, {}, &created);
    assert(!loadRc.IsErr());
    assert(created.size() == 1);
    const Entity clone = created[0];

    // 往返保真：序列化层不改身份 —— clone 读回的 GUID 与原一致。
    // 这正是"为什么 clone 后必须 Reassign"的根据（否则同一 world 里两个
    // 实体共享 GUID）。
    const auto* cloneGuid = w.GetComponent<GuidComponent>(clone);
    assert(cloneGuid != nullptr);
    assert(cloneGuid->guid == original);

    // clone 工作流：消费方对新建实体 Reassign 后，身份与源分离。
    Scene::ReassignEntityGuids(w, created);
    assert(w.GetComponent<GuidComponent>(clone)->guid != original);
    assert(w.GetComponent<GuidComponent>(root)->guid == original);  // 源不动

    std::printf("  [ok] serialization round-trip保真 + clone reassign 分离身份\n");
}

// Duplicate / Paste 的核心 clone 路径：SaveSubtreeToString → LoadFromString →
// SeparateClonedIdentities。验证克隆体的 GuidComponent 与源分离（修
// GAP-2026-05-30-prefab 里 Reassign→Duplicate 的碰撞 bug）。不经编辑器 EditorHost，
// 直接测可复用的 Scene::SeparateClonedIdentities。
void TestSeparateClonedIdentitiesReassignsGuid()
{
    World w;
    const Entity root = w.CreateEntity();
    w.AddComponent<NameComponent>(root, NameComponent{"WithGuid"});
    Scene::EnsureEntityGuids(w);
    const Guid original = w.GetComponent<GuidComponent>(root)->guid;

    const std::vector<Entity> roots{root};
    auto blob = Scene::SaveSubtreeToString(w, roots);
    assert(blob.IsOk());

    std::vector<Entity> created;
    auto loadRc = Scene::LoadFromString(blob.Value(), w, {}, &created);
    assert(loadRc.IsOk());
    assert(created.size() == 1);
    const Entity clone = created[0];
    // clone 读回时与源共享 GUID（往返保真），SeparateClonedIdentities 前先确认。
    assert(w.GetComponent<GuidComponent>(clone)->guid == original);

    // 一站式身份分离：clone 的 GUID 换新，源不动。
    Scene::SeparateClonedIdentities(w, created);
    assert(w.GetComponent<GuidComponent>(clone)->guid != original);
    assert(w.GetComponent<GuidComponent>(clone)->guid.IsValid());
    assert(w.GetComponent<GuidComponent>(root)->guid == original);  // 源不动

    std::printf("  [ok] SeparateClonedIdentities 给克隆体换新 GUID（源不动）\n");
}

// Duplicate 一个 prefab 实例：clone 的 PrefabInstanceComponent.instanceId 必须换新
// （否则被误认为与源属于同一次实例化）。再验证"分组保留"——同一旧 instanceId 的
// 多个克隆体共享同一个新 instanceId；不同旧 instanceId 映射到不同新 instanceId。
void TestSeparateClonedIdentitiesRemapsPrefabInstanceId()
{
    World w;

    // 构造一棵 2 实体子树，模拟一次 prefab 实例化的产物：root + child 同 instanceId，
    // root.isInstanceRoot=true。子树拓扑用 HierarchyComponent 串好（保证 clone 后
    // 这两个实体作为同一棵子树被一并克隆）。
    const Entity root  = w.CreateEntity();
    const Entity child = w.CreateEntity();
    const Guid   srcInstanceId = Guid::Generate();
    {
        HierarchyComponent rh;
        rh.firstChild = child;
        w.AddComponent<HierarchyComponent>(root, rh);
        HierarchyComponent ch;
        ch.parent = root;
        w.AddComponent<HierarchyComponent>(child, ch);
    }
    {
        PrefabInstanceComponent rl;
        rl.sourcePrefabPath = "assets/Prefabs/foo.prefab.json";
        rl.instanceId       = srcInstanceId;
        rl.isInstanceRoot   = true;
        w.AddComponent<PrefabInstanceComponent>(root, rl);
        PrefabInstanceComponent cl;
        cl.sourcePrefabPath = "assets/Prefabs/foo.prefab.json";
        cl.instanceId       = srcInstanceId;
        cl.isInstanceRoot   = false;
        w.AddComponent<PrefabInstanceComponent>(child, cl);
    }

    const std::vector<Entity> roots{root};
    auto blob = Scene::SaveSubtreeToString(w, roots);
    assert(blob.IsOk());

    std::vector<Entity> created;
    auto loadRc = Scene::LoadFromString(blob.Value(), w, {}, &created);
    assert(loadRc.IsOk());
    assert(created.size() == 2);

    // clone 读回时与源共享 instanceId（往返保真）。
    for (const Entity ce : created)
    {
        const auto* link = w.GetComponent<PrefabInstanceComponent>(ce);
        assert(link != nullptr);
        assert(link->instanceId == srcInstanceId);
    }

    const std::size_t remapped = Scene::SeparateClonedIdentities(w, created);
    assert(remapped == 2);  // 两个克隆体都带 PrefabInstanceComponent

    // 克隆体的 instanceId 都换新、彼此一致（同源 instanceId → 同一新 id，分组保留），
    // 且与源 instanceId 不同。sourcePrefabPath / isInstanceRoot 不动。
    Guid cloneInstanceId{};
    bool first = true;
    int  rootCount = 0;
    for (const Entity ce : created)
    {
        const auto* link = w.GetComponent<PrefabInstanceComponent>(ce);
        assert(link != nullptr);
        assert(link->instanceId != srcInstanceId);
        assert(link->instanceId.IsValid());
        assert(link->sourcePrefabPath == "assets/Prefabs/foo.prefab.json");
        if (first) { cloneInstanceId = link->instanceId; first = false; }
        else       { assert(link->instanceId == cloneInstanceId); }  // 同一新 id
        if (link->isInstanceRoot) { ++rootCount; }
    }
    assert(rootCount == 1);  // 仍只有一个根标记

    // 源子树的 instanceId 不被改动。
    assert(w.GetComponent<PrefabInstanceComponent>(root)->instanceId == srcInstanceId);
    assert(w.GetComponent<PrefabInstanceComponent>(child)->instanceId == srcInstanceId);

    std::printf("  [ok] SeparateClonedIdentities 换新 prefab instanceId（分组保留 + 源不动）\n");
}

// 两个独立实例（不同 instanceId）混在一次 clone 里：重映射后仍是两个不同的新
// instanceId（不会被错误合并成一个）。锁住"不同旧 id → 不同新 id"的语义。
void TestSeparateClonedIdentitiesKeepsDistinctInstancesDistinct()
{
    World w;
    const Entity a = w.CreateEntity();
    const Entity b = w.CreateEntity();
    const Guid   idA = Guid::Generate();
    const Guid   idB = Guid::Generate();
    assert(idA != idB);
    {
        PrefabInstanceComponent la;
        la.sourcePrefabPath = "p.prefab.json";
        la.instanceId       = idA;
        la.isInstanceRoot   = true;
        w.AddComponent<PrefabInstanceComponent>(a, la);
        PrefabInstanceComponent lb;
        lb.sourcePrefabPath = "p.prefab.json";
        lb.instanceId       = idB;
        lb.isInstanceRoot   = true;
        w.AddComponent<PrefabInstanceComponent>(b, lb);
    }

    // 直接对 {a, b} 调（模拟一次 clone 出来的 created 同时含两个不同实例）。
    const std::vector<Entity> created{a, b};
    assert(Scene::SeparateClonedIdentities(w, created) == 2);

    const Guid newA = w.GetComponent<PrefabInstanceComponent>(a)->instanceId;
    const Guid newB = w.GetComponent<PrefabInstanceComponent>(b)->instanceId;
    assert(newA.IsValid() && newB.IsValid());
    assert(newA != idA && newB != idB);  // 各自换新
    assert(newA != newB);                // 不同旧 id → 不同新 id（没被合并）

    std::printf("  [ok] SeparateClonedIdentities 保持不同实例的 instanceId 互异\n");
}

// FindEntityByGuid（A2 先行 slice S2）：按稳定 guid 反查 entity——命中 / 未命中 /
// 非法 guid 判负 / Reassign 后旧失效新命中 / 无 GuidComponent 实体不被零 guid 误匹配。
void TestFindEntityByGuid()
{
    World        w;
    const Entity a = w.CreateEntity();
    const Entity b = w.CreateEntity();
    Scene::EnsureEntityGuids(w);  // a,b 全补 guid

    const Guid ga = w.GetComponent<GuidComponent>(a)->guid;
    const Guid gb = w.GetComponent<GuidComponent>(b)->guid;

    // 命中：按 guid 反查回对应 entity。
    assert(Scene::FindEntityByGuid(w, ga) == a);
    assert(Scene::FindEntityByGuid(w, gb) == b);

    // 未命中：无关 guid → Invalid。
    assert(!Scene::FindEntityByGuid(w, Guid::Generate()).IsValid());

    // 非法 guid（全 0）→ Invalid（即使存在未分配实体也不误匹配）。
    assert(!Scene::FindEntityByGuid(w, Guid{}).IsValid());

    // Reassign 后：旧 guid 失效、新 guid 命中同一 entity。
    const std::vector<Entity> targets{a};
    Scene::ReassignEntityGuids(w, targets);
    assert(!Scene::FindEntityByGuid(w, ga).IsValid());
    const Guid gaNew = w.GetComponent<GuidComponent>(a)->guid;
    assert(gaNew != ga && Scene::FindEntityByGuid(w, gaNew) == a);

    // 无 GuidComponent 的新实体不被零 guid 误匹配。
    w.CreateEntity();
    assert(!Scene::FindEntityByGuid(w, Guid{}).IsValid());

    std::printf("  [ok] FindEntityByGuid hit / miss / zero-guid / reassign\n");
}

}  // namespace

int main()
{
    std::printf("[scene_entity_guid_test]\n");
    TestGuidGenerateAndStringRoundTrip();
    TestEnsureAssignsUniqueAndIdempotent();
    TestReassignChangesGuid();
    TestFindEntityByGuid();
    TestSerializationRoundTripAndCloneReassign();
    TestSeparateClonedIdentitiesReassignsGuid();
    TestSeparateClonedIdentitiesRemapsPrefabInstanceId();
    TestSeparateClonedIdentitiesKeepsDistinctInstancesDistinct();
    std::printf("[scene_entity_guid_test] all passed\n");
    return 0;
}
