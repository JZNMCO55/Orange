// Prefab 引擎层 MVP 单测 —— 资产 round-trip + 实例化结构 + GUID 分离 +
// 链接组件 + 链接 round-trip + 错误路径。纯逻辑、headless 可测（零 GLFW /
// Vulkan）。仿 EntityGuidTest / SceneSubtreeCloneTest 的 cassert + main 模式。
//
// 覆盖：
//   T1 资产 round-trip：SaveSubtreeToString → PrefabLoader::Save → Load
//      → TemplateBlob() 字节保真；坏 schema → SchemaMismatch。
//   T2 实例化结构：InstantiatePrefab → 2 实体、根/子 hierarchy 链正确、
//      Name/Transform 保真。
//   T3 GUID 分离：两次实例化 → 实体 GUID 互不相同且都 ≠ 模板 GUID。
//   T4 链接组件：每个实例实体有 PrefabInstanceComponent、sourcePrefabPath
//      正确、同实例 instanceId 一致、跨实例不同、仅根 isInstanceRoot。
//   T5 链接 round-trip：含实例的 world → Scene::Save → Load → 三字段保真。
//   T6 错误路径：无效 handle → InvalidArgument。

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/asset/PrefabLoader.h>
#include <orange/engine/core/Guid.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
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
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using Orange::Engine::Entity;
using Orange::Engine::ResultCode;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::PrefabAsset;
using Orange::Engine::Asset::PrefabLoader;
using Orange::Engine::Core::Guid;
using Orange::Engine::Scene::GuidComponent;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::PrefabInstanceComponent;
using Orange::Engine::Scene::TransformComponent;

namespace Scene = Orange::Engine::Scene;

namespace
{

fs::path TempDir()
{
    auto root = fs::temp_directory_path() / "orange_engine_prefab_test";
    fs::create_directories(root);
    return root;
}

// 构一棵 root(Name="Root", pos=1,2,3) + child(Name="Child") 子树，
// child.parent=root / root.firstChild=child；EnsureEntityGuids 补身份。
// 返回 {root, child}。
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

// 在 registry 注册 PrefabLoader 并 Load 一个 prefab 文件 → handle。
AssetHandle<PrefabAsset> RegisterAndLoad(AssetRegistry& reg, const fs::path& path)
{
    auto regRc = reg.RegisterLoader<PrefabAsset>(std::make_unique<PrefabLoader>());
    assert(regRc.IsOk());
    auto loaded = reg.Load<PrefabAsset>(path.generic_string());
    assert(loaded.IsOk());
    return loaded.Value();
}

// ---------------------------------------------------------------------------
// T1 资产 round-trip
// ---------------------------------------------------------------------------
void TestAssetRoundTrip(const fs::path& root)
{
    World w;
    const auto subtree = BuildSampleSubtree(w);

    const std::array<Entity, 1> roots{subtree[0]};
    auto blob = Scene::SaveSubtreeToString(w, roots);
    assert(blob.IsOk());
    assert(!blob.Value().empty());

    const fs::path prefabPath = root / "sample.prefab.json";
    auto saveRc = PrefabLoader::Save(prefabPath.generic_string(), "SamplePrefab", blob.Value());
    assert(saveRc.IsOk());

    AssetRegistry reg;
    auto handle = RegisterAndLoad(reg, prefabPath);
    const PrefabAsset* asset = reg.Get(handle);
    assert(asset != nullptr);

    // 形态 B 字节保真：磁盘 round-trip 后 template 与原 blob 完全一致。
    assert(asset->TemplateBlob() == blob.Value());
    assert(asset->PrefabName() == "SamplePrefab");

    // 坏 schema → SchemaMismatch。写一个 namespace 错的文件。
    const fs::path badPath = root / "bad.prefab.json";
    {
        std::ofstream out(badPath);
        out << R"({"schemaVersion":{"namespace":"scene/world","major":1,"minor":0},)"
               R"("prefabName":"X","template":"{}"})";
    }
    PrefabLoader loader;
    auto badResult = loader.Load(badPath.generic_string());
    assert(badResult.IsErr());
    assert(badResult.Error() == ResultCode::SchemaMismatch);

    std::printf("  [ok] T1 资产 round-trip（形态 B 字节保真）+ 坏 schema → SchemaMismatch\n");
}

// ---------------------------------------------------------------------------
// T2 实例化结构
// ---------------------------------------------------------------------------
void TestInstantiateStructure(const fs::path& root)
{
    World tmplWorld;
    const auto subtree = BuildSampleSubtree(tmplWorld);
    const std::array<Entity, 1> roots{subtree[0]};
    auto blob = Scene::SaveSubtreeToString(tmplWorld, roots);
    assert(blob.IsOk());

    const fs::path prefabPath = root / "structure.prefab.json";
    assert(PrefabLoader::Save(prefabPath.generic_string(), "Structure", blob.Value()).IsOk());

    AssetRegistry reg;
    auto handle = RegisterAndLoad(reg, prefabPath);

    // 实例化到一个全新 world（独立于模板 world）。
    World w;
    auto rootResult = Scene::InstantiatePrefab(w, reg, handle);
    assert(rootResult.IsOk());
    const Entity instanceRoot = rootResult.Value();
    assert(instanceRoot.IsValid());

    // world 共 2 实体（root + child）。
    assert(w.Size() == 2);

    // 找根 / 子：根 parent==Invalid，子 parent==根。
    const auto* rootH = w.GetComponent<HierarchyComponent>(instanceRoot);
    assert(rootH != nullptr);
    assert(!rootH->parent.IsValid());
    const Entity childEntity = rootH->firstChild;
    assert(childEntity.IsValid());
    const auto* childH = w.GetComponent<HierarchyComponent>(childEntity);
    assert(childH != nullptr);
    assert(childH->parent == instanceRoot);  // 子的 parent 指向实例根，非模板根

    // Name / Transform 保真。
    assert(w.GetComponent<NameComponent>(instanceRoot)->name == "Root");
    assert(w.GetComponent<NameComponent>(childEntity)->name == "Child");
    const auto* rootTc = w.GetComponent<TransformComponent>(instanceRoot);
    assert(rootTc != nullptr);
    assert(rootTc->position.x == 1.0f && rootTc->position.y == 2.0f
           && rootTc->position.z == 3.0f);

    std::printf("  [ok] T2 实例化结构（2 实体 + 根/子链 + Name/Transform 保真）\n");
}

// ---------------------------------------------------------------------------
// T3 GUID 分离
// ---------------------------------------------------------------------------
void TestGuidSeparation(const fs::path& root)
{
    World tmplWorld;
    const auto subtree = BuildSampleSubtree(tmplWorld);
    const Guid tmplRootGuid  = tmplWorld.GetComponent<GuidComponent>(subtree[0])->guid;
    const Guid tmplChildGuid = tmplWorld.GetComponent<GuidComponent>(subtree[1])->guid;
    const std::array<Entity, 1> roots{subtree[0]};
    auto blob = Scene::SaveSubtreeToString(tmplWorld, roots);
    assert(blob.IsOk());

    const fs::path prefabPath = root / "guid.prefab.json";
    assert(PrefabLoader::Save(prefabPath.generic_string(), "Guid", blob.Value()).IsOk());

    AssetRegistry reg;
    auto handle = RegisterAndLoad(reg, prefabPath);

    World w;
    auto r1 = Scene::InstantiatePrefab(w, reg, handle);
    auto r2 = Scene::InstantiatePrefab(w, reg, handle);
    assert(r1.IsOk() && r2.IsOk());

    // 收集每个实例的两实体 GUID。
    auto collectGuids = [&w](Entity instRoot) -> std::array<Guid, 2>
    {
        const auto* h = w.GetComponent<HierarchyComponent>(instRoot);
        const Entity child = h->firstChild;
        return {w.GetComponent<GuidComponent>(instRoot)->guid,
                w.GetComponent<GuidComponent>(child)->guid};
    };
    const auto g1 = collectGuids(r1.Value());
    const auto g2 = collectGuids(r2.Value());

    // 四个实例 GUID 全部有效。
    for (const Guid& g : {g1[0], g1[1], g2[0], g2[1]})
    {
        assert(g.IsValid());
    }
    // 实例内根 ≠ 子。
    assert(g1[0] != g1[1]);
    assert(g2[0] != g2[1]);
    // 两实例对应实体 GUID 互不相同（实例化各自 Reassign）。
    assert(g1[0] != g2[0]);
    assert(g1[1] != g2[1]);
    assert(g1[0] != g2[1] && g1[1] != g2[0]);
    // 都 ≠ 模板 GUID。
    for (const Guid& g : {g1[0], g2[0]})
    {
        assert(g != tmplRootGuid);
    }
    for (const Guid& g : {g1[1], g2[1]})
    {
        assert(g != tmplChildGuid);
    }

    std::printf("  [ok] T3 GUID 分离（两实例互不相同 + 都 ≠ 模板）\n");
}

// ---------------------------------------------------------------------------
// T4 链接组件
// ---------------------------------------------------------------------------
void TestLinkComponent(const fs::path& root)
{
    World tmplWorld;
    const auto subtree = BuildSampleSubtree(tmplWorld);
    const std::array<Entity, 1> roots{subtree[0]};
    auto blob = Scene::SaveSubtreeToString(tmplWorld, roots);
    assert(blob.IsOk());

    const fs::path prefabPath = root / "link.prefab.json";
    assert(PrefabLoader::Save(prefabPath.generic_string(), "Link", blob.Value()).IsOk());

    AssetRegistry reg;
    auto handle = RegisterAndLoad(reg, prefabPath);
    const std::string expectedPath(reg.PathOf(handle));

    World w;
    auto r1 = Scene::InstantiatePrefab(w, reg, handle);
    auto r2 = Scene::InstantiatePrefab(w, reg, handle);
    assert(r1.IsOk() && r2.IsOk());

    auto checkInstance = [&](Entity instRoot) -> Guid
    {
        const auto* rootLink = w.GetComponent<PrefabInstanceComponent>(instRoot);
        assert(rootLink != nullptr);
        assert(rootLink->sourcePrefabPath == expectedPath);
        assert(rootLink->isInstanceRoot);  // 根 isInstanceRoot=true

        const Entity child = w.GetComponent<HierarchyComponent>(instRoot)->firstChild;
        const auto* childLink = w.GetComponent<PrefabInstanceComponent>(child);
        assert(childLink != nullptr);
        assert(childLink->sourcePrefabPath == expectedPath);
        assert(!childLink->isInstanceRoot);  // 子 isInstanceRoot=false

        // 同实例 instanceId 一致。
        assert(rootLink->instanceId == childLink->instanceId);
        assert(rootLink->instanceId.IsValid());
        return rootLink->instanceId;
    };
    const Guid id1 = checkInstance(r1.Value());
    const Guid id2 = checkInstance(r2.Value());

    // 跨实例 instanceId 不同。
    assert(id1 != id2);

    std::printf("  [ok] T4 链接组件（sourcePrefabPath + instanceId 同实例一致/跨实例不同 + root 标志）\n");
}

// ---------------------------------------------------------------------------
// T5 链接 round-trip
// ---------------------------------------------------------------------------
void TestLinkRoundTrip(const fs::path& root)
{
    World tmplWorld;
    const auto subtree = BuildSampleSubtree(tmplWorld);
    const std::array<Entity, 1> roots{subtree[0]};
    auto blob = Scene::SaveSubtreeToString(tmplWorld, roots);
    assert(blob.IsOk());

    const fs::path prefabPath = root / "rt.prefab.json";
    assert(PrefabLoader::Save(prefabPath.generic_string(), "RT", blob.Value()).IsOk());

    AssetRegistry reg;
    auto handle = RegisterAndLoad(reg, prefabPath);

    // 含实例的 world。
    World w;
    auto r1 = Scene::InstantiatePrefab(w, reg, handle);
    assert(r1.IsOk());
    const Entity instRoot = r1.Value();
    const auto* origLink = w.GetComponent<PrefabInstanceComponent>(instRoot);
    const std::string origPath = origLink->sourcePrefabPath;
    const Guid        origId   = origLink->instanceId;
    const bool        origRoot = origLink->isInstanceRoot;

    // Scene::Save 到磁盘 → 全新 world Load。assetRegistry 传 &reg，让 Save
    // 路径能反查 handle（本组件不持 handle，但保持调用对称）。
    const fs::path scenePath = root / "with_instance.scene.json";
    Scene::SaveOptions saveOpt;
    saveOpt.assetRegistry = &reg;
    auto sceneSaveRc = Scene::Save(w, scenePath.generic_string(), saveOpt);
    assert(sceneSaveRc.IsOk());

    World w2;
    Scene::LoadOptions loadOpt;
    loadOpt.assetRegistry = &reg;
    auto sceneLoadRc = Scene::Load(scenePath.generic_string(), w2, loadOpt);
    assert(sceneLoadRc.IsOk());

    // 找回挂 PrefabInstanceComponent 且 isInstanceRoot 的实体。
    Entity reloadedRoot = Entity::Invalid();
    for (auto e : w2.Registry().view<PrefabInstanceComponent>())
    {
        const Entity entity = World::FromEntt(e);
        const auto* link = w2.GetComponent<PrefabInstanceComponent>(entity);
        if (link->isInstanceRoot)
        {
            reloadedRoot = entity;
            break;
        }
    }
    assert(reloadedRoot.IsValid());
    const auto* reloadedLink = w2.GetComponent<PrefabInstanceComponent>(reloadedRoot);
    assert(reloadedLink != nullptr);

    // 三字段保真（验证 ComponentSerializers + schema bump 1.13）。
    assert(reloadedLink->sourcePrefabPath == origPath);
    assert(reloadedLink->instanceId == origId);
    assert(reloadedLink->isInstanceRoot == origRoot);

    std::printf("  [ok] T5 链接 round-trip（PrefabInstanceComponent 三字段保真）\n");
}

// ---------------------------------------------------------------------------
// T6 错误路径
// ---------------------------------------------------------------------------
void TestErrorPaths()
{
    AssetRegistry reg;
    World w;

    // 无效 handle → InvalidArgument。
    AssetHandle<PrefabAsset> invalidHandle;
    auto r = Scene::InstantiatePrefab(w, reg, invalidHandle);
    assert(r.IsErr());
    assert(r.Error() == ResultCode::InvalidArgument);
    assert(w.Size() == 0);  // 失败时 world 未被改动

    std::printf("  [ok] T6 错误路径（无效 handle → InvalidArgument）\n");
}

}  // namespace

int main()
{
    std::printf("[scene_prefab_test]\n");
    const fs::path root = TempDir();
    TestAssetRoundTrip(root);
    TestInstantiateStructure(root);
    TestGuidSeparation(root);
    TestLinkComponent(root);
    TestLinkRoundTrip(root);
    TestErrorPaths();
    std::printf("[scene_prefab_test] all passed\n");
    return 0;
}
