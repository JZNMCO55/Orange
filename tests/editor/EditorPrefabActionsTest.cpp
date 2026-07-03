// OrangeEditor prefab 消费层逻辑单测 —— 命令逻辑层（不经 ImGui / EditorHost）。
//
// 覆盖 GAP-2026-05-30-prefab 编辑器层 MVP 的两条核心逻辑路径：
//   T1 资产创建 round-trip：World 子树 → SaveSubtreeToString → PrefabLoader::
//      Save 临时文件 → AssetRegistry::Load<PrefabAsset> → TemplateBlob 非空保真。
//      （等价于 EditorPrefabActions::CommitNewPrefabFile 写盘 + 重新加载的核心，
//      不经 modal / ImGui。）
//   T2 实例化命令逻辑：InstantiatePrefabCommand 的 Execute/Undo/Redo 契约 ——
//      Execute = InstantiatePrefab（实例作新根，实体数增 + 实例根挂
//      PrefabInstanceComponent）；Undo = DestroySubtree（删干净，实体数回原值）；
//      Redo = 再 InstantiatePrefab（实例根是**新 EnTT id**，必须重新追踪才能
//      再 Undo 删对——这是命令用 shared_ptr<Entity> 追踪根的根因，delete-undo
//      踩过的坑）。
//
// 为什么不直接实例化 InstantiatePrefabCommand：它持 EditorHost&，而 EditorHost
// 拖 AudioEngine（构造期摸声卡）+ ThumbnailService（unique_ptr，dtor 需完整类型，
// 拖 Vulkan 重）。按 gap 设计的允许口径，本测试在 World/registry 上直接复刻命令
// 的 Execute/Undo/Redo 序列（命令本体只是把这几步包成 shared_ptr 追踪的薄壳，
// 真正逻辑就是这里验的引擎调用）。命令 TU（PrefabCommands.cpp）+ 写盘 helper TU
// （EditorPrefabActions.cpp）+ 图操作 TU（EditorHierarchy.cpp）仍链进本 exe，
// 保证它们在 headless 上下文里编译 / 链接零回归。纯逻辑、无 Vulkan。

#include "EditorHierarchy.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/asset/PrefabLoader.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/PrefabInstanceComponent.h>
#include <orange/engine/scene/PrefabInstantiation.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::PrefabAsset;
using Orange::Engine::Asset::PrefabLoader;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::PrefabInstanceComponent;
using Orange::Engine::Scene::TransformComponent;

namespace Scene = Orange::Engine::Scene;

namespace
{

    fs::path TempDir()
    {
        auto root = fs::temp_directory_path() / "orange_editor_prefab_actions_test";
        fs::create_directories(root);
        return root;
    }

    // 构一棵 root(Name="Root", pos=1,2,3) + child(Name="Child") 子树。
    std::array<Entity, 2> BuildSampleSubtree(World& w)
    {
        const Entity root  = w.CreateEntity();
        const Entity child = w.CreateEntity();

        w.AddComponent<NameComponent>(root, NameComponent{"Root"});
        w.AddComponent<NameComponent>(child, NameComponent{"Child"});
        {
            TransformComponent tc;
            tc.position = {1.0f, 2.0f, 3.0f};
            w.AddComponent<TransformComponent>(root, tc);
        }
        w.AddComponent<TransformComponent>(child, TransformComponent{});
        {
            HierarchyComponent rh;
            rh.firstChild = child;
            w.AddComponent<HierarchyComponent>(root, rh);
            HierarchyComponent ch;
            ch.parent = root;
            w.AddComponent<HierarchyComponent>(child, ch);
        }
        Scene::EnsureEntityGuids(w);
        return {root, child};
    }

    // 写一个 prefab 文件到 path（从一棵 sample 子树）。返回模板 blob 原文。
    std::string WriteSamplePrefab(const fs::path& path)
    {
        World                       tmpl;
        const auto                  subtree = BuildSampleSubtree(tmpl);
        const std::array<Entity, 1> roots{subtree[0]};
        auto                        blob = Scene::SaveSubtreeToString(tmpl, roots);
        assert(blob.IsOk());
        assert(!blob.Value().empty());
        auto saveRc =
            PrefabLoader::Save(path.generic_string(), "Sample", blob.Value());
        assert(saveRc.IsOk());
        return blob.Value();
    }

    // ---------------------------------------------------------------------------
    // T1 资产创建 round-trip（CommitNewPrefabFile 写盘 + Load 的核心）
    // ---------------------------------------------------------------------------
    void TestAssetRoundTrip(const fs::path& root)
    {
        const fs::path    prefabPath = root / "roundtrip.prefab.json";
        const std::string blob       = WriteSamplePrefab(prefabPath);

        AssetRegistry reg;
        auto          regRc = reg.RegisterLoader<PrefabAsset>(std::make_unique<PrefabLoader>());
        assert(regRc.IsOk());
        auto loaded = reg.Load<PrefabAsset>(prefabPath.generic_string());
        assert(loaded.IsOk());
        const PrefabAsset* asset = reg.Get(loaded.Value());
        assert(asset != nullptr);
        assert(!asset->TemplateBlob().empty());
        assert(asset->TemplateBlob() == blob); // 形态 B 字节保真

        std::printf("  [ok] T1 资产创建 round-trip（Save → Load → TemplateBlob 非空保真）\n");
    }

    // ---------------------------------------------------------------------------
    // T2 实例化命令逻辑（Execute = InstantiatePrefab / Undo = DestroySubtree /
    //    Redo = 再 InstantiatePrefab，shared_ptr 追踪新根的契约）
    // ---------------------------------------------------------------------------
    void TestInstantiateCommandLogic(const fs::path& root)
    {
        const fs::path prefabPath = root / "cmd.prefab.json";
        WriteSamplePrefab(prefabPath);

        AssetRegistry reg;
        auto          regRc = reg.RegisterLoader<PrefabAsset>(std::make_unique<PrefabLoader>());
        assert(regRc.IsOk());
        auto loaded = reg.Load<PrefabAsset>(prefabPath.generic_string());
        assert(loaded.IsOk());
        const AssetHandle<PrefabAsset> handle = loaded.Value();

        World w;
        assert(w.Size() == 0);

        // 命令以 shared_ptr<Entity> 追踪实例根（redo 是新 id，必须回写）。这里复刻
        // 同一追踪存储，验证 Execute → Undo → Redo → Undo 的实体数与 id 契约。
        auto rootPtr = std::make_shared<Entity>(Entity::Invalid());

        // LoadOptions 填 assetRegistry（最小依赖；本测试模板无材质 / animator）。
        Scene::LoadOptions lo;
        lo.assetRegistry = &reg;

        auto execute = [&]()
        {
            Scene::InstantiateOptions opt{};
            opt.parent      = Entity::Invalid(); // MVP：作新根
            opt.loadOptions = &lo;
            auto r          = Scene::InstantiatePrefab(w, reg, handle, opt);
            assert(r.IsOk());
            *rootPtr = r.Value();
        };
        auto undo = [&]()
        {
            if (rootPtr->IsValid() && w.IsValid(*rootPtr))
            {
                EditorHierarchy::DestroySubtree(w, *rootPtr);
            }
            *rootPtr = Entity::Invalid();
        };

        // Execute → 实体数 0 → 2（root + child）。
        execute();
        assert(w.Size() == 2);
        const Entity rootAfterExec = *rootPtr;
        assert(rootAfterExec.IsValid() && w.IsValid(rootAfterExec));
        // 实例根挂 PrefabInstanceComponent 且 isInstanceRoot + parent==Invalid。
        const auto* rootLink = w.GetComponent<PrefabInstanceComponent>(rootAfterExec);
        assert(rootLink != nullptr);
        assert(rootLink->isInstanceRoot);
        const auto* rootH = w.GetComponent<HierarchyComponent>(rootAfterExec);
        assert(rootH != nullptr && !rootH->parent.IsValid());

        // Undo → 删干净，实体数回 0。
        undo();
        assert(w.Size() == 0);
        assert(!rootPtr->IsValid());

        // Redo（再 Execute）→ 实体数再增到 2；实例根是新 EnTT id。
        execute();
        assert(w.Size() == 2);
        const Entity rootAfterRedo = *rootPtr;
        assert(rootAfterRedo.IsValid() && w.IsValid(rootAfterRedo));
        const auto* redoLink = w.GetComponent<PrefabInstanceComponent>(rootAfterRedo);
        assert(redoLink != nullptr && redoLink->isInstanceRoot);

        // 再 Undo 验证 redo 后追踪的（新）根能被删干净——若仍删旧 id 会留残 / EnTT
        // assert（delete-undo 踩过的坑，命令用 shared_ptr 回写新根正是为此）。
        undo();
        assert(w.Size() == 0);

        std::printf("  [ok] T2 实例化命令逻辑（Execute/Undo/Redo 实体数增删 + 实例根 "
                    "PrefabInstanceComponent + redo 新 id 追踪正确）\n");
    }

} // namespace

int main()
{
    std::printf("[editor_prefab_actions_test]\n");
    const fs::path root = TempDir();
    TestAssetRoundTrip(root);
    TestInstantiateCommandLogic(root);
    std::printf("[editor_prefab_actions_test] all passed\n");
    return 0;
}
