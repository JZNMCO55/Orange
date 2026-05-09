// Scene 序列化端到端单元测试。
//
// 覆盖：
//   * 空 World round-trip
//   * 单 entity（Transform / Hierarchy / Name 三件套）round-trip
//   * 多 entity + Hierarchy 父子链 round-trip
//   * schemaVersion 缺失 / mismatch → 拒绝读，World 保持原状
//   * 损坏 JSON → 拒绝读，World 保持原状

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using Orange::Engine::Entity;
using Orange::Engine::ResultCode;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::TransformComponent;
namespace SceneSerialization = Orange::Engine::Scene;

namespace
{

// 每个 test case 用一个隔离的临时 .scene.json 文件——跑完即删，避免
// 互相污染。统一放 std::filesystem::temp_directory_path 下。
std::filesystem::path MakeTempScenePath(const char* tag)
{
    auto base = std::filesystem::temp_directory_path();
    base /= std::string{"orange_engine_scene_test_"} + tag + ".scene.json";
    std::error_code ec;
    std::filesystem::remove(base, ec);  // 残留清掉
    return base;
}

void RemoveIfExists(const std::filesystem::path& p) noexcept
{
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

bool FloatEq(float a, float b, float eps = 1e-5f) noexcept
{
    return std::fabs(a - b) <= eps;
}

void TestEmptyWorldRoundTrip()
{
    const auto path = MakeTempScenePath("empty");

    World source;
    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Empty());

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] empty world round-trip\n");
}

void TestSingleEntityThreeComponentsRoundTrip()
{
    const auto path = MakeTempScenePath("single");

    World source;
    Entity e = source.CreateEntity();

    TransformComponent tx;
    tx.position = {1.5f, -2.25f, 3.75f};
    tx.rotation = glm::quat(0.7071068f, 0.0f, 0.7071068f, 0.0f);  // (w, x, y, z)
    tx.scale    = {2.0f, 1.0f, 0.5f};
    source.AddComponent(e, tx);

    HierarchyComponent h;
    // 单实体没有父子，全 invalid——验证 invalid → -1 → invalid 这条往返。
    source.AddComponent(e, h);

    NameComponent name;
    name.name = "lonely_root";
    source.AddComponent(e, name);

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    // 通过 view 找回那个唯一实体——entt 会重新分配新的 entity 句柄，
    // 不能假设新 World 里的 Entity 数值与旧 World 相同。
    auto& reg = loaded.Registry();
    auto view = reg.view<TransformComponent>();
    int hits = 0;
    Entity loadedE = Entity::Invalid();
    for (auto ent : view)
    {
        ++hits;
        loadedE = World::FromEntt(ent);
    }
    assert(hits == 1);

    const auto* loadedTx = loaded.GetComponent<TransformComponent>(loadedE);
    assert(loadedTx != nullptr);
    assert(FloatEq(loadedTx->position.x, 1.5f));
    assert(FloatEq(loadedTx->position.y, -2.25f));
    assert(FloatEq(loadedTx->position.z, 3.75f));
    assert(FloatEq(loadedTx->rotation.w, 0.7071068f));
    assert(FloatEq(loadedTx->rotation.x, 0.0f));
    assert(FloatEq(loadedTx->rotation.y, 0.7071068f));
    assert(FloatEq(loadedTx->rotation.z, 0.0f));
    assert(FloatEq(loadedTx->scale.x, 2.0f));
    assert(FloatEq(loadedTx->scale.z, 0.5f));

    const auto* loadedH = loaded.GetComponent<HierarchyComponent>(loadedE);
    assert(loadedH != nullptr);
    assert(!loadedH->parent.IsValid());
    assert(!loadedH->firstChild.IsValid());
    assert(!loadedH->nextSibling.IsValid());
    assert(!loadedH->prevSibling.IsValid());

    const auto* loadedName = loaded.GetComponent<NameComponent>(loadedE);
    assert(loadedName != nullptr);
    assert(loadedName->name == "lonely_root");

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] single entity three-components round-trip\n");
}

void TestHierarchyChainRoundTrip()
{
    const auto path = MakeTempScenePath("hierarchy");

    // 构造：root → [a, b, c]，全部带 Name 便于反序列化后定位。
    World source;
    Entity root = source.CreateEntity();
    Entity a    = source.CreateEntity();
    Entity b    = source.CreateEntity();
    Entity c    = source.CreateEntity();

    source.AddComponent<NameComponent>(root, {"root"});
    source.AddComponent<NameComponent>(a,    {"a"});
    source.AddComponent<NameComponent>(b,    {"b"});
    source.AddComponent<NameComponent>(c,    {"c"});

    source.AddComponent<HierarchyComponent>(root, {});
    source.AddComponent<HierarchyComponent>(a, {});
    source.AddComponent<HierarchyComponent>(b, {});
    source.AddComponent<HierarchyComponent>(c, {});

    auto* rootH = source.GetComponent<HierarchyComponent>(root);
    auto* aH    = source.GetComponent<HierarchyComponent>(a);
    auto* bH    = source.GetComponent<HierarchyComponent>(b);
    auto* cH    = source.GetComponent<HierarchyComponent>(c);

    rootH->firstChild = a;
    aH->parent       = root;
    aH->nextSibling  = b;
    bH->parent       = root;
    bH->prevSibling  = a;
    bH->nextSibling  = c;
    cH->parent       = root;
    cH->prevSibling  = b;

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 4);

    // 按 Name 反向定位 4 个 entity。
    Entity lroot = Entity::Invalid();
    Entity la    = Entity::Invalid();
    Entity lb    = Entity::Invalid();
    Entity lc    = Entity::Invalid();
    auto& reg = loaded.Registry();
    for (auto ent : reg.view<NameComponent>())
    {
        const auto& nc = reg.get<NameComponent>(ent);
        const Entity h = World::FromEntt(ent);
        if (nc.name == "root") lroot = h;
        else if (nc.name == "a") la = h;
        else if (nc.name == "b") lb = h;
        else if (nc.name == "c") lc = h;
    }
    assert(lroot.IsValid() && la.IsValid() && lb.IsValid() && lc.IsValid());

    // 验证链：root.firstChild == a；a.next == b；b.prev == a；b.next == c；
    // 全员 parent == root。
    const auto* lrootH = loaded.GetComponent<HierarchyComponent>(lroot);
    const auto* laH    = loaded.GetComponent<HierarchyComponent>(la);
    const auto* lbH    = loaded.GetComponent<HierarchyComponent>(lb);
    const auto* lcH    = loaded.GetComponent<HierarchyComponent>(lc);
    assert(lrootH && laH && lbH && lcH);
    assert(lrootH->firstChild == la);
    assert(laH->parent       == lroot);
    assert(laH->nextSibling  == lb);
    assert(lbH->parent       == lroot);
    assert(lbH->prevSibling  == la);
    assert(lbH->nextSibling  == lc);
    assert(lcH->parent       == lroot);
    assert(lcH->prevSibling  == lb);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] hierarchy chain round-trip\n");
}

void TestSchemaMismatchRejected()
{
    const auto path = MakeTempScenePath("schema_mismatch");

    // 手写一个 namespace 对不上的 scene 文件。
    {
        std::ofstream out(path);
        out <<
            R"({
              "schemaVersion": { "namespace": "scene/world", "major": 99, "minor": 0 },
              "entities": []
            })";
    }

    World loaded;
    Entity preExisting = loaded.CreateEntity();  // 验证 Load 失败时这条不被波及
    (void)preExisting;

    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsErr());
    assert(loadResult.Error() == ResultCode::SchemaMismatch);
    assert(loaded.Size() == 1);  // 仅原本存在的那条；新 entity 不该被 create

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] schema mismatch rejected, world unchanged\n");
}

void TestCorruptJsonRejected()
{
    const auto path = MakeTempScenePath("corrupt");

    // 故意写一个语法不合法的 JSON。
    {
        std::ofstream out(path);
        out << "{ this is not valid json ::: ";
    }

    World loaded;
    Entity preExisting = loaded.CreateEntity();
    (void)preExisting;

    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsErr());
    // 解析失败 → InvalidArgument（来自 JsonReader::FromFile 的 ParseError.code）
    assert(loaded.Size() == 1);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] corrupt JSON rejected, world unchanged\n");
}

void TestPartialFailureRollsBack()
{
    const auto path = MakeTempScenePath("partial_fail");

    // 文件 schema 正确，但第二个 entity 的 Transform 缺 position 字段——
    // ReadFloatArray 严格匹配长度 → 返回 false。SceneSerialization::Load
    // 应整体回滚（不留下第一个 entity）。
    {
        std::ofstream out(path);
        out <<
            R"({
              "schemaVersion": { "namespace": "scene/world", "major": 1, "minor": 0 },
              "entities": [
                {
                  "id": 0,
                  "components": {
                    "Transform": {
                      "position": [0, 0, 0],
                      "rotation": [0, 0, 0, 1],
                      "scale":    [1, 1, 1]
                    }
                  }
                },
                {
                  "id": 1,
                  "components": {
                    "Transform": {
                      "rotation": [0, 0, 0, 1],
                      "scale":    [1, 1, 1]
                    }
                  }
                }
              ]
            })";
    }

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsErr());
    assert(loadResult.Error() == ResultCode::InvalidArgument);
    assert(loaded.Empty());  // 第一个 entity 也应该被回滚

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] partial-failure rolls back created entities\n");
}

void TestMixedKnownAndUnknownComponents()
{
    const auto path = MakeTempScenePath("forward_compat");

    // 把一个未来版本的"Foo"组件混进 components 对象——加载层应静默
    // 跳过它，不视为 fatal，并把已识别的 Name 正常装回去。
    {
        std::ofstream out(path);
        out <<
            R"({
              "schemaVersion": { "namespace": "scene/world", "major": 1, "minor": 0 },
              "entities": [
                {
                  "id": 0,
                  "components": {
                    "Name": { "name": "compat_target" },
                    "Foo":  { "bar": 42 }
                  }
                }
              ]
            })";
    }

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    int hits = 0;
    for (auto ent : reg.view<NameComponent>())
    {
        const auto& nc = reg.get<NameComponent>(ent);
        assert(nc.name == "compat_target");
        ++hits;
    }
    assert(hits == 1);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] forward-compat: unknown component skipped\n");
}

// 在 registry 中"程序式"塞一个 mesh，方便往返测试不依赖磁盘上的真
// .orme 文件。Insert 走的是同一份 path → handle 表，PathOf 反查能命中。
Orange::Engine::Asset::AssetHandle<MeshAsset>
InsertSyntheticMesh(AssetRegistry& reg, std::string_view path)
{
    auto mesh = std::make_unique<MeshAsset>(
        std::vector<Orange::Engine::Asset::VertexPosition3>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},
        std::vector<std::uint32_t>{0, 1, 2});
    auto result = reg.Insert<MeshAsset>(path, std::move(mesh));
    assert(result.IsOk());
    return result.Value();
}

void TestRenderableRoundTripWithRegistry()
{
    const auto path = MakeTempScenePath("renderable");

    AssetRegistry srcReg;
    auto handle = InsertSyntheticMesh(srcReg, "meshes/square.mesh");

    World source;
    Entity e = source.CreateEntity();
    source.AddComponent<NameComponent>(e, {"renderable_target"});
    RenderableComponent r;
    r.mesh        = handle;
    r.visible     = false;
    r.castsShadow = false;
    source.AddComponent(e, r);

    auto saveResult = SceneSerialization::Save(source, path.string(), &srcReg);
    assert(saveResult.IsOk());

    // Load 端用一个新的 registry——验证"路径 → 重新 Insert"链路。
    // 引擎内置 mesh loader 需要真磁盘文件；这里走 Insert 提前把同 path
    // 挂进 dst registry，模拟 Load<Mesh>(path) 命中 dedup 直接返回。
    AssetRegistry dstReg;
    auto preloaded = InsertSyntheticMesh(dstReg, "meshes/square.mesh");
    (void)preloaded;

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded, &dstReg);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    int hits = 0;
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<RenderableComponent>())
    {
        ++hits;
        loadedE = World::FromEntt(ent);
    }
    assert(hits == 1);

    const auto* loadedR = loaded.GetComponent<RenderableComponent>(loadedE);
    assert(loadedR != nullptr);
    assert(loadedR->mesh.IsValid());
    // 反查回 path —— 验证 handle 真的指向我们 dstReg 里那条 mesh 而不是
    // 误用了 srcReg 的句柄数值。
    assert(dstReg.PathOf<MeshAsset>(loadedR->mesh) == "meshes/square.mesh");
    assert(loadedR->visible     == false);
    assert(loadedR->castsShadow == false);
    // materialInstance 不参与序列化——读回来必须是 nullptr，调用方在
    // Load 之后自己挂 instance。
    assert(loadedR->materialInstance == nullptr);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] renderable round-trip with AssetRegistry path resolution\n");
}

void TestRenderableWithoutRegistryGraceful()
{
    const auto path = MakeTempScenePath("renderable_no_registry");

    AssetRegistry srcReg;
    auto handle = InsertSyntheticMesh(srcReg, "meshes/square.mesh");

    World source;
    Entity e = source.CreateEntity();
    RenderableComponent r;
    r.mesh    = handle;
    r.visible = true;
    source.AddComponent(e, r);

    // Save 不传 registry → mesh 字段写空 path（warn）；scene 仍能保存。
    auto saveResult = SceneSerialization::Save(source, path.string(), nullptr);
    assert(saveResult.IsOk());

    // Load 不传 registry → 即使 path 被写空也照常 attach RenderableComponent，
    // 只是 handle 留空。pure-data 字段（visible / castsShadow）不受影响。
    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded, nullptr);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<RenderableComponent>())
    {
        loadedE = World::FromEntt(ent);
    }
    assert(loadedE.IsValid());
    const auto* loadedR = loaded.GetComponent<RenderableComponent>(loadedE);
    assert(loadedR != nullptr);
    assert(!loadedR->mesh.IsValid());  // 无 registry 时 handle 留空
    assert(loadedR->visible == true);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] renderable graceful when no AssetRegistry supplied\n");
}

void TestDirectionalLightRoundTrip()
{
    const auto path = MakeTempScenePath("directional_light");

    World source;
    Entity e = source.CreateEntity();

    DirectionalLight light;
    light.direction   = {0.5f, -0.7f, 0.5f};
    light.color       = {1.0f, 0.95f, 0.85f};
    light.intensity   = 2.5f;
    light.castsShadow = true;
    source.AddComponent(e, light);

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    int hits = 0;
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<DirectionalLight>())
    {
        ++hits;
        loadedE = World::FromEntt(ent);
    }
    assert(hits == 1);

    const auto* loadedLight = loaded.GetComponent<DirectionalLight>(loadedE);
    assert(loadedLight != nullptr);
    assert(FloatEq(loadedLight->direction.x, 0.5f));
    assert(FloatEq(loadedLight->direction.y, -0.7f));
    assert(FloatEq(loadedLight->direction.z, 0.5f));
    assert(FloatEq(loadedLight->color.x, 1.0f));
    assert(FloatEq(loadedLight->color.y, 0.95f));
    assert(FloatEq(loadedLight->color.z, 0.85f));
    assert(FloatEq(loadedLight->intensity, 2.5f));
    assert(loadedLight->castsShadow == true);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] directional light round-trip\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[SceneSerializationTest] running\n");
    TestEmptyWorldRoundTrip();
    TestSingleEntityThreeComponentsRoundTrip();
    TestHierarchyChainRoundTrip();
    TestSchemaMismatchRejected();
    TestCorruptJsonRejected();
    TestPartialFailureRollsBack();
    TestMixedKnownAndUnknownComponents();
    TestRenderableRoundTripWithRegistry();
    TestRenderableWithoutRegistryGraceful();
    TestDirectionalLightRoundTrip();
    std::fprintf(stdout, "[SceneSerializationTest] all tests passed.\n");
    return 0;
}
