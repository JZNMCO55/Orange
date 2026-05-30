// EntityGuid 单测 —— Core::Guid 生成/字符串往返 + GuidComponent 的
// EnsureEntityGuids（惰性分配 + 幂等）/ ReassignEntityGuids（clone 换新）+
// 序列化往返保真 + clone 工作流（往返复制 GUID → Reassign 分离身份）。
// 纯逻辑、headless 可测（见 ADR-013）。

#include <orange/engine/core/Guid.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/NameComponent.h>
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
using Orange::Engine::Scene::NameComponent;

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

}  // namespace

int main()
{
    std::printf("[scene_entity_guid_test]\n");
    TestGuidGenerateAndStringRoundTrip();
    TestEnsureAssignsUniqueAndIdempotent();
    TestReassignChangesGuid();
    TestSerializationRoundTripAndCloneReassign();
    std::printf("[scene_entity_guid_test] all passed\n");
    return 0;
}
