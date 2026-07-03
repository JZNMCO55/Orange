// Prefab 实例↔模板 override diff 单测（C1 CS1）—— headless 真测。
//
// 覆盖 c1-prefab-override-design.md §3 CS1 的核心契约：
//   T1 未改实例：ComputeInstanceOverrides 返回空（实例==模板，除身份 component 外
//      无 override）。
//   T2 改单字段（root Transform.position → (5,0,0)）：精确报出 Transform/position/0，
//      未改字段（position/1、position/2、rotation、scale）不报。
//   T3 改多字段 / 多 component：各 report（Transform.position + Renderable.visible）。
//   T4 身份 component（Guid / PrefabInstance）不同也不报（已过滤）。
//   T5 模板 entity 缺失（伪造空 / 不匹配 templateEntityGuid）→ 返回空不崩。
//   T6 底层入口 ComputeEntityOverrides：两 world 两 entity 直接 diff（含无效 entity
//      graceful）。
//
// 纯逻辑、零 GLFW/Vulkan，仿 PrefabTest 的 cassert + main 模式。

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
using Orange::Engine::Scene::ComputeEntityOverrides;
using Orange::Engine::Scene::ComputeInstanceOverrides;
using Orange::Engine::Scene::GuidComponent;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::OverrideField;
using Orange::Engine::Scene::PrefabInstanceComponent;
using Orange::Engine::Scene::TransformComponent;

namespace Scene = Orange::Engine::Scene;

namespace
{

    // override 字段集里是否含 component/fieldPath 这一条。
    bool HasOverride(const std::vector<OverrideField>& fields,
                     const std::string& component, const std::string& fieldPath)
    {
        for (const auto& f : fields)
        {
            if (f.componentName == component && f.fieldPath == fieldPath)
            {
                return true;
            }
        }
        return false;
    }

    // 是否含某 component 的任意 override 字段。
    bool HasComponentOverride(const std::vector<OverrideField>& fields,
                              const std::string&                component)
    {
        for (const auto& f : fields)
        {
            if (f.componentName == component)
            {
                return true;
            }
        }
        return false;
    }

    // 构一棵 root(Name="Root", Transform pos=(0,0,0), Renderable visible=true,
    // castsShadow=true) 子树（单 root，无子）。EnsureEntityGuids 补身份。返回 root。
    Entity BuildSampleRoot(World& w)
    {
        const Entity root = w.CreateEntity();
        w.AddComponent<NameComponent>(root, NameComponent{"Root"});
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

    // 从 root 子树构一个 PrefabAsset（内存形态，不落盘——TemplateBlob 直接用
    // SaveSubtreeToString 的产物）。
    PrefabAsset MakePrefabFromRoot(World& w, Entity root)
    {
        const std::array<Entity, 1> roots{root};
        auto                        blob = Scene::SaveSubtreeToString(w, roots);
        assert(blob.IsOk());
        return PrefabAsset{"SamplePrefab", blob.Value()};
    }

    // 把 prefab 实例化到 instWorld（经临时 AssetRegistry + Insert），返回实例根。
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
    // T1 未改实例 → 空
    // ---------------------------------------------------------------------------
    void TestUnchangedInstanceNoOverride()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld);
        const PrefabAsset tmpl     = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, tmpl);

        const auto overrides = ComputeInstanceOverrides(instWorld, instRoot, tmpl);
        // 实例==模板（除身份 component）→ 无 override。
        assert(overrides.empty());

        std::printf("  [ok] T1 未改实例 → override 空\n");
    }

    // ---------------------------------------------------------------------------
    // T2 改单字段 → 精确报出该叶子，未改不报
    // ---------------------------------------------------------------------------
    void TestSingleFieldOverride()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld);
        const PrefabAsset tmpl     = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, tmpl);

        // 改实例 root 的 Transform.position.x → 5（y/z 不动）。
        auto* tc = instWorld.GetComponent<TransformComponent>(instRoot);
        assert(tc != nullptr);
        tc->position.x = 5.0f;

        const auto overrides = ComputeInstanceOverrides(instWorld, instRoot, tmpl);

        // position[0] 被报出。
        assert(HasOverride(overrides, "Transform", "position/0"));
        // 未改字段不报。
        assert(!HasOverride(overrides, "Transform", "position/1"));
        assert(!HasOverride(overrides, "Transform", "position/2"));
        assert(!HasOverride(overrides, "Transform", "rotation/0"));
        assert(!HasOverride(overrides, "Transform", "scale/0"));
        // 其他 component 完全没动 → 不报。
        assert(!HasComponentOverride(overrides, "Renderable"));
        assert(!HasComponentOverride(overrides, "Name"));
        // 恰好一条 override。
        assert(overrides.size() == 1);

        std::printf("  [ok] T2 改单字段 → 精确报 Transform/position/0（未改不报）\n");
    }

    // ---------------------------------------------------------------------------
    // T3 改多字段 / 多 component → 各 report
    // ---------------------------------------------------------------------------
    void TestMultiFieldMultiComponent()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld);
        const PrefabAsset tmpl     = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, tmpl);

        // Transform：position.x → 5、position.z → -2（两个叶子）。
        auto* tc       = instWorld.GetComponent<TransformComponent>(instRoot);
        tc->position.x = 5.0f;
        tc->position.z = -2.0f;
        // Renderable：visible true → false、castsShadow true → false。
        auto* rc        = instWorld.GetComponent<RenderableComponent>(instRoot);
        rc->visible     = false;
        rc->castsShadow = false;
        // Name：改名 → name 字段 override（字符串叶子）。
        auto* nc = instWorld.GetComponent<NameComponent>(instRoot);
        nc->name = "RenamedRoot";

        const auto overrides = ComputeInstanceOverrides(instWorld, instRoot, tmpl);

        assert(HasOverride(overrides, "Transform", "position/0"));
        assert(HasOverride(overrides, "Transform", "position/2"));
        assert(!HasOverride(overrides, "Transform", "position/1")); // y 没动
        assert(HasOverride(overrides, "Renderable", "visible"));
        assert(HasOverride(overrides, "Renderable", "castsShadow"));
        assert(HasOverride(overrides, "Name", "name"));
        // 共 5 条（pos[0] / pos[2] / visible / castsShadow / name）。
        assert(overrides.size() == 5);

        std::printf("  [ok] T3 改多字段/多 component → 各 report（共 5 条）\n");
    }

    // ---------------------------------------------------------------------------
    // T4 身份 component 不同也不报（已过滤）
    // ---------------------------------------------------------------------------
    void TestIdentityComponentsFiltered()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld);
        const PrefabAsset tmpl     = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, tmpl);

        // 实例化天然让 Guid / PrefabInstance 与模板不同（换新 per-entity guid +
        // 实例独有 PrefabInstanceComponent）。不改任何业务字段。
        // 实例 guid ≠ 模板 guid（确认前提成立）。
        const Guid instGuid = instWorld.GetComponent<GuidComponent>(instRoot)->guid;
        assert(instGuid.IsValid());
        // 实例有 PrefabInstanceComponent（模板侧没有）。
        assert(instWorld.GetComponent<PrefabInstanceComponent>(instRoot) != nullptr);

        const auto overrides = ComputeInstanceOverrides(instWorld, instRoot, tmpl);

        // 身份/链接 component 全被过滤 → 不报。
        assert(!HasComponentOverride(overrides, "Guid"));
        assert(!HasComponentOverride(overrides, "PrefabInstance"));
        assert(!HasComponentOverride(overrides, "Hierarchy"));
        // 业务字段没动 → 整体空。
        assert(overrides.empty());

        std::printf("  [ok] T4 身份 component（Guid/PrefabInstance/Hierarchy）不报\n");
    }

    // ---------------------------------------------------------------------------
    // T5 模板 entity 缺失 → 返回空不崩
    // ---------------------------------------------------------------------------
    void TestMissingTemplateEntity()
    {
        World             tmplWorld;
        const Entity      tmplRoot = BuildSampleRoot(tmplWorld);
        const PrefabAsset tmpl     = MakePrefabFromRoot(tmplWorld, tmplRoot);

        World         instWorld;
        AssetRegistry reg;
        const Entity  instRoot = InstantiateInto(instWorld, reg, tmpl);

        // 改实例字段（确认正常路径本会报，对照下方"缺失锚定"应转空）。
        instWorld.GetComponent<TransformComponent>(instRoot)->position.x = 9.0f;

        // (a) 伪造空 templateEntityGuid（旧数据）→ 无从配对 → 空。
        {
            auto*      link          = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
            const Guid saved         = link->templateEntityGuid;
            link->templateEntityGuid = Guid{}; // 空 guid
            const auto overrides     = ComputeInstanceOverrides(instWorld, instRoot, tmpl);
            assert(overrides.empty());
            link->templateEntityGuid = saved; // 还原
        }

        // (b) 伪造一个模板里不存在的 templateEntityGuid → FindEntityByGuid 未命中 → 空。
        {
            auto*      link          = instWorld.GetComponent<PrefabInstanceComponent>(instRoot);
            const Guid saved         = link->templateEntityGuid;
            link->templateEntityGuid = Guid::Generate(); // 几乎不可能命中模板
            const auto overrides     = ComputeInstanceOverrides(instWorld, instRoot, tmpl);
            assert(overrides.empty());
            link->templateEntityGuid = saved;
        }

        // (c) 还原锚定后正常路径仍能报出改动（证明前两步是因缺锚而空，非别的 bug）。
        {
            const auto overrides = ComputeInstanceOverrides(instWorld, instRoot, tmpl);
            assert(HasOverride(overrides, "Transform", "position/0"));
        }

        std::printf("  [ok] T5 模板 entity 缺失（空/未命中 guid）→ 空不崩；还原后仍能报\n");
    }

    // ---------------------------------------------------------------------------
    // T6 底层入口 ComputeEntityOverrides（两 world 两 entity）+ 无效 entity graceful
    // ---------------------------------------------------------------------------
    void TestLowLevelEntryAndInvalid()
    {
        // 两个独立 world，各建一个等价实体；改其中一个的 Transform.position.y。
        World        wa;
        const Entity ea = BuildSampleRoot(wa);
        World        wb;
        const Entity eb                                     = BuildSampleRoot(wb);
        wa.GetComponent<TransformComponent>(ea)->position.y = 3.0f;

        const auto overrides = ComputeEntityOverrides(wa, ea, wb, eb);
        assert(HasOverride(overrides, "Transform", "position/1"));
        assert(!HasOverride(overrides, "Transform", "position/0"));
        // 身份 component 在两 world 各自 EnsureEntityGuids 出不同 guid，但被过滤 → 不报。
        assert(!HasComponentOverride(overrides, "Guid"));
        assert(overrides.size() == 1);

        // 无效 entity → 空不崩。
        assert(ComputeEntityOverrides(wa, Entity::Invalid(), wb, eb).empty());
        assert(ComputeEntityOverrides(wa, ea, wb, Entity::Invalid()).empty());

        // 两侧完全相同（同一 world 自比）→ 仅身份 component 不同被过滤 → 空。
        assert(ComputeEntityOverrides(wb, eb, wb, eb).empty());

        std::printf("  [ok] T6 底层 ComputeEntityOverrides + 无效 entity graceful\n");
    }

} // namespace

int main()
{
    std::printf("[prefab_override_test]\n");
    TestUnchangedInstanceNoOverride();
    TestSingleFieldOverride();
    TestMultiFieldMultiComponent();
    TestIdentityComponentsFiltered();
    TestMissingTemplateEntity();
    TestLowLevelEntryAndInvalid();
    std::printf("[prefab_override_test] all passed\n");
    return 0;
}
