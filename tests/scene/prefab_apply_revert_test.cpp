// Prefab apply / revert 引擎层原语单测（C1.3）—— headless 真测。
//
// C1.3 是 C1 prefab override epic 的收尾件，落两个原语：
//   * RevertEntityOverridePath / RevertInstanceOverridePath —— 把实例某个 field path
//     回退到模板值（CS2 merge 的"单 leaf 版"）+ 清掉该 path 的 override 记录，其余
//     override 不动。
//   * ApplyInstanceToTemplate —— 以实例当前态重建模板 blob，实例 guid 必须映回它锚定的
//     templateEntityGuid 再写出（否则其它实例的 A2.2 锚定全断）+ 清空本实例 overriddenPaths。
//
// 覆盖：
//   T1 Revert 单字段：改实例两字段 → 都记 override → revert 其中一条 → 断言该字段回模板值 +
//      该 path override 记录清除；另一条 override 记录 + 其字段值（实例值）保留。
//   T2 Apply：建模板 + 两个实例，改实例 A 某字段 → ApplyInstanceToTemplate(A) →
//      (a) 新 blob InstantiatePrefab 出新实例 C，断言 C 拿到 A 改后的值；
//      (b) 新模板实体 guid 恒等（== 原 templateEntityGuid，没被实例 guid 污染）；
//      (c) 实例 B RefreshInstanceWithRecordedOverrides(新模板) 后拿到新模板值，且 B 自己的
//          override 保留；
//      (d) apply 后实例 A 的 overriddenPaths 清空。
//   T3 round-trip：apply 后的 blob LoadFromString 正常（含 guid 保真）。
//   T4 失败路径：无效 entity / 非实例 entity（无 PrefabInstanceComponent）→ revert no-op false。
//
// 纯逻辑、零 GLFW/Vulkan，仿 PrefabOverriddenPathsTest 的 cassert + main 模式。

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
using Orange::Engine::Scene::ApplyInstanceToTemplate;
using Orange::Engine::Scene::GuidComponent;
using Orange::Engine::Scene::IsPathOverridden;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::PrefabInstanceComponent;
using Orange::Engine::Scene::RecordOverridePath;
using Orange::Engine::Scene::RefreshInstanceWithRecordedOverrides;
using Orange::Engine::Scene::RevertInstanceOverridePath;
using Orange::Engine::Scene::TransformComponent;

namespace Scene = Orange::Engine::Scene;

namespace
{

bool NearEq(float a, float b)
{
    return std::fabs(a - b) < 1e-4f;
}

// 构一棵 root(Name, Transform pos=(0,0,0), Renderable visible/castsShadow=true) 子树。
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

Entity InstantiateInto(World& instWorld, AssetRegistry& reg, const PrefabAsset& tmpl,
                       const char* path)
{
    auto handle = reg.Insert<PrefabAsset>(path, std::make_unique<PrefabAsset>(tmpl));
    assert(handle.IsOk());
    auto rootRes = Scene::InstantiatePrefab(instWorld, reg, handle.Value());
    assert(rootRes.IsOk());
    return rootRes.Value();
}

// ---------------------------------------------------------------------------
// T1 Revert 单字段：改两字段、都记 override → revert 一条 → 该字段回模板值 + 记录清除；
//    另一条 override 记录 + 其实例值保留。
// ---------------------------------------------------------------------------
void TestRevertSingleField()
{
    World tmplWorld;
    const Entity tmplRoot = BuildSampleRoot(tmplWorld, "A");
    const PrefabAsset tmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    World instWorld;
    AssetRegistry reg;
    const Entity instRoot = InstantiateInto(instWorld, reg, tmpl, "mem://t1.prefab.json");

    // 实例侧改两字段：position.x → 5、Renderable.visible → false。
    instWorld.GetComponent<TransformComponent>(instRoot)->position.x = 5.0f;
    instWorld.GetComponent<RenderableComponent>(instRoot)->visible = false;

    // 两条都记成 override。
    auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
    assert(link != nullptr);
    RecordOverridePath(*link, "Transform", "position/0");
    RecordOverridePath(*link, "Renderable", "visible");
    assert(link->overriddenPaths.size() == 2);

    // revert position.x（position/0）→ 回模板值 0、该 path 记录清除。
    const bool ok = RevertInstanceOverridePath(instWorld, instRoot, tmpl,
                                               "Transform", "position/0");
    assert(ok);

    const auto* tc = instWorld.GetComponent<TransformComponent>(instRoot);
    const auto* rc = instWorld.GetComponent<RenderableComponent>(instRoot);
    // 被 revert 的 position.x 回模板值 0。
    assert(NearEq(tc->position.x, 0.0f));
    // 该 path 的 override 记录清除。
    assert(!IsPathOverridden(*link, "Transform", "position/0"));
    // 另一条 override（visible）记录保留 + 其实例值（false）不被回退。
    assert(IsPathOverridden(*link, "Renderable", "visible"));
    assert(!rc->visible);
    assert(link->overriddenPaths.size() == 1);

    // revert 一条原本就没记的 path → 仍用模板值覆盖（幂等），返回 true。
    const bool ok2 = RevertInstanceOverridePath(instWorld, instRoot, tmpl,
                                                "Transform", "position/1");
    assert(ok2);
    assert(NearEq(instWorld.GetComponent<TransformComponent>(instRoot)->position.y, 0.0f));

    std::printf("  [ok] T1 revert position.x 回模板值 + 记录清除；visible override 保留\n");
}

// ---------------------------------------------------------------------------
// T2 Apply：改实例 A 字段 → ApplyInstanceToTemplate(A) → 新 blob 出新实例拿到改后值 +
//    模板 guid 恒等 + 实例 B refresh 拿新模板值且 B 自己 override 保留 + A overriddenPaths 清空。
// ---------------------------------------------------------------------------
void TestApply()
{
    World tmplWorld;
    const Entity tmplRoot = BuildSampleRoot(tmplWorld, "A");
    const PrefabAsset tmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    // 模板实体的稳定 guid（apply 后新模板里的实体 guid 必须恒等于它）。
    const Guid tmplEntityGuid = tmplWorld.GetComponent<GuidComponent>(tmplRoot)->guid;

    // 两个实例 A、B（各自独立 world，便于独立断言）。
    World worldA;
    AssetRegistry regA;
    const Entity instA = InstantiateInto(worldA, regA, tmpl, "mem://A.prefab.json");

    World worldB;
    AssetRegistry regB;
    const Entity instB = InstantiateInto(worldB, regB, tmpl, "mem://B.prefab.json");

    // 实例 A、B 都锚定同一模板实体 guid（A2.2）。
    assert(worldA.GetComponent<PrefabInstanceComponent>(instA)->templateEntityGuid
           == tmplEntityGuid);
    assert(worldB.GetComponent<PrefabInstanceComponent>(instB)->templateEntityGuid
           == tmplEntityGuid);

    // 实例 A 改 position.x → 7、Name → "AppliedName"，记 override。
    worldA.GetComponent<TransformComponent>(instA)->position.x = 7.0f;
    worldA.GetComponent<NameComponent>(instA)->name = "AppliedName";
    auto* linkA = worldA.GetComponent<PrefabInstanceComponent>(instA);
    RecordOverridePath(*linkA, "Transform", "position/0");
    RecordOverridePath(*linkA, "Name", "name");
    assert(!linkA->overriddenPaths.empty());

    // 实例 B 自己改一个不同字段并记 override：visible → false（apply A 后 refresh B
    // 应保留 B 的这条 override）。
    worldB.GetComponent<RenderableComponent>(instB)->visible = false;
    auto* linkB = worldB.GetComponent<PrefabInstanceComponent>(instB);
    RecordOverridePath(*linkB, "Renderable", "visible");

    // ApplyInstanceToTemplate(A) → 新模板 blob。
    auto newBlobRes = ApplyInstanceToTemplate(worldA, instA);
    assert(newBlobRes.IsOk());
    const PrefabAsset newTmpl{"SamplePrefab", newBlobRes.Value()};

    // (d) apply 后实例 A 的 overriddenPaths 清空（实例 == 模板）。
    assert(worldA.GetComponent<PrefabInstanceComponent>(instA)->overriddenPaths.empty());

    // (b) 新模板实体 guid 恒等（没被实例 A 的 guid 污染）。重载入新 blob → 它的实体
    //     应能按 tmplEntityGuid 找回。
    {
        World checkWorld;
        assert(Scene::LoadFromString(newTmpl.TemplateBlob(), checkWorld).IsOk());
        const Entity found = Scene::FindEntityByGuid(checkWorld, tmplEntityGuid);
        assert(found.IsValid());
        // 模板里该实体拿到 A 改后的值。
        assert(NearEq(checkWorld.GetComponent<TransformComponent>(found)->position.x, 7.0f));
        assert(checkWorld.GetComponent<NameComponent>(found)->name == "AppliedName");
        // 模板里不应残留 PrefabInstanceComponent（apply 已移除链接组件）。
        assert(checkWorld.GetComponent<PrefabInstanceComponent>(found) == nullptr);
    }

    // (a) 用新 blob InstantiatePrefab 出新实例 C，断言 C 拿到 A 改后的值。
    {
        World worldC;
        AssetRegistry regC;
        const Entity instC = InstantiateInto(worldC, regC, newTmpl, "mem://C.prefab.json");
        assert(NearEq(worldC.GetComponent<TransformComponent>(instC)->position.x, 7.0f));
        assert(worldC.GetComponent<NameComponent>(instC)->name == "AppliedName");
        // C 是新实例化，应有自己的 PrefabInstanceComponent + 锚回模板 guid（恒等）。
        const auto* linkC = worldC.GetComponent<PrefabInstanceComponent>(instC);
        assert(linkC != nullptr);
        assert(linkC->templateEntityGuid == tmplEntityGuid);
        // C 自己的 per-entity guid 不等于模板 guid（实例化 Reassign 换新身份）。
        assert(worldC.GetComponent<GuidComponent>(instC)->guid != tmplEntityGuid);
    }

    // (c) 实例 B refresh 到新模板：拿新模板值（position.x=7 / Name=AppliedName），但 B
    //     自己的 override（visible=false）保留。
    {
        const bool ok = RefreshInstanceWithRecordedOverrides(worldB, instB, newTmpl);
        assert(ok);
        assert(NearEq(worldB.GetComponent<TransformComponent>(instB)->position.x, 7.0f));
        assert(worldB.GetComponent<NameComponent>(instB)->name == "AppliedName");
        // B 自己的 override 保留：visible 仍 false（未被新模板的 true 覆盖）。
        assert(!worldB.GetComponent<RenderableComponent>(instB)->visible);
        // B 的 override 记录仍在。
        assert(IsPathOverridden(*worldB.GetComponent<PrefabInstanceComponent>(instB),
                                "Renderable", "visible"));
    }

    std::printf("  [ok] T2 apply(A)：新实例拿改后值 + 模板 guid 恒等 + B refresh 保留自己 override + A 清空\n");
}

// ---------------------------------------------------------------------------
// T3 round-trip：apply 后的 blob LoadFromString 正常（含 guid 保真）。
// ---------------------------------------------------------------------------
void TestApplyRoundTrip()
{
    World tmplWorld;
    const Entity tmplRoot = BuildSampleRoot(tmplWorld, "Root");
    const PrefabAsset tmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);
    const Guid tmplEntityGuid = tmplWorld.GetComponent<GuidComponent>(tmplRoot)->guid;

    World instWorld;
    AssetRegistry reg;
    const Entity instRoot = InstantiateInto(instWorld, reg, tmpl, "mem://t3.prefab.json");
    instWorld.GetComponent<NameComponent>(instRoot)->name = "Changed";

    auto blobRes = ApplyInstanceToTemplate(instWorld, instRoot);
    assert(blobRes.IsOk());

    // round-trip：blob 能正常 LoadFromString，且 guid 保真（仍是模板锚 guid）。
    World loaded;
    assert(Scene::LoadFromString(blobRes.Value(), loaded).IsOk());
    const Entity loadedEnt = Scene::FindEntityByGuid(loaded, tmplEntityGuid);
    assert(loadedEnt.IsValid());
    assert(loaded.GetComponent<NameComponent>(loadedEnt)->name == "Changed");

    std::printf("  [ok] T3 apply blob round-trip LoadFromString 正常 + guid 保真\n");
}

// ---------------------------------------------------------------------------
// T4 失败路径：无效 entity / 非实例 entity → revert no-op false；无效 entity → apply 失败。
// ---------------------------------------------------------------------------
void TestFailurePaths()
{
    World tmplWorld;
    const Entity tmplRoot = BuildSampleRoot(tmplWorld, "A");
    const PrefabAsset tmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

    World instWorld;
    AssetRegistry reg;
    const Entity instRoot = InstantiateInto(instWorld, reg, tmpl, "mem://t4.prefab.json");

    // (a) 无效 entity → revert no-op false。
    assert(!RevertInstanceOverridePath(instWorld, Entity::Invalid(), tmpl,
                                       "Transform", "position/0"));

    // (b) 非实例 entity（无 PrefabInstanceComponent）→ revert no-op false。
    World plain;
    const Entity plainEnt = BuildSampleRoot(plain, "Plain");
    assert(!RevertInstanceOverridePath(plain, plainEnt, tmpl, "Transform", "position/0"));

    // (c) 空 templateEntityGuid（旧数据）→ revert no-op false（实例不变）。
    {
        auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
        const Guid saved = link->templateEntityGuid;
        link->templateEntityGuid = Guid{};
        instWorld.GetComponent<TransformComponent>(instRoot)->position.x = 3.0f;
        const bool ok = RevertInstanceOverridePath(instWorld, instRoot, tmpl,
                                                   "Transform", "position/0");
        assert(!ok);
        // 锚为空 → no-op，实例值不被回退（仍 3）。
        assert(NearEq(instWorld.GetComponent<TransformComponent>(instRoot)->position.x, 3.0f));
        link->templateEntityGuid = saved;
    }

    // (d) 无效 entity → apply 失败。
    assert(ApplyInstanceToTemplate(instWorld, Entity::Invalid()).IsErr());

    std::printf("  [ok] T4 失败路径（无效/非实例/空锚 entity）→ revert/apply no-op false\n");
}

}  // namespace

int main()
{
    std::printf("[prefab_apply_revert_test]\n");
    TestRevertSingleField();
    TestApply();
    TestApplyRoundTrip();
    TestFailurePaths();
    std::printf("[prefab_apply_revert_test] all passed\n");
    return 0;
}
