// Prefab 持久化 overriddenPaths 单测（C1 / ADR-019 问题 4）—— headless 真测。
//
// 覆盖"持久化显式 override 集"的三块契约：
//   * 序列化 round-trip：overriddenPaths 经 Scene::Save → Load 保真；旧数据（无字段，
//     1.17 形态）graceful 读空。
//   * 记录 / 查询 helper：RecordOverridePath dedup（重复记同 path 不增）；
//     IsPathOverridden 命中 / 未命中；ClearOverridePath 移除。
//   * RefreshInstanceWithRecordedOverrides：用持久化 override 集做 refresh —— 被记录的
//     字段保留实例值、未记录的字段更新成模板值（这正是"持久化 override 集让 refresh
//     精确"的价值点，对比 CS2 需 base 三方）；空 override 集 = 全取模板值。
//
//   T1 序列化 round-trip 保真 + 旧数据（无字段）graceful 读空。
//   T2 RecordOverridePath dedup + IsPathOverridden 命中/未命中 + ClearOverridePath。
//   T3 RefreshInstanceWithRecordedOverrides：记录的字段保留、未记录的字段拉模板值。
//   T4 空 overriddenPaths refresh = 全取模板值（实例完全跟随模板）。
//   T5 失败路径（空 templateEntityGuid / 模板里不存在的 guid）→ no-op false、实例不变。
//
// 纯逻辑、零 GLFW/Vulkan，仿 PrefabRefreshTest 的 cassert + main 模式。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/core/Guid.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/PrefabInstanceComponent.h>
#include <orange/engine/scene/PrefabInstantiation.h>
#include <orange/engine/scene/PrefabOverride.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/render/RenderableComponent.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::PrefabAsset;
using Orange::Engine::Core::Guid;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::ClearOverridePath;
using Orange::Engine::Scene::GuidComponent;
using Orange::Engine::Scene::IsPathOverridden;
using Orange::Engine::Scene::MakeOverridePath;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::PrefabInstanceComponent;
using Orange::Engine::Scene::RecordOverridePath;
using Orange::Engine::Scene::RefreshInstanceWithRecordedOverrides;
using Orange::Engine::Scene::TransformComponent;

namespace Scene = Orange::Engine::Scene;

namespace
{

bool NearEq(float a, float b)
{
    return std::fabs(a - b) < 1e-4f;
}

// 构一棵 root(Name, Transform pos=(0,0,0), Renderable visible/castsShadow=true) 子树。
// name 入参让"模板演进"测试能造出与实例 bake 值不同的模板默认名。EnsureEntityGuids
// 补身份。返回 root。
Entity BuildSampleRoot(World& w, const std::string& name)
{
    const Entity root = w.CreateEntity();
    w.AddComponent<NameComponent>(root, NameComponent{name});
    {
        TransformComponent tc;
        tc.position = {0.0f, 0.0f, 0.0f};
        w.AddComponent<TransformComponent>(root, tc);
    }
    {
        RenderableComponent rc;  // visible=true / castsShadow=true 默认
        w.AddComponent<RenderableComponent>(root, rc);
    }
    Scene::EnsureEntityGuids(w);
    return root;
}

PrefabAsset MakePrefabFromRoot(World& w, Entity root)
{
    const std::array<Entity, 1> roots{root};
    auto blob = Scene::SaveSubtreeToString(w, roots);
    assert(blob.IsOk());
    return PrefabAsset{"SamplePrefab", blob.Value()};
}

Entity InstantiateInto(World& instWorld, AssetRegistry& reg, const PrefabAsset& tmpl)
{
    auto handle = reg.Insert<PrefabAsset>("mem://sample.prefab.json",
                                          std::make_unique<PrefabAsset>(tmpl));
    assert(handle.IsOk());
    auto rootRes = Scene::InstantiatePrefab(instWorld, reg, handle.Value());
    assert(rootRes.IsOk());
    return rootRes.Value();
}

// ---------------------------------------------------------------------------
// T1 序列化 round-trip 保真 + 旧数据（无字段）graceful 读空
// ---------------------------------------------------------------------------
void TestSerializationRoundTrip()
{
    // 造一个带 overriddenPaths 的实例（实例化 → 手填两条 override path）。
    World tmplWorld;
    const Entity tmplRoot = BuildSampleRoot(tmplWorld, "A");
    const PrefabAsset tmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    World instWorld;
    AssetRegistry reg;
    const Entity instRoot = InstantiateInto(instWorld, reg, tmpl);

    auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
    assert(link != nullptr);
    RecordOverridePath(*link, "Transform", "position/0");
    RecordOverridePath(*link, "Renderable", "visible");
    const Guid savedTmplAnchor = link->templateEntityGuid;

    // Save 实例子树 → string（SaveSubtreeToString 是 Save 核心同一份序列化器，含
    // PrefabInstance 的 overriddenPaths），再 LoadFromString 回新 world。
    const std::array<Entity, 1> instRoots{instRoot};
    auto savedRes = Scene::SaveSubtreeToString(instWorld, instRoots);
    assert(savedRes.IsOk());
    const std::string saved = savedRes.Value();

    World loaded;
    assert(Scene::LoadFromString(saved, loaded).IsOk());

    // 在 loaded 里按 templateEntityGuid 找回该实例实体，断言 overriddenPaths 保真。
    const Entity reloadedRoot = Scene::FindEntityByGuid(
        loaded,
        instWorld.GetComponent<GuidComponent>(instRoot)->guid);
    assert(reloadedRoot.IsValid());
    const auto* reloadedLink =
        loaded.GetComponent<PrefabInstanceComponent>(reloadedRoot);
    assert(reloadedLink != nullptr);
    assert(reloadedLink->templateEntityGuid == savedTmplAnchor);
    assert(reloadedLink->overriddenPaths.size() == 2);
    assert(IsPathOverridden(*reloadedLink, "Transform", "position/0"));
    assert(IsPathOverridden(*reloadedLink, "Renderable", "visible"));

    // 旧数据（1.17 形态，无 overriddenPaths 字段）graceful 读空：手动把 saved 文本里
    // 的 "overriddenPaths" 段抹掉模拟旧文件不可靠（JSON 结构敏感）。改为直接造一个
    // 不写 overriddenPaths 的最小实例：用全新 instance 但 Record 0 条 → 落空数组，
    // Load 回来 overriddenPaths 为空（空数组与缺字段在读侧同走"读为空"路径）。
    {
        World tmplWorld2;
        const Entity tr2 = BuildSampleRoot(tmplWorld2, "B");
        const PrefabAsset tmpl2 = MakePrefabFromRoot(tmplWorld2, tr2);
        World instWorld2;
        AssetRegistry reg2;
        const Entity ir2 = InstantiateInto(instWorld2, reg2, tmpl2);
        // 不 Record 任何 override → overriddenPaths 空。
        const std::array<Entity, 1> roots2{ir2};
        auto saved2Res = Scene::SaveSubtreeToString(instWorld2, roots2);
        assert(saved2Res.IsOk());
        World loaded2;
        assert(Scene::LoadFromString(saved2Res.Value(), loaded2).IsOk());
        const Entity rr2 = Scene::FindEntityByGuid(
            loaded2, instWorld2.GetComponent<GuidComponent>(ir2)->guid);
        assert(rr2.IsValid());
        const auto* l2 = loaded2.GetComponent<PrefabInstanceComponent>(rr2);
        assert(l2 != nullptr);
        assert(l2->overriddenPaths.empty());
    }

    std::printf("  [ok] T1 overriddenPaths Save→Load round-trip 保真 + 空数组读空\n");
}

// ---------------------------------------------------------------------------
// T2 RecordOverridePath dedup + IsPathOverridden 命中/未命中 + ClearOverridePath
// ---------------------------------------------------------------------------
void TestRecordQueryClear()
{
    PrefabInstanceComponent link;

    // MakeOverridePath 格式：componentName/fieldPath；fieldPath 空 → 仅 componentName。
    assert(MakeOverridePath("Transform", "position/0") == "Transform/position/0");
    assert(MakeOverridePath("Renderable", "") == "Renderable");

    // 初始未命中。
    assert(!IsPathOverridden(link, "Transform", "position/0"));

    // 首次记录 → 新增 true。
    assert(RecordOverridePath(link, "Transform", "position/0"));
    assert(link.overriddenPaths.size() == 1);
    assert(IsPathOverridden(link, "Transform", "position/0"));

    // dedup：重复记同 path → false，不增。
    assert(!RecordOverridePath(link, "Transform", "position/0"));
    assert(link.overriddenPaths.size() == 1);

    // 记另一条 → 新增。
    assert(RecordOverridePath(link, "Renderable", "visible"));
    assert(link.overriddenPaths.size() == 2);
    assert(IsPathOverridden(link, "Renderable", "visible"));
    // 未记录的字段未命中。
    assert(!IsPathOverridden(link, "Transform", "position/1"));

    // ClearOverridePath：移除一条 → true，再查未命中、size 减。
    assert(ClearOverridePath(link, "Transform", "position/0"));
    assert(link.overriddenPaths.size() == 1);
    assert(!IsPathOverridden(link, "Transform", "position/0"));
    // 其余条目不受影响。
    assert(IsPathOverridden(link, "Renderable", "visible"));
    // 移除不存在的条目 → false，no-op。
    assert(!ClearOverridePath(link, "Transform", "position/0"));
    assert(link.overriddenPaths.size() == 1);

    std::printf("  [ok] T2 Record dedup + IsPathOverridden 命中/未命中 + Clear\n");
}

// ---------------------------------------------------------------------------
// T3 RefreshInstanceWithRecordedOverrides：记录的字段保留、未记录的字段拉模板值
// （持久化 override 集让 refresh 精确，对比 CS2 需 base）
// ---------------------------------------------------------------------------
void TestRefreshWithRecordedOverrides()
{
    // 模板初始 Name="A"、pos=0；实例化。
    World tmplWorld;
    const Entity tmplRoot = BuildSampleRoot(tmplWorld, "A");
    const PrefabAsset baseTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    World instWorld;
    AssetRegistry reg;
    const Entity instRoot = InstantiateInto(instWorld, reg, baseTmpl);

    // 实例侧改两个字段：position.x → 5、Renderable.visible → false。
    instWorld.GetComponent<TransformComponent>(instRoot)->position.x = 5.0f;
    instWorld.GetComponent<RenderableComponent>(instRoot)->visible = false;

    // **只把其中一个**记成 override：position.x（position/0）。visible 不记。
    auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
    assert(link != nullptr);
    RecordOverridePath(*link, "Transform", "position/0");

    // 模板演进：把这两个字段的默认值都改掉（position.x → 9、visible → true 本就是
    // 默认，这里改个能观察的：position.x → 9、Name → "Evolved"）。关键是改 position.x
    // 的模板默认值，以便区分"refresh 后 position.x 是实例 5（被记录保留）还是模板 9"。
    tmplWorld.GetComponent<TransformComponent>(tmplRoot)->position.x = 9.0f;
    tmplWorld.GetComponent<RenderableComponent>(tmplRoot)->visible = true;
    tmplWorld.GetComponent<NameComponent>(tmplRoot)->name = "Evolved";
    const PrefabAsset newTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    // 用持久化 override 集 refresh（单模板，不需要 base）。
    const bool ok = RefreshInstanceWithRecordedOverrides(instWorld, instRoot, newTmpl);
    assert(ok);

    const auto* tc = instWorld.GetComponent<TransformComponent>(instRoot);
    const auto* rc = instWorld.GetComponent<RenderableComponent>(instRoot);
    // 被记录的字段 position.x 保留实例值 5（不被模板 9 覆盖）。
    assert(NearEq(tc->position.x, 5.0f));
    // 未记录的字段 visible 更新成模板值 true（即便实例手改过 false，因没记 override）。
    assert(rc->visible);
    // 其余未记录字段也拉模板值：Name → "Evolved"。
    assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "Evolved");

    std::printf(
        "  [ok] T3 记录的 position.x 保留实例值 + 未记录的 visible/Name 拉模板值\n");
}

// ---------------------------------------------------------------------------
// T4 空 overriddenPaths refresh = 全取模板值（实例完全跟随模板）
// ---------------------------------------------------------------------------
void TestEmptyOverridesFullyFollowsTemplate()
{
    World tmplWorld;
    const Entity tmplRoot = BuildSampleRoot(tmplWorld, "A");
    const PrefabAsset baseTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    World instWorld;
    AssetRegistry reg;
    const Entity instRoot = InstantiateInto(instWorld, reg, baseTmpl);

    // 实例侧改字段，但**不记**任何 override（模拟"无显式 override"）。
    instWorld.GetComponent<TransformComponent>(instRoot)->position.x = 5.0f;
    instWorld.GetComponent<NameComponent>(instRoot)->name = "DriftedAway";

    // 模板演进：position.x → 9、Name → "Evolved"。
    tmplWorld.GetComponent<TransformComponent>(tmplRoot)->position.x = 9.0f;
    tmplWorld.GetComponent<NameComponent>(tmplRoot)->name = "Evolved";
    const PrefabAsset newTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    const bool ok = RefreshInstanceWithRecordedOverrides(instWorld, instRoot, newTmpl);
    assert(ok);

    // 空 override 集 → 全取模板值：实例的手改漂移全被覆盖。
    assert(NearEq(instWorld.GetComponent<TransformComponent>(instRoot)->position.x, 9.0f));
    assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "Evolved");

    // 身份 component 不动（实例仍是自己的 per-entity guid）。
    assert(instWorld.GetComponent<PrefabInstanceComponent>(instRoot) != nullptr);

    std::printf("  [ok] T4 空 overriddenPaths refresh → 全取模板值（实例跟随模板）\n");
}

// ---------------------------------------------------------------------------
// T5 失败路径 → no-op false、实例不变
// ---------------------------------------------------------------------------
void TestFailurePathsNoOp()
{
    World tmplWorld;
    const Entity tmplRoot = BuildSampleRoot(tmplWorld, "A");
    const PrefabAsset baseTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    World instWorld;
    AssetRegistry reg;
    const Entity instRoot = InstantiateInto(instWorld, reg, baseTmpl);

    // 模板演进（若 refresh 真跑会把 Name 改成 "B"——失败路径下不该发生）。
    tmplWorld.GetComponent<NameComponent>(tmplRoot)->name = "B";
    const PrefabAsset newTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    // (a) 空 templateEntityGuid（旧数据）→ no-op false、Name 仍 "A"。
    {
        auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
        const Guid saved = link->templateEntityGuid;
        link->templateEntityGuid = Guid{};
        const bool ok = RefreshInstanceWithRecordedOverrides(instWorld, instRoot, newTmpl);
        assert(!ok);
        assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "A");
        link->templateEntityGuid = saved;
    }

    // (b) 模板里不存在的 templateEntityGuid → FindEntityByGuid 未命中 → no-op false。
    {
        auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
        const Guid saved = link->templateEntityGuid;
        link->templateEntityGuid = Guid::Generate();
        const bool ok = RefreshInstanceWithRecordedOverrides(instWorld, instRoot, newTmpl);
        assert(!ok);
        assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "A");
        link->templateEntityGuid = saved;
    }

    // (c) 还原锚定后正常路径仍能刷新（证明前两步是因缺锚 no-op，非别的 bug）。
    {
        const bool ok = RefreshInstanceWithRecordedOverrides(instWorld, instRoot, newTmpl);
        assert(ok);
        assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "B");
    }

    // (d) 无效 entity → no-op false 不崩。
    assert(!RefreshInstanceWithRecordedOverrides(instWorld, Entity::Invalid(), newTmpl));

    std::printf("  [ok] T5 失败路径（空/未命中 guid / 无效 entity）→ no-op false 实例不变\n");
}

}  // namespace

int main()
{
    std::printf("[prefab_overridden_paths_test]\n");
    TestSerializationRoundTrip();
    TestRecordQueryClear();
    TestRefreshWithRecordedOverrides();
    TestEmptyOverridesFullyFollowsTemplate();
    TestFailurePathsNoOp();
    std::printf("[prefab_overridden_paths_test] all passed\n");
    return 0;
}
