// Scene 序列化端到端单元测试。
//
// 覆盖：
//   * 空 World round-trip
//   * 单 entity（Transform / Hierarchy / Name 三件套）round-trip
//   * 多 entity + Hierarchy 父子链 round-trip
//   * schemaVersion 缺失 / mismatch → 拒绝读，World 保持原状
//   * 损坏 JSON → 拒绝读，World 保持原状

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/AnimatorRegistry.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
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
using Orange::Engine::Animation::AnimatorComponent;
using Orange::Engine::Animation::AnimatorRegistry;
using Orange::Engine::Animation::IAnimator;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Physics::BodyType;
using Orange::Engine::Physics::BoxDesc;
using Orange::Engine::Physics::CircleDesc;
using Orange::Engine::Physics::ColliderComponent;
using Orange::Engine::Physics::EdgeChainDesc;
using Orange::Engine::Physics::PhysicsWorld;
using Orange::Engine::Physics::PolygonDesc;
using Orange::Engine::Physics::RigidBodyComponent;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::ParticleEmitterComponent;
using Orange::Engine::Render::ParticleEmitterDesc;
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

    auto saveResult = SceneSerialization::Save(
        source, path.string(),
        SceneSerialization::SaveOptions{.assetRegistry = &srcReg});
    assert(saveResult.IsOk());

    // Load 端用一个新的 registry——验证"路径 → 重新 Insert"链路。
    // 引擎内置 mesh loader 需要真磁盘文件；这里走 Insert 提前把同 path
    // 挂进 dst registry，模拟 Load<Mesh>(path) 命中 dedup 直接返回。
    AssetRegistry dstReg;
    auto preloaded = InsertSyntheticMesh(dstReg, "meshes/square.mesh");
    (void)preloaded;

    World loaded;
    auto loadResult = SceneSerialization::Load(
        path.string(), loaded,
        SceneSerialization::LoadOptions{.assetRegistry = &dstReg});
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
    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    // Load 不传 registry → 即使 path 被写空也照常 attach RenderableComponent，
    // 只是 handle 留空。pure-data 字段（visible / castsShadow）不受影响。
    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
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

    // 方向已搬到 entity 的 TransformComponent.rotation —— 测试 round-trip
    // 同时覆盖 (a) DirectionalLight color/intensity/castsShadow 字段 +
    // (b) Transform.rotation 在 Save/Load 内保留。
    World source;
    Entity e = source.CreateEntity();

    const glm::vec3 desiredDir{0.5f, -0.7f, 0.5f};
    TransformComponent xf{};
    xf.rotation = Orange::Engine::Render::
        MakeDirectionalLightRotationFromDir(desiredDir);
    source.AddComponent(e, xf);

    DirectionalLight light;
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
    assert(FloatEq(loadedLight->color.x, 1.0f));
    assert(FloatEq(loadedLight->color.y, 0.95f));
    assert(FloatEq(loadedLight->color.z, 0.85f));
    assert(FloatEq(loadedLight->intensity, 2.5f));
    assert(loadedLight->castsShadow == true);

    // Transform.rotation 保留 + 派生回方向匹配原始（数值精度内）
    const auto* loadedXf = loaded.GetComponent<TransformComponent>(loadedE);
    assert(loadedXf != nullptr);
    const glm::vec3 derived = Orange::Engine::Render::
        ComputeDirectionalLightWorldDir(loadedXf->rotation);
    const glm::vec3 expected = glm::normalize(desiredDir);
    assert(FloatEq(derived.x, expected.x));
    assert(FloatEq(derived.y, expected.y));
    assert(FloatEq(derived.z, expected.z));

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] directional light round-trip\n");
}

// ---------------------------------------------------------------------------
// Backend-dependent components 测试辅助
// ---------------------------------------------------------------------------

// 一个最小 IAnimator 桩 backend——验证 AnimatorRegistry::Create 路径被
// 触发就足够。无需接 DragonBones / ProceduralAnimator 真实现。
class StubAnimator final : public IAnimator
{
public:
    explicit StubAnimator(std::string_view name) : mBackendName{std::string{name}} {}
    void             Tick(float) override {}
    bool             IsFinished() const noexcept override { return false; }
    std::string_view BackendName() const noexcept override { return mBackendName; }
private:
    std::string mBackendName;
};

void TestRigidBodyColliderRoundTripWithPhysicsWorld()
{
    const auto path = MakeTempScenePath("rigidbody_collider");

    World source;
    Entity e = source.CreateEntity();

    RigidBodyComponent rb;
    rb.type             = BodyType::Dynamic;
    rb.initialPosition  = {3.0f, 4.0f};
    rb.initialAngle     = 0.5f;
    rb.linearDamping    = 0.1f;
    rb.angularDamping   = 0.05f;
    rb.fixedRotation    = true;
    rb.gravityScale     = 0.8f;
    source.AddComponent(e, rb);

    ColliderComponent col;
    col.shape       = CircleDesc{2.5f, glm::vec2{0.1f, -0.2f}};
    col.density     = 1.5f;
    col.friction    = 0.4f;
    col.restitution = 0.2f;
    col.isSensor    = false;
    source.AddComponent(e, col);

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    PhysicsWorld pw;
    auto loadResult = SceneSerialization::Load(
        path.string(), loaded,
        SceneSerialization::LoadOptions{.physicsWorld = &pw});
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);
    assert(pw.BodyCount() == 1);

    auto& reg = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<RigidBodyComponent>())
    {
        loadedE = World::FromEntt(ent);
    }
    assert(loadedE.IsValid());

    const auto* loadedRb = loaded.GetComponent<RigidBodyComponent>(loadedE);
    assert(loadedRb != nullptr);
    assert(loadedRb->type == BodyType::Dynamic);
    assert(FloatEq(loadedRb->initialPosition.x, 3.0f));
    assert(FloatEq(loadedRb->initialPosition.y, 4.0f));
    assert(FloatEq(loadedRb->initialAngle, 0.5f));
    assert(FloatEq(loadedRb->linearDamping, 0.1f));
    assert(FloatEq(loadedRb->angularDamping, 0.05f));
    assert(loadedRb->fixedRotation == true);
    assert(FloatEq(loadedRb->gravityScale, 0.8f));
    // PhysicsWorld 提供 → handle 应被反写为有效。
    assert(loadedRb->handle.IsValid());
    assert(pw.IsValid(loadedRb->handle));

    const auto* loadedCol = loaded.GetComponent<ColliderComponent>(loadedE);
    assert(loadedCol != nullptr);
    assert(std::holds_alternative<CircleDesc>(loadedCol->shape));
    const auto& circle = std::get<CircleDesc>(loadedCol->shape);
    assert(FloatEq(circle.radius, 2.5f));
    assert(FloatEq(circle.center.x, 0.1f));
    assert(FloatEq(circle.center.y, -0.2f));
    assert(FloatEq(loadedCol->density, 1.5f));
    assert(FloatEq(loadedCol->friction, 0.4f));
    assert(FloatEq(loadedCol->restitution, 0.2f));
    assert(loadedCol->isSensor == false);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] rigidbody+collider round-trip with PhysicsWorld\n");
}

void TestRigidBodyColliderWithoutPhysicsWorldGraceful()
{
    const auto path = MakeTempScenePath("physics_no_world");

    World source;
    Entity e = source.CreateEntity();
    source.AddComponent(e, RigidBodyComponent{.type = BodyType::Static,
                                              .initialPosition = {1.0f, 2.0f}});
    ColliderComponent col;
    col.shape = BoxDesc{glm::vec2{1.0f, 0.5f}, glm::vec2{0.0f, 0.0f}};
    source.AddComponent(e, col);

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    // 无 PhysicsWorld → desc 仍 attach 但 backend body 不建立。
    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<RigidBodyComponent>())
    {
        loadedE = World::FromEntt(ent);
    }
    assert(loadedE.IsValid());
    const auto* loadedRb = loaded.GetComponent<RigidBodyComponent>(loadedE);
    assert(loadedRb != nullptr);
    assert(loadedRb->type == BodyType::Static);
    assert(!loadedRb->handle.IsValid());  // 无 PhysicsWorld → handle 留空
    const auto* loadedCol = loaded.GetComponent<ColliderComponent>(loadedE);
    assert(loadedCol != nullptr);
    assert(std::holds_alternative<BoxDesc>(loadedCol->shape));

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] rigidbody+collider graceful when no PhysicsWorld\n");
}

void TestPolygonAndEdgeChainShapesRoundTrip()
{
    const auto path = MakeTempScenePath("polygon_edge");

    World source;

    // entity 0：Polygon（3 顶点）
    Entity ePoly = source.CreateEntity();
    source.AddComponent<NameComponent>(ePoly, {"poly"});
    source.AddComponent(ePoly, RigidBodyComponent{.type = BodyType::Static});
    {
        ColliderComponent col;
        PolygonDesc poly;
        poly.count = 3;
        poly.vertices[0] = {0.0f, 0.0f};
        poly.vertices[1] = {1.0f, 0.0f};
        poly.vertices[2] = {0.0f, 1.0f};
        col.shape = poly;
        col.density = 2.0f;
        source.AddComponent(ePoly, col);
    }

    // entity 1：EdgeChain（4 顶点，loop）
    Entity eChain = source.CreateEntity();
    source.AddComponent<NameComponent>(eChain, {"chain"});
    source.AddComponent(eChain, RigidBodyComponent{.type = BodyType::Static});
    {
        ColliderComponent col;
        EdgeChainDesc chain;
        chain.count = 4;
        chain.vertices[0] = {-1.0f, 0.0f};
        chain.vertices[1] = { 1.0f, 0.0f};
        chain.vertices[2] = { 1.0f, 1.0f};
        chain.vertices[3] = {-1.0f, 1.0f};
        chain.isLoop = true;
        col.shape = chain;
        source.AddComponent(eChain, col);
    }

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    PhysicsWorld pw;
    auto loadResult = SceneSerialization::Load(
        path.string(), loaded,
        SceneSerialization::LoadOptions{.physicsWorld = &pw});
    assert(loadResult.IsOk());
    assert(loaded.Size() == 2);

    // 按 Name 反向定位。
    auto& reg = loaded.Registry();
    Entity polyE = Entity::Invalid();
    Entity chainE = Entity::Invalid();
    for (auto ent : reg.view<NameComponent>())
    {
        const auto& nc = reg.get<NameComponent>(ent);
        if      (nc.name == "poly")  polyE  = World::FromEntt(ent);
        else if (nc.name == "chain") chainE = World::FromEntt(ent);
    }
    assert(polyE.IsValid() && chainE.IsValid());

    const auto* polyCol = loaded.GetComponent<ColliderComponent>(polyE);
    assert(polyCol != nullptr);
    assert(std::holds_alternative<PolygonDesc>(polyCol->shape));
    const auto& poly = std::get<PolygonDesc>(polyCol->shape);
    assert(poly.count == 3);
    assert(FloatEq(poly.vertices[2].y, 1.0f));

    const auto* chainCol = loaded.GetComponent<ColliderComponent>(chainE);
    assert(chainCol != nullptr);
    assert(std::holds_alternative<EdgeChainDesc>(chainCol->shape));
    const auto& chain = std::get<EdgeChainDesc>(chainCol->shape);
    assert(chain.count == 4);
    assert(chain.isLoop == true);
    assert(FloatEq(chain.vertices[3].x, -1.0f));

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] polygon + edge-chain shape round-trip\n");
}

void TestAnimatorBackendNameRoundTrip()
{
    const auto path = MakeTempScenePath("animator");

    World source;
    Entity e = source.CreateEntity();
    AnimatorComponent ac;
    ac.animator = std::make_unique<StubAnimator>("test_stub");
    source.AddComponent(e, std::move(ac));

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    // 注册同名 backend factory，让 Load 能 Create 出 IAnimator 实例。
    AnimatorRegistry animReg;
    auto regResult = animReg.RegisterBackend(
        "test_stub", []() -> std::unique_ptr<IAnimator> {
            return std::make_unique<StubAnimator>("test_stub");
        });
    assert(regResult.IsOk());

    World loaded;
    auto loadResult = SceneSerialization::Load(
        path.string(), loaded,
        SceneSerialization::LoadOptions{.animatorRegistry = &animReg});
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<AnimatorComponent>())
    {
        loadedE = World::FromEntt(ent);
    }
    assert(loadedE.IsValid());
    const auto* loadedAc = loaded.GetComponent<AnimatorComponent>(loadedE);
    assert(loadedAc != nullptr);
    assert(loadedAc->animator != nullptr);
    assert(loadedAc->animator->BackendName() == "test_stub");

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] animator backend name round-trip\n");
}

void TestParticleEmitterRoundTrip()
{
    const auto path = MakeTempScenePath("particle_emitter");

    World source;
    Entity e = source.CreateEntity();

    ParticleEmitterDesc desc{};
    desc.emissionRate       = 75.0f;
    desc.lifetimeMin        = 0.4f;
    desc.lifetimeMax        = 1.2f;
    desc.spawnOffsetMin     = {-0.2f, 0.0f};
    desc.spawnOffsetMax     = { 0.2f, 0.05f};
    desc.initialVelocityMin = {-1.0f, 1.5f};
    desc.initialVelocityMax = { 1.0f, 3.5f};
    desc.gravity            = { 0.0f, -4.5f};
    desc.colorStart         = { 1.0f, 0.7f, 0.2f, 2.0f};   // alpha > 1 → bloom
    desc.colorEnd           = { 0.4f, 0.05f, 0.0f, 0.0f};
    desc.sizeStart          = 0.06f;
    desc.sizeEnd            = 0.18f;
    desc.maxParticles       = 192;
    source.AddComponent<ParticleEmitterComponent>(e, {desc, /*emitting=*/false});

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<ParticleEmitterComponent>())
    {
        loadedE = World::FromEntt(ent);
    }
    assert(loadedE.IsValid());
    const auto* pe = loaded.GetComponent<ParticleEmitterComponent>(loadedE);
    assert(pe != nullptr);
    assert(FloatEq(pe->desc.emissionRate, 75.0f));
    assert(FloatEq(pe->desc.lifetimeMin, 0.4f));
    assert(FloatEq(pe->desc.lifetimeMax, 1.2f));
    assert(FloatEq(pe->desc.spawnOffsetMax.y, 0.05f));
    assert(FloatEq(pe->desc.initialVelocityMax.y, 3.5f));
    assert(FloatEq(pe->desc.gravity.y, -4.5f));
    assert(FloatEq(pe->desc.colorStart.a, 2.0f));
    assert(FloatEq(pe->desc.colorEnd.r, 0.4f));
    assert(FloatEq(pe->desc.sizeStart, 0.06f));
    assert(FloatEq(pe->desc.sizeEnd, 0.18f));
    assert(pe->desc.maxParticles == 192);
    assert(pe->emitting == false);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] particle emitter round-trip\n");
}

void TestAnimatorWithoutRegistryGraceful()
{
    const auto path = MakeTempScenePath("animator_no_registry");

    World source;
    Entity e = source.CreateEntity();
    AnimatorComponent ac;
    ac.animator = std::make_unique<StubAnimator>("test_stub");
    source.AddComponent(e, std::move(ac));

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    // 不传 registry → component attach 但 animator unique_ptr 留 nullptr。
    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<AnimatorComponent>())
    {
        loadedE = World::FromEntt(ent);
    }
    assert(loadedE.IsValid());
    const auto* loadedAc = loaded.GetComponent<AnimatorComponent>(loadedE);
    assert(loadedAc != nullptr);
    assert(loadedAc->animator == nullptr);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] animator graceful when no AnimatorRegistry supplied\n");
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
    TestRigidBodyColliderRoundTripWithPhysicsWorld();
    TestRigidBodyColliderWithoutPhysicsWorldGraceful();
    TestPolygonAndEdgeChainShapesRoundTrip();
    TestAnimatorBackendNameRoundTrip();
    TestAnimatorWithoutRegistryGraceful();
    TestParticleEmitterRoundTrip();
    std::fprintf(stdout, "[SceneSerializationTest] all tests passed.\n");
    return 0;
}
