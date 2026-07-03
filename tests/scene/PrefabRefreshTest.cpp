// Prefab refresh-from-template 单测（C1 CS2）—— headless 真测。
//
// 覆盖 c1-prefab-override-design.md §3 CS2 的核心契约：把实例实体的**未被 override
// 字段**从（演进后）模板重拉，**override 字段保留实例值**；身份/链接 component
// （Guid / PrefabInstance / Hierarchy）完全不动。
//
// CS2 是**三方 merge**：base（bake 时模板，判 override 的基准）+ new（演进后模板，
// 未 override 字段的新值源）+ mine（实例）。真 override = diff(mine, base)。详见
// PrefabOverride.h 的设计注释（二方 diff 分不清"用户手改"与"模板演进"）。
//
//   T1 override 保留 + 未 override 更新：改实例 position（override）+ 改模板 Name
//      默认值（模拟模板演进，实例 bake 的是旧名）→ refresh 后 position 仍是实例值、
//      Name 变成新模板值。
//   T2 全未改实例 + 未演进模板 refresh → 实例不变（幂等无害）。
//   T3 多字段 / 多 component：部分 override 保留、其余回新模板值，逐叶子精确。
//   T4 身份 component（Guid / PrefabInstance）refresh 后不动（仍是实例 guid + 链接）。
//   T5 失败路径（空 templateEntityGuid / 模板里不存在的 guid）→ no-op 返回 false、
//      实例不变。
//   T6 底层入口 RefreshEntityFromTemplate（三方两 world+base 三 entity）+ 无效 graceful。
//
// 纯逻辑、零 GLFW/Vulkan，仿 PrefabOverrideTest 的 cassert + main 模式。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/core/Guid.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
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
using Orange::Engine::Scene::GuidComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::PrefabInstanceComponent;
using Orange::Engine::Scene::RefreshEntityFromTemplate;
using Orange::Engine::Scene::RefreshInstanceFromTemplate;
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
            RenderableComponent rc; // visible=true / castsShadow=true 默认
            w.AddComponent<RenderableComponent>(root, rc);
        }
        Scene::EnsureEntityGuids(w);
        return root;
    }

    PrefabAsset MakePrefabFromRoot(World& w, Entity root)
    {
        const std::array<Entity, 1> roots{root};
        auto                        blob = Scene::SaveSubtreeToString(w, roots);
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
    // T1 override 保留 + 未 override 字段更新成模板值（核心契约）
    // ---------------------------------------------------------------------------
    void TestOverridePreservedUnchangedUpdated()
    {
        // 模板初始 Name="A"；实例化 → 实例 bake 了 "A"。baseTmpl 捕获 bake 时快照。
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld, "A");
        const PrefabAsset baseTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, baseTmpl);

        // 实例侧 override：position.x → 5（这条该在 refresh 后保留）。
        instWorld.GetComponent<TransformComponent>(instRoot)->position.x = 5.0f;

        // 模板演进：把模板里 Name 改成 "B" 并重新打包成新 blob（实例 bake 的仍是 "A"）。
        // per-entity guid 不变（改的是字段值，没重建实体），故 base/new 锚定同一模板实体。
        tmplWorld.GetComponent<NameComponent>(tmplRoot)->name = "B";
        const PrefabAsset newTmpl                             = MakePrefabFromRoot(tmplWorld, tmplRoot);

        // 三方 refresh：base=bake 时模板，new=演进后模板。
        const bool ok = RefreshInstanceFromTemplate(instWorld, instRoot, baseTmpl, newTmpl);
        assert(ok);

        // override 字段保留：position.x 仍是实例值 5。
        const auto* tc = instWorld.GetComponent<TransformComponent>(instRoot);
        assert(NearEq(tc->position.x, 5.0f));
        // 未 override 字段更新成模板值：position.y/z 仍 0（模板就是 0），Name 变 "B"。
        assert(NearEq(tc->position.y, 0.0f));
        assert(NearEq(tc->position.z, 0.0f));
        assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "B");

        std::printf("  [ok] T1 override(position.x=5) 保留 + 未 override(Name) 更新成 \"B\"\n");
    }

    // ---------------------------------------------------------------------------
    // T2 全未改实例 refresh → 实例 == 模板（幂等无害）
    // ---------------------------------------------------------------------------
    void TestUnchangedInstanceIdempotent()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld, "Root");
        const PrefabAsset tmpl     = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, tmpl);

        // 记录 refresh 前的身份（用于 T 之外确认身份不动）。
        const Guid instGuidBefore = instWorld.GetComponent<GuidComponent>(instRoot)->guid;

        const bool ok = RefreshInstanceFromTemplate(instWorld, instRoot, tmpl);
        assert(ok);

        // 无 override → 全量回模板值；模板与实例本就等价，故各字段不变。
        const auto* tc = instWorld.GetComponent<TransformComponent>(instRoot);
        assert(NearEq(tc->position.x, 0.0f));
        assert(NearEq(tc->position.y, 0.0f));
        assert(NearEq(tc->position.z, 0.0f));
        assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "Root");
        assert(instWorld.GetComponent<RenderableComponent>(instRoot)->visible);
        // 身份 component 不动。
        assert(instWorld.GetComponent<GuidComponent>(instRoot)->guid == instGuidBefore);

        std::printf("  [ok] T2 未改实例 refresh → 实例 == 模板（幂等）\n");
    }

    // ---------------------------------------------------------------------------
    // T3 多字段 / 多 component：部分 override 保留、其余回模板值
    // ---------------------------------------------------------------------------
    void TestMultiFieldMixed()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld, "A");
        const PrefabAsset baseTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, baseTmpl);

        // 实例侧 override：position.x → 7、Renderable.visible → false。
        instWorld.GetComponent<TransformComponent>(instRoot)->position.x = 7.0f;
        instWorld.GetComponent<RenderableComponent>(instRoot)->visible   = false;

        // 模板演进：Name "A" → "Evolved"、position.z 0 → 3、castsShadow true → false。
        tmplWorld.GetComponent<NameComponent>(tmplRoot)->name              = "Evolved";
        tmplWorld.GetComponent<TransformComponent>(tmplRoot)->position.z   = 3.0f;
        tmplWorld.GetComponent<RenderableComponent>(tmplRoot)->castsShadow = false;
        const PrefabAsset newTmpl                                          = MakePrefabFromRoot(tmplWorld, tmplRoot);

        const bool ok = RefreshInstanceFromTemplate(instWorld, instRoot, baseTmpl, newTmpl);
        assert(ok);

        const auto* tc = instWorld.GetComponent<TransformComponent>(instRoot);
        const auto* rc = instWorld.GetComponent<RenderableComponent>(instRoot);
        // override 保留：position.x=7、visible=false。
        assert(NearEq(tc->position.x, 7.0f));
        assert(!rc->visible);
        // 未 override 字段拉模板新值：position.z=3、Name="Evolved"、castsShadow=false。
        assert(NearEq(tc->position.z, 3.0f));
        assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "Evolved");
        assert(!rc->castsShadow);
        // position.y 两侧都没动 → 仍 0。
        assert(NearEq(tc->position.y, 0.0f));

        std::printf("  [ok] T3 多字段/多 component：override 保留 + 未 override 拉模板新值\n");
    }

    // ---------------------------------------------------------------------------
    // T4 身份 component refresh 后完全不动
    // ---------------------------------------------------------------------------
    void TestIdentityComponentsUntouched()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld, "A");
        const PrefabAsset baseTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, baseTmpl);

        // refresh 前快照身份 / 链接。
        const Guid  instGuid   = instWorld.GetComponent<GuidComponent>(instRoot)->guid;
        const auto* linkBefore = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
        assert(linkBefore != nullptr);
        const Guid        instanceId = linkBefore->instanceId;
        const Guid        tmplAnchor = linkBefore->templateEntityGuid;
        const bool        isRoot     = linkBefore->isInstanceRoot;
        const std::string srcPath    = linkBefore->sourcePrefabPath;

        // 模板演进 + refresh（确保走完整 merge/write-back 路径）。
        tmplWorld.GetComponent<NameComponent>(tmplRoot)->name = "B";
        const PrefabAsset newTmpl                             = MakePrefabFromRoot(tmplWorld, tmplRoot);
        const bool        ok                                  = RefreshInstanceFromTemplate(instWorld, instRoot, baseTmpl, newTmpl);
        assert(ok);

        // 身份 GUID 不动（实例仍是自己的 per-entity guid，没被模板 guid 覆盖）。
        assert(instWorld.GetComponent<GuidComponent>(instRoot)->guid == instGuid);
        // PrefabInstance 链接字段全不动。
        const auto* linkAfter = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
        assert(linkAfter != nullptr);
        assert(linkAfter->instanceId == instanceId);
        assert(linkAfter->templateEntityGuid == tmplAnchor);
        assert(linkAfter->isInstanceRoot == isRoot);
        assert(linkAfter->sourcePrefabPath == srcPath);
        // 业务字段确实刷新了（证明 refresh 真跑了，不是整体 no-op）。
        assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "B");

        std::printf("  [ok] T4 身份 component（Guid/PrefabInstance）refresh 后不动\n");
    }

    // ---------------------------------------------------------------------------
    // T5 失败路径 → no-op 返回 false、实例不变
    // ---------------------------------------------------------------------------
    void TestFailurePathsNoOp()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld, "A");
        const PrefabAsset baseTmpl = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, baseTmpl);

        // 模板演进（若 refresh 真跑会把 Name 改成 "B"——失败路径下不该发生）。
        tmplWorld.GetComponent<NameComponent>(tmplRoot)->name = "B";
        const PrefabAsset newTmpl                             = MakePrefabFromRoot(tmplWorld, tmplRoot);

        // (a) 空 templateEntityGuid（旧数据）→ no-op false、Name 仍 "A"。
        {
            auto*      link          = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
            const Guid saved         = link->templateEntityGuid;
            link->templateEntityGuid = Guid{};
            const bool ok            = RefreshInstanceFromTemplate(instWorld, instRoot, baseTmpl, newTmpl);
            assert(!ok);
            assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "A");
            link->templateEntityGuid = saved;
        }

        // (b) 模板里不存在的 templateEntityGuid → FindEntityByGuid 未命中 → no-op false。
        {
            auto*      link          = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
            const Guid saved         = link->templateEntityGuid;
            link->templateEntityGuid = Guid::Generate();
            const bool ok            = RefreshInstanceFromTemplate(instWorld, instRoot, baseTmpl, newTmpl);
            assert(!ok);
            assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "A");
            link->templateEntityGuid = saved;
        }

        // (c) 还原锚定后正常路径仍能刷新（证明前两步是因缺锚 no-op，非别的 bug）。
        {
            const bool ok = RefreshInstanceFromTemplate(instWorld, instRoot, baseTmpl, newTmpl);
            assert(ok);
            assert(instWorld.GetComponent<NameComponent>(instRoot)->name == "B");
        }

        std::printf("  [ok] T5 失败路径（空/未命中 guid）→ no-op false 实例不变；还原后能刷\n");
    }

    // ---------------------------------------------------------------------------
    // T6 底层入口 RefreshEntityFromTemplate（两 world 两 entity）+ 无效 entity graceful
    // ---------------------------------------------------------------------------
    void TestLowLevelEntryAndInvalid()
    {
        // base：bake 时模板（Name="A", pos=0）。
        World        baseW;
        const Entity base = BuildSampleRoot(baseW, "A");
        // new：演进后模板（Name="Evolved", pos.z=3）。
        World        newW;
        const Entity neo                                       = BuildSampleRoot(newW, "Evolved");
        newW.GetComponent<TransformComponent>(neo)->position.z = 3.0f;
        // mine：实例（手改 position.x=9 = override；Name 仍 "A" = 未 override，与 base 同）。
        World        instW;
        const Entity inst                                        = BuildSampleRoot(instW, "A");
        instW.GetComponent<TransformComponent>(inst)->position.x = 9.0f;

        // 三方 refresh：position.x=9（mine!=base）→ 保留 9；Name "A"==base → 拉 new
        // "Evolved"；position.z 0==base → 拉 new 3。
        const bool ok = RefreshEntityFromTemplate(instW, inst, baseW, base, newW, neo);
        assert(ok);
        assert(NearEq(instW.GetComponent<TransformComponent>(inst)->position.x, 9.0f));
        assert(NearEq(instW.GetComponent<TransformComponent>(inst)->position.z, 3.0f));
        assert(instW.GetComponent<NameComponent>(inst)->name == "Evolved");

        // 无效 entity（任一）→ no-op false 不崩。
        assert(!RefreshEntityFromTemplate(instW, Entity::Invalid(), baseW, base, newW, neo));
        assert(!RefreshEntityFromTemplate(instW, inst, baseW, Entity::Invalid(), newW, neo));
        assert(!RefreshEntityFromTemplate(instW, inst, baseW, base, newW, Entity::Invalid()));

        std::printf("  [ok] T6 底层 RefreshEntityFromTemplate（三方）+ 无效 entity graceful\n");
    }

} // namespace

int main()
{
    std::printf("[prefab_refresh_test]\n");
    TestOverridePreservedUnchangedUpdated();
    TestUnchangedInstanceIdempotent();
    TestMultiFieldMixed();
    TestIdentityComponentsUntouched();
    TestFailurePathsNoOp();
    TestLowLevelEntryAndInvalid();
    std::printf("[prefab_refresh_test] all passed\n");
    return 0;
}
