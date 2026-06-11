// Scene 序列化端到端单元测试。
//
// 覆盖：
//   * 空 World round-trip
//   * 单 entity（Transform / Hierarchy / Name 三件套）round-trip
//   * 多 entity + Hierarchy 父子链 round-trip
//   * schemaVersion 缺失 / mismatch → 拒绝读，World 保持原状
//   * 损坏 JSON → 拒绝读，World 保持原状

#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/animation/AnimationClipLoader.h>
#include <orange/engine/animation/AnimationClipSerialization.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/AnimatorRegistry.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/core/Guid.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/script/ScriptComponent.h>

#include <glm/vec4.hpp>

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
using Orange::Engine::Animation::AnimationClip;
using Orange::Engine::Animation::AnimationTrack;
using Orange::Engine::Animation::AnimatorComponent;
using Orange::Engine::Animation::AnimatorRegistry;
using Orange::Engine::Animation::ClipAnimator;
using Orange::Engine::Animation::IAnimator;
using Orange::Engine::Animation::InterpMode;
using Orange::Engine::Animation::Keyframe;
using Orange::Engine::Animation::TrackValueType;
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
using Orange::Engine::Core::Guid;
using Orange::Engine::Scene::GuidComponent;
using Orange::Engine::Scene::HierarchyComponent;
using Orange::Engine::Scene::NameComponent;
using Orange::Engine::Scene::TransformComponent;
using Orange::Engine::Script::ScriptComponent;
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

// "clip" backend 例外（B2.2）：ClipAnimator 的关键帧数据嵌入 scene（形态 B），
// Load 端不靠 registry 而是从 clipJson 重建 + 把 target 重连到 entity 自身的
// TransformComponent。验证 clip 数据 round-trip + target 重连后 tick 真写 transform。
void TestClipAnimatorRoundTrip()
{
    const auto path = MakeTempScenePath("clip_animator");

    World  source;
    Entity e = source.CreateEntity();
    source.AddComponent(e, TransformComponent{});

    AnimationClip clip;
    clip.name     = "scene_clip";
    clip.duration = 1.0f;
    clip.loop     = false;
    AnimationTrack track;
    track.targetName = "position.x";
    track.valueType  = TrackValueType::Float;
    Keyframe k0;
    k0.time  = 0.0f;
    k0.value = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    Keyframe k1;
    k1.time  = 1.0f;
    k1.value = glm::vec4(10.0f, 0.0f, 0.0f, 0.0f);
    track.keys.push_back(k0);
    track.keys.push_back(k1);
    clip.tracks.push_back(track);

    AnimatorComponent ac;
    ac.animator = std::make_unique<ClipAnimator>(clip, source.GetComponent<TransformComponent>(e));
    source.AddComponent(e, std::move(ac));

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    // 关键：Load 不提供 animatorRegistry —— clip backend 完全旁路 registry。
    World loaded;
    auto  loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto&  reg     = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<AnimatorComponent>())
    {
        loadedE = World::FromEntt(ent);
    }
    assert(loadedE.IsValid());

    const auto* loadedAc = loaded.GetComponent<AnimatorComponent>(loadedE);
    assert(loadedAc != nullptr && loadedAc->animator != nullptr);
    assert(loadedAc->animator->BackendName() == "clip");

    auto* loadedClip = static_cast<ClipAnimator*>(loadedAc->animator.get());
    // clip 数据 round-trip。
    assert(loadedClip->Clip().name == "scene_clip");
    assert(loadedClip->Clip().tracks.size() == 1);
    assert(loadedClip->Clip().tracks[0].targetName == "position.x");
    assert(loadedClip->Clip().tracks[0].keys.size() == 2);
    assert(FloatEq(loadedClip->Clip().tracks[0].keys[1].value.x, 10.0f));

    // target 已重连到 loaded entity 自身 Transform。
    const auto* loadedTc = loaded.GetComponent<TransformComponent>(loadedE);
    assert(loadedTc != nullptr);
    assert(loadedClip->GetTarget() == loadedTc && "Load 应把 target 接到 self Transform");

    // tick 半程 → position.x 应为 5（证明 target 真接通、写得进去）。
    loadedClip->Tick(0.5f);
    assert(FloatEq(loaded.GetComponent<TransformComponent>(loadedE)->position.x, 5.0f));

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] ClipAnimator clip+target round-trip（旁路 registry）\n");
}

// Bezier 缓动经完整 scene "clip" backend 持久化管线后端到端保真：clip 创作 → clipJson
// 序列化 → Load 重建 → ClipAnimator 重采样产出**缓动后**的值（非线性中点）。锁住本 session
// 的 InterpMode::Bezier 真时序缓动（切线时间方向 .x）流经 scene 持久化不丢切线、不退化线性。
void TestClipAnimatorBezierRoundTrip()
{
    const auto path = MakeTempScenePath("clip_animator_bezier");

    World  source;
    Entity e = source.CreateEntity();
    source.AddComponent(e, TransformComponent{});

    // ease-in（cubic-bezier(0.42,0,1,1)）：k0 出柄时间方向 0.42、值方向 0；k1 默认柄。
    AnimationClip clip;
    clip.name     = "bezier_clip";
    clip.duration = 1.0f;
    clip.loop     = false;
    AnimationTrack track;
    track.targetName = "position.x";
    track.valueType  = TrackValueType::Float;
    Keyframe k0;
    k0.time       = 0.0f;
    k0.value      = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    k0.interp     = InterpMode::Bezier;
    k0.outTangent = glm::vec2(0.42f, 0.0f);
    Keyframe k1;
    k1.time   = 1.0f;
    k1.value  = glm::vec4(100.0f, 0.0f, 0.0f, 0.0f);
    k1.interp = InterpMode::Bezier;
    track.keys.push_back(k0);
    track.keys.push_back(k1);
    clip.tracks.push_back(track);

    AnimatorComponent ac;
    ac.animator = std::make_unique<ClipAnimator>(clip, source.GetComponent<TransformComponent>(e));
    source.AddComponent(e, std::move(ac));

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    auto  loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());

    auto&  reg     = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<AnimatorComponent>()) { loadedE = World::FromEntt(ent); }
    assert(loadedE.IsValid());

    auto* loadedAc = loaded.GetComponent<AnimatorComponent>(loadedE);
    assert(loadedAc != nullptr && loadedAc->animator != nullptr);
    auto* loadedClip = static_cast<ClipAnimator*>(loadedAc->animator.get());

    // interp + 切线经 clipJson round-trip 保真。
    const auto& lk0 = loadedClip->Clip().tracks[0].keys[0];
    assert(lk0.interp == InterpMode::Bezier && "interp 经 scene 持久化保真");
    assert(FloatEq(lk0.outTangent.x, 0.42f) && "outTangent.x 经 scene 持久化保真");

    // 重采样：tick 半程 → 缓动值（ease-in 慢启动 ≈ 31.5），**显著低于线性中点 50**——
    // 证明 Bezier 时序缓动经整条 scene 持久化管线后未丢切线、未退化为线性插值。
    loadedClip->Tick(0.5f);
    const float midX = loaded.GetComponent<TransformComponent>(loadedE)->position.x;
    assert(midX < 45.0f && "Bezier ease-in：缓动中点显著低于线性 50（经管线未退化线性）");
    assert(midX > 25.0f && midX < 38.0f && "缓动中点落在 ease-in 预期带（≈31.5）");

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] ClipAnimator Bezier 缓动经 scene 持久化端到端保真（mid=%.1f<50）\n",
                 midX);
}

// B2.6 改点 1：ClipAnimator 来自 .anim 资产时，scene 只存 clipSource 引用，Load 端
// 经 assetRegistry 从 .anim 加载 clip + 重连 source path（与 mesh 同款资产引用）。
void TestClipAnimatorAssetSourceRoundTrip()
{
    namespace Anim         = Orange::Engine::Animation;
    const auto  scenePath  = MakeTempScenePath("clip_animator_asset");
    const auto  animPath   = (std::filesystem::temp_directory_path() /
                            "orange_engine_scene_test_src.anim").string();

    // 先落一个 .anim 资产到盘。
    {
        Anim::AnimationClip clip;
        clip.name     = "asset_clip";
        clip.duration = 1.0f;
        Anim::AnimationTrack t;
        t.targetName = "position.x";
        t.valueType  = Anim::TrackValueType::Float;
        Anim::Keyframe k0; k0.time = 0.0f; k0.value = glm::vec4(0, 0, 0, 0);
        Anim::Keyframe k1; k1.time = 1.0f; k1.value = glm::vec4(6, 0, 0, 0);
        t.keys.push_back(k0); t.keys.push_back(k1);
        clip.tracks.push_back(t);
        auto sr = Anim::SaveAnimationClip(clip, animPath);
        assert(sr.IsOk());
    }

    // 源 World：entity 挂 ClipAnimator，clip 来自上面的 .anim（设 source path）。
    World  source;
    Entity e = source.CreateEntity();
    source.AddComponent(e, TransformComponent{});
    {
        Anim::AnimationClip placeholder;  // 内容无关——save 走 clipSource 引用，不嵌 clipJson
        auto up = std::make_unique<ClipAnimator>(placeholder,
                                                 source.GetComponent<TransformComponent>(e));
        up->SetSourceAssetPath(animPath);
        AnimatorComponent ac;
        ac.animator = std::move(up);
        source.AddComponent(e, std::move(ac));
    }

    auto saveRes = SceneSerialization::Save(source, scenePath.string());
    assert(saveRes.IsOk());

    // Load：提供带 AnimationClipLoader 的 AssetRegistry，scene 经 clipSource 加载 clip。
    AssetRegistry reg;
    assert(reg.RegisterLoader<Anim::AnimationClip>(
               std::make_unique<Anim::AnimationClipLoader>()).IsOk());

    World loaded;
    auto  loadRes = SceneSerialization::Load(
        scenePath.string(), loaded,
        SceneSerialization::LoadOptions{.assetRegistry = &reg});
    assert(loadRes.IsOk());

    auto&  lreg    = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : lreg.view<AnimatorComponent>()) { loadedE = World::FromEntt(ent); }
    assert(loadedE.IsValid());
    const auto* lac = loaded.GetComponent<AnimatorComponent>(loadedE);
    assert(lac != nullptr && lac->animator != nullptr &&
           lac->animator->BackendName() == "clip");

    auto* lclip = static_cast<ClipAnimator*>(lac->animator.get());
    // clip 内容来自 .anim 资产。
    assert(lclip->Clip().name == "asset_clip" && "clip 应从 .anim 资产加载");
    assert(lclip->Clip().tracks.size() == 1 &&
           FloatEq(lclip->Clip().tracks[0].keys[1].value.x, 6.0f));
    // source path round-trip。
    assert(lclip->SourceAssetPath() == animPath && "clipSource 路径应 round-trip");
    // target 重连 + tick 真写。
    lclip->Tick(0.5f);
    assert(FloatEq(loaded.GetComponent<TransformComponent>(loadedE)->position.x, 3.0f));

    RemoveIfExists(scenePath);
    std::error_code ec;
    std::filesystem::remove(animPath, ec);
    std::fprintf(stdout, "  [PASS] ClipAnimator .anim 资产引用 round-trip（clipSource）\n");
}

// B2.6 改点 1 降级路径：clipSource 存在但 Load 未提供 assetRegistry —— 不崩，
// animator 仍 attach（空 clip），且 source path 保留（下次 Save 不丢引用）。
void TestClipAnimatorAssetSourceNoRegistryGraceful()
{
    namespace Anim        = Orange::Engine::Animation;
    const auto  scenePath = MakeTempScenePath("clip_animator_noreg");
    const char* srcPath   = "some/unresolved/clip.anim";

    World  source;
    Entity e = source.CreateEntity();
    source.AddComponent(e, TransformComponent{});
    {
        auto up = std::make_unique<ClipAnimator>(Anim::AnimationClip{},
                                                 source.GetComponent<TransformComponent>(e));
        up->SetSourceAssetPath(srcPath);
        AnimatorComponent ac;
        ac.animator = std::move(up);
        source.AddComponent(e, std::move(ac));
    }
    assert(SceneSerialization::Save(source, scenePath.string()).IsOk());

    // Load 不提供 assetRegistry —— clipSource 无法解析，应 graceful。
    World loaded;
    auto  loadRes = SceneSerialization::Load(scenePath.string(), loaded);
    assert(loadRes.IsOk() && "无 registry 也不应整体失败");

    auto&  lreg    = loaded.Registry();
    Entity loadedE = Entity::Invalid();
    for (auto ent : lreg.view<AnimatorComponent>()) { loadedE = World::FromEntt(ent); }
    assert(loadedE.IsValid());
    const auto* lac = loaded.GetComponent<AnimatorComponent>(loadedE);
    assert(lac != nullptr && lac->animator != nullptr &&
           lac->animator->BackendName() == "clip" && "animator 仍 attach");

    auto* lclip = static_cast<ClipAnimator*>(lac->animator.get());
    assert(lclip->Clip().tracks.empty() && "无 registry → 空 clip（不崩）");
    assert(lclip->SourceAssetPath() == srcPath && "source path 保留，下次 Save 不丢引用");

    RemoveIfExists(scenePath);
    std::fprintf(stdout, "  [PASS] ClipAnimator clipSource 无 registry → graceful（空 clip + 保留引用）\n");
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

// 验证 Save → Load → Save 字节稳定（GAP-2026-05-23-editor-play-stop-entity-
// tree-order-reversed 回归测试）。
//
// 没有这条 fix 之前，EnTT view<entt::entity>() 在 packed array 上的 LIFO
// 迭代会让 Source World 写出的 entity 数组顺序与 Loaded World 重新写出的
// 顺序完全反转——纯无业务变动的污染 diff。
//
// 这里构造 8 个带 Name 的 entity，跑 Save → Load → Save，比对两次产出的
// JSON 字节是否完全一致。
void TestSaveLoadSaveByteStable()
{
    const auto path1 = MakeTempScenePath("byte_stable_1");
    const auto path2 = MakeTempScenePath("byte_stable_2");

    World source;
    for (int i = 0; i < 8; ++i)
    {
        Entity e = source.CreateEntity();
        NameComponent nm;
        nm.name = std::string{"entity_"} + std::to_string(i);
        source.AddComponent(e, nm);
    }

    auto save1 = SceneSerialization::Save(source, path1.string());
    assert(save1.IsOk());

    World loaded;
    auto load1 = SceneSerialization::Load(path1.string(), loaded);
    assert(load1.IsOk());
    assert(loaded.Size() == 8);

    auto save2 = SceneSerialization::Save(loaded, path2.string());
    assert(save2.IsOk());

    // 字节级比对两份 JSON。
    auto readAll = [](const std::filesystem::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    };
    const std::string a = readAll(path1);
    const std::string b = readAll(path2);
    assert(!a.empty());
    assert(a == b);

    RemoveIfExists(path1);
    RemoveIfExists(path2);
    std::fprintf(stdout, "  [PASS] save-load-save byte stable (regression for "
                         "entity tree order reversal)\n");
}

// ScriptComponent（ADR-017 B1.2）round-trip —— 不依赖 CLR：纯字符串字段
// assemblyPath / typeName 经 Save → Load 保真。验证含脚本组件的场景在不开
// dotnet 的构建里也能 round-trip（本测试始终编译，不门控）。
void TestScriptComponentRoundTrip()
{
    const auto path = MakeTempScenePath("script_component");

    World source;
    Entity e = source.CreateEntity();
    source.AddComponent<NameComponent>(e, {"scripted"});
    ScriptComponent sc;
    sc.assemblyPath = "assets/scripts/Game.dll";
    sc.typeName     = "Game.Patrol, Game";
    // B1.3 fieldOverrides round-trip：3 条不同类型，验证 name/type/value 保真。
    using Orange::Engine::Script::ScriptFieldType;
    sc.fieldOverrides.push_back({"Speed",   ScriptFieldType::Float,  "2.5"});
    sc.fieldOverrides.push_back({"MaxHits", ScriptFieldType::Int,    "3"});
    sc.fieldOverrides.push_back({"Looping", ScriptFieldType::Bool,   "true"});
    sc.fieldOverrides.push_back({"Tag",     ScriptFieldType::String, "patrol-A"});
    source.AddComponent(e, sc);

    auto saveResult = SceneSerialization::Save(source, path.string());
    assert(saveResult.IsOk());

    World loaded;
    auto loadResult = SceneSerialization::Load(path.string(), loaded);
    assert(loadResult.IsOk());
    assert(loaded.Size() == 1);

    auto& reg = loaded.Registry();
    int hits = 0;
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<ScriptComponent>())
    {
        ++hits;
        loadedE = World::FromEntt(ent);
    }
    assert(hits == 1);

    const auto* loadedSc = loaded.GetComponent<ScriptComponent>(loadedE);
    assert(loadedSc != nullptr);
    assert(loadedSc->assemblyPath == "assets/scripts/Game.dll");
    assert(loadedSc->typeName == "Game.Patrol, Game");

    // fieldOverrides 数量 + 每条 name/type/value 完全一致。
    assert(loadedSc->fieldOverrides.size() == 4);
    assert(loadedSc->fieldOverrides[0].name == "Speed");
    assert(loadedSc->fieldOverrides[0].type == ScriptFieldType::Float);
    assert(loadedSc->fieldOverrides[0].value == "2.5");
    assert(loadedSc->fieldOverrides[1].name == "MaxHits");
    assert(loadedSc->fieldOverrides[1].type == ScriptFieldType::Int);
    assert(loadedSc->fieldOverrides[1].value == "3");
    assert(loadedSc->fieldOverrides[2].name == "Looping");
    assert(loadedSc->fieldOverrides[2].type == ScriptFieldType::Bool);
    assert(loadedSc->fieldOverrides[2].value == "true");
    assert(loadedSc->fieldOverrides[3].name == "Tag");
    assert(loadedSc->fieldOverrides[3].type == ScriptFieldType::String);
    assert(loadedSc->fieldOverrides[3].value == "patrol-A");

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] script component round-trip (no CLR; +fieldOverrides)\n");
}

// 旧版本场景向后兼容：一个 scene/world 1.0 文件（不含 Script 段）应正常读，
// 实体不挂 ScriptComponent，其余组件照常装回——additive schema 的核心承诺。
void TestOldSceneWithoutScriptComponentLoads()
{
    const auto path = MakeTempScenePath("script_backward_compat");

    {
        std::ofstream out(path);
        out <<
            R"({
              "schemaVersion": { "namespace": "scene/world", "major": 1, "minor": 0 },
              "entities": [
                {
                  "id": 0,
                  "components": {
                    "Name": { "name": "legacy_entity" }
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
    Entity loadedE = Entity::Invalid();
    for (auto ent : reg.view<NameComponent>())
    {
        loadedE = World::FromEntt(ent);
    }
    assert(loadedE.IsValid());
    assert(loaded.GetComponent<NameComponent>(loadedE)->name == "legacy_entity");
    // 旧文件无 Script 段 → 实体不挂 ScriptComponent。
    assert(loaded.GetComponent<ScriptComponent>(loadedE) == nullptr);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] old scene without Script component loads (backward compat)\n");
}

// ---------------------------------------------------------------------------
// A2 EntityGuid 持久身份主键迁移（ADR-018，选项 B 双键过渡）
// ---------------------------------------------------------------------------

// 小工具：按 Name 在 loaded world 里反查 entity。
Entity FindByName(const World& w, const char* name)
{
    auto& reg = const_cast<World&>(w).Registry();
    for (auto ent : reg.view<NameComponent>())
    {
        if (reg.get<NameComponent>(ent).name == name)
        {
            return World::FromEntt(ent);
        }
    }
    return Entity::Invalid();
}

// 读整文件字节。
std::string ReadFileBytes(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// 构造一棵 root → [a, b, c] 的小层级，全员带 Name + Hierarchy。
void BuildHierarchyFixture(World& w, Entity& root, Entity& a, Entity& b, Entity& c)
{
    root = w.CreateEntity();
    a    = w.CreateEntity();
    b    = w.CreateEntity();
    c    = w.CreateEntity();
    w.AddComponent<NameComponent>(root, {"root"});
    w.AddComponent<NameComponent>(a,    {"a"});
    w.AddComponent<NameComponent>(b,    {"b"});
    w.AddComponent<NameComponent>(c,    {"c"});
    w.AddComponent<HierarchyComponent>(root, {});
    w.AddComponent<HierarchyComponent>(a, {});
    w.AddComponent<HierarchyComponent>(b, {});
    w.AddComponent<HierarchyComponent>(c, {});
    auto* rootH = w.GetComponent<HierarchyComponent>(root);
    auto* aH    = w.GetComponent<HierarchyComponent>(a);
    auto* bH    = w.GetComponent<HierarchyComponent>(b);
    auto* cH    = w.GetComponent<HierarchyComponent>(c);
    rootH->firstChild = a;
    aH->parent = root;  aH->nextSibling = b;
    bH->parent = root;  bH->prevSibling = a;  bH->nextSibling = c;
    cH->parent = root;  cH->prevSibling = b;
}

// 验证 loaded world 的 root→[a,b,c] 拓扑正确（无论经 guid 还是 int 解析）。
void AssertHierarchyTopology(const World& w)
{
    const Entity lroot = FindByName(w, "root");
    const Entity la    = FindByName(w, "a");
    const Entity lb    = FindByName(w, "b");
    const Entity lc    = FindByName(w, "c");
    assert(lroot.IsValid() && la.IsValid() && lb.IsValid() && lc.IsValid());
    const auto* rh = w.GetComponent<HierarchyComponent>(lroot);
    const auto* ah = w.GetComponent<HierarchyComponent>(la);
    const auto* bh = w.GetComponent<HierarchyComponent>(lb);
    const auto* ch = w.GetComponent<HierarchyComponent>(lc);
    assert(rh && ah && bh && ch);
    assert(rh->firstChild == la);
    assert(ah->parent == lroot && ah->nextSibling == lb);
    assert(bh->parent == lroot && bh->prevSibling == la && bh->nextSibling == lc);
    assert(ch->parent == lroot && ch->prevSibling == lb);
}

// S1：SaveOptions.ensureGuids（默认开）—— 非 const Save 路径在 Save 前普遍补 guid。
// 补 guid → Save → Load → 每 entity 有 guid 且与 Save 前一致。
void TestSaveEnsuresGuids()
{
    const auto path = MakeTempScenePath("s1_ensure_guids");

    World source;
    Entity e0 = source.CreateEntity();
    Entity e1 = source.CreateEntity();
    source.AddComponent<NameComponent>(e0, {"n0"});
    source.AddComponent<NameComponent>(e1, {"n1"});
    // Save 前**没有**任何 GuidComponent。
    assert(source.GetComponent<GuidComponent>(e0) == nullptr);
    assert(source.GetComponent<GuidComponent>(e1) == nullptr);

    // 非 const Save（默认 ensureGuids=true）→ Save 前普遍补 guid（mutate source）。
    auto sv = SceneSerialization::Save(source, path.string());
    assert(sv.IsOk());
    // Save 后 source 上的实体已被补 guid（EnsureEntityGuids 的副作用）。
    const auto* sg0 = source.GetComponent<GuidComponent>(e0);
    const auto* sg1 = source.GetComponent<GuidComponent>(e1);
    assert(sg0 != nullptr && sg0->guid.IsValid());
    assert(sg1 != nullptr && sg1->guid.IsValid());
    const Guid savedG0 = sg0->guid;
    const Guid savedG1 = sg1->guid;

    // Load → 每 entity 有 guid 且与 Save 前一致。
    World loaded;
    auto lv = SceneSerialization::Load(path.string(), loaded);
    assert(lv.IsOk());
    assert(loaded.Size() == 2);
    const Entity ln0 = FindByName(loaded, "n0");
    const Entity ln1 = FindByName(loaded, "n1");
    assert(ln0.IsValid() && ln1.IsValid());
    const auto* lg0 = loaded.GetComponent<GuidComponent>(ln0);
    const auto* lg1 = loaded.GetComponent<GuidComponent>(ln1);
    assert(lg0 != nullptr && lg0->guid == savedG0);
    assert(lg1 != nullptr && lg1->guid == savedG1);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] S1: Save(World&) ensures guids universally + round-trips\n");
}

// S1：ensureGuids=false → 非 const Save 不补 guid（行为与历史一致）。
void TestSaveEnsureGuidsFalseNoOp()
{
    const auto path = MakeTempScenePath("s1_ensure_false");

    World source;
    Entity e = source.CreateEntity();
    source.AddComponent<NameComponent>(e, {"n"});

    SceneSerialization::SaveOptions opt;
    opt.ensureGuids = false;
    auto sv = SceneSerialization::Save(source, path.string(), opt);
    assert(sv.IsOk());
    // 关：不补 guid。
    assert(source.GetComponent<GuidComponent>(e) == nullptr);

    World loaded;
    assert(SceneSerialization::Load(path.string(), loaded).IsOk());
    assert(loaded.Size() == 1);
    const Entity ln = FindByName(loaded, "n");
    assert(ln.IsValid());
    assert(loaded.GetComponent<GuidComponent>(ln) == nullptr);  // 无 guid 段

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] S1: ensureGuids=false leaves world without guids\n");
}

// S1：const Save 入口物理上不补 guid（ensureGuids 被忽略）。
void TestConstSaveDoesNotEnsureGuids()
{
    const auto path = MakeTempScenePath("s1_const_save");

    World source;
    Entity e = source.CreateEntity();
    source.AddComponent<NameComponent>(e, {"n"});

    // 经 const 引用调 Save —— 选到 const 重载，不 mutate、不补 guid。
    const World& cref = source;
    auto sv = SceneSerialization::Save(cref, path.string());  // 默认 ensureGuids=true，但 const 入口忽略
    assert(sv.IsOk());
    assert(source.GetComponent<GuidComponent>(e) == nullptr);  // 未被补 guid

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] S1: const Save ignores ensureGuids (no mutation)\n");
}

// S3：scene 级 guid 不变性 —— Save→Load guid 逐 entity 稳定 + EnsureEntityGuids 幂等 +
// ReassignEntityGuids 后旧 guid 不复现于序列化产物。
void TestGuidInvariantsSceneLevel()
{
    const auto path = MakeTempScenePath("s3_guid_invariants");

    World source;
    Entity e0 = source.CreateEntity();
    Entity e1 = source.CreateEntity();
    source.AddComponent<NameComponent>(e0, {"i0"});
    source.AddComponent<NameComponent>(e1, {"i1"});

    // 显式补 guid（非 Save 副作用），记下。
    assert(SceneSerialization::EnsureEntityGuids(source) == 2);
    // 幂等：再 Ensure 返回 0。
    assert(SceneSerialization::EnsureEntityGuids(source) == 0);
    const Guid g0 = source.GetComponent<GuidComponent>(e0)->guid;
    const Guid g1 = source.GetComponent<GuidComponent>(e1)->guid;

    // Save→Load guid 逐 entity 稳定。
    assert(SceneSerialization::Save(source, path.string()).IsOk());
    World loaded;
    assert(SceneSerialization::Load(path.string(), loaded).IsOk());
    assert(loaded.GetComponent<GuidComponent>(FindByName(loaded, "i0"))->guid == g0);
    assert(loaded.GetComponent<GuidComponent>(FindByName(loaded, "i1"))->guid == g1);

    // ReassignEntityGuids(e0) 后旧 guid 不复现：再 Save 的文件里不含旧 g0 字符串。
    const std::vector<Entity> targets{e0};
    SceneSerialization::ReassignEntityGuids(source, targets);
    assert(source.GetComponent<GuidComponent>(e0)->guid != g0);
    const auto path2 = MakeTempScenePath("s3_reassigned");
    assert(SceneSerialization::Save(source, path2.string()).IsOk());
    const std::string bytes = ReadFileBytes(path2);
    assert(bytes.find(g0.ToString()) == std::string::npos &&
           "Reassign 后旧 guid 不应再出现在序列化产物里");
    assert(bytes.find(g1.ToString()) != std::string::npos && "未 Reassign 的 guid 仍在");

    RemoveIfExists(path);
    RemoveIfExists(path2);
    std::fprintf(stdout, "  [PASS] S3: scene-level guid invariants (stable / idempotent / reassign)\n");
}

// A2.1 ①新写：Save（有 guid）→ Load → 父子拓扑经 guid 解析正确。
// 间接验 guid 路径：Save 前补 guid，文件里 *Guid 字段非空；Load 时若 guid 索引
// 命中即走 guid（顺序 int 仍在但作回退）。拓扑正确即证明双键解析无误。
void TestHierarchyGuidKeyNewWrite()
{
    const auto path = MakeTempScenePath("a21_new_write");

    World source;
    Entity root, a, b, c;
    BuildHierarchyFixture(source, root, a, b, c);

    // 非 const Save 默认补 guid → 写出 *Guid 字段。
    assert(SceneSerialization::Save(source, path.string()).IsOk());

    // 文件应含 parentGuid 字段名（schema 1.16 增写）+ 实体 guid 字符串。
    const std::string bytes = ReadFileBytes(path);
    assert(bytes.find("parentGuid") != std::string::npos && "1.16 应增写 parentGuid 字段");
    assert(bytes.find("firstChildGuid") != std::string::npos);
    // a 的 guid 应作为 root.firstChildGuid 出现。
    const Guid ga = source.GetComponent<GuidComponent>(a)->guid;
    assert(bytes.find(ga.ToString()) != std::string::npos);

    World loaded;
    assert(SceneSerialization::Load(path.string(), loaded).IsOk());
    assert(loaded.Size() == 4);
    AssertHierarchyTopology(loaded);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] A2.1 ①: hierarchy guid-key new-write round-trip\n");
}

// A2.1 ②旧读：手造旧版纯 int scene（schema 1.15，无 *Guid 字段）→ Load → 走 int
// 回退，拓扑正确。锁住向后兼容（选项 B 核心卖点）。
void TestHierarchyOldPureIntScene()
{
    const auto path = MakeTempScenePath("a21_old_int");

    {
        std::ofstream out(path);
        out <<
            R"({
              "schemaVersion": { "namespace": "scene/world", "major": 1, "minor": 15 },
              "entities": [
                { "id": 0, "components": {
                    "Name": { "name": "root" },
                    "Hierarchy": { "parent": -1, "firstChild": 1, "nextSibling": -1, "prevSibling": -1 } } },
                { "id": 1, "components": {
                    "Name": { "name": "a" },
                    "Hierarchy": { "parent": 0, "firstChild": -1, "nextSibling": 2, "prevSibling": -1 } } },
                { "id": 2, "components": {
                    "Name": { "name": "b" },
                    "Hierarchy": { "parent": 0, "firstChild": -1, "nextSibling": 3, "prevSibling": 1 } } },
                { "id": 3, "components": {
                    "Name": { "name": "c" },
                    "Hierarchy": { "parent": 0, "firstChild": -1, "nextSibling": -1, "prevSibling": 2 } } }
              ]
            })";
    }

    World loaded;
    auto lv = SceneSerialization::Load(path.string(), loaded);
    assert(lv.IsOk() && "旧 1.15 纯 int 文件必须仍能 Load（向后兼容）");
    assert(loaded.Size() == 4);
    AssertHierarchyTopology(loaded);
    // 旧文件无 Guid 段 → 实体不挂 GuidComponent。
    assert(loaded.GetComponent<GuidComponent>(FindByName(loaded, "root")) == nullptr);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] A2.1 ②: old pure-int scene (no *Guid) loads via int fallback\n");
}

// A2.1 ③混合：部分实体有 guid、部分缺。被引用实体无 guid 时该链接经 int 回退；
// 有 guid 的链接经 guid 解析。两条路径都得拓扑正确。
void TestHierarchyMixedGuidAndInt()
{
    const auto path = MakeTempScenePath("a21_mixed");

    World source;
    Entity root, a, b, c;
    BuildHierarchyFixture(source, root, a, b, c);
    // 只给 root 和 a 补 guid（b / c 无 guid）。
    const std::vector<Entity> withGuid{root, a};
    SceneSerialization::ReassignEntityGuids(source, withGuid);  // 给这两个分配 guid
    // 确认 b / c 仍无 guid。
    assert(source.GetComponent<GuidComponent>(b) == nullptr);
    assert(source.GetComponent<GuidComponent>(c) == nullptr);

    // const Save：不补 guid（保留"部分缺"的混合态）。ensureGuids 对 const 入口无效，
    // 但这里显式用 const 引用，绝不补。
    const World& cref = source;
    assert(SceneSerialization::Save(cref, path.string()).IsOk());

    const std::string bytes = ReadFileBytes(path);
    // root / a 的 guid 应出现（firstChildGuid = a.guid，a.parentGuid = root.guid）。
    assert(bytes.find(source.GetComponent<GuidComponent>(a)->guid.ToString()) != std::string::npos);

    World loaded;
    assert(SceneSerialization::Load(path.string(), loaded).IsOk());
    assert(loaded.Size() == 4);
    // 混合解析后拓扑仍正确：root.firstChild→a（guid 路径），a.nextSibling→b（b 无
    // guid，a 写出的 nextSiblingGuid 为空串 → 回退 int），b.nextSibling→c（int），等。
    AssertHierarchyTopology(loaded);

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] A2.1 ③: mixed guid/int hierarchy resolves correctly\n");
}

// A2.1 ④字节稳定：guid 普遍后 Save→Load→Save 字节一致（顺序排序前提不破）。
void TestHierarchyGuidByteStable()
{
    const auto path1 = MakeTempScenePath("a21_byte1");
    const auto path2 = MakeTempScenePath("a21_byte2");

    World source;
    Entity root, a, b, c;
    BuildHierarchyFixture(source, root, a, b, c);

    // 第一次 Save（非 const，补 guid）。
    assert(SceneSerialization::Save(source, path1.string()).IsOk());

    World loaded;
    assert(SceneSerialization::Load(path1.string(), loaded).IsOk());
    assert(loaded.Size() == 4);

    // 第二次 Save（非 const，guid 已普遍存在 → Ensure 幂等不新增）。
    assert(SceneSerialization::Save(loaded, path2.string()).IsOk());

    const std::string a1 = ReadFileBytes(path1);
    const std::string a2 = ReadFileBytes(path2);
    assert(!a1.empty());
    assert(a1 == a2 && "guid 普遍后 Save→Load→Save 字节稳定（含 *Guid 字段）");

    RemoveIfExists(path1);
    RemoveIfExists(path2);
    std::fprintf(stdout, "  [PASS] A2.1 ④: hierarchy guid round-trip byte-stable\n");
}

// A2.1 加固：guid 主键真生效——构造一个"顺序 int 故意指向错误实体、但 guid 指向
// 正确实体"的文件，验证读端**优先 guid**（拓扑按 guid 而非 int）。这是双键里"guid
// 当主键"的判定性证据（否则无法区分 guid 路径是否被真正走到）。
void TestHierarchyGuidWinsOverWrongInt()
{
    const auto path = MakeTempScenePath("a21_guid_wins");

    // 两个实体 P（parent）和 K（child）。给定 guid。K.parent 的顺序 int 故意写成
    // 一个**不存在**的 id（99）→ 若读端走 int 必得 Invalid parent；但 parentGuid
    // 写 P 的 guid → 走 guid 应得 P。
    const Guid gP{0x1111111111111111ull, 0x2222222222222222ull};
    const Guid gK{0x3333333333333333ull, 0x4444444444444444ull};
    {
        std::ofstream out(path);
        out <<
            R"({
              "schemaVersion": { "namespace": "scene/world", "major": 1, "minor": 16 },
              "entities": [
                { "id": 0, "components": {
                    "Name": { "name": "P" },
                    "Guid": { "value": ")" << gP.ToString() << R"(" },
                    "Hierarchy": { "parent": -1, "firstChild": 1, "firstChildGuid": ")" << gK.ToString() << R"(",
                                   "nextSibling": -1, "prevSibling": -1, "parentGuid": "" } } },
                { "id": 1, "components": {
                    "Name": { "name": "K" },
                    "Guid": { "value": ")" << gK.ToString() << R"(" },
                    "Hierarchy": { "parent": 99, "parentGuid": ")" << gP.ToString() << R"(",
                                   "firstChild": -1, "nextSibling": -1, "prevSibling": -1 } } }
              ]
            })";
    }

    World loaded;
    auto lv = SceneSerialization::Load(path.string(), loaded);
    assert(lv.IsOk());
    assert(loaded.Size() == 2);

    const Entity lp = FindByName(loaded, "P");
    const Entity lk = FindByName(loaded, "K");
    assert(lp.IsValid() && lk.IsValid());
    const auto* kh = loaded.GetComponent<HierarchyComponent>(lk);
    assert(kh != nullptr);
    // 关键断言：parent 经 guid 解析为 P（而非 int=99 的 Invalid）。
    assert(kh->parent == lp && "读端应优先 guid（int 故意指向不存在的 id 99）");
    const auto* ph = loaded.GetComponent<HierarchyComponent>(lp);
    assert(ph != nullptr && ph->firstChild == lk && "firstChild 经 guid 解析");

    RemoveIfExists(path);
    std::fprintf(stdout, "  [PASS] A2.1: guid key wins over (deliberately wrong) int\n");
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
    TestClipAnimatorRoundTrip();
    TestClipAnimatorBezierRoundTrip();
    TestClipAnimatorAssetSourceRoundTrip();
    TestClipAnimatorAssetSourceNoRegistryGraceful();
    TestParticleEmitterRoundTrip();
    TestSaveLoadSaveByteStable();
    TestScriptComponentRoundTrip();
    TestOldSceneWithoutScriptComponentLoads();
    // A2 EntityGuid 持久身份主键迁移（ADR-018）
    TestSaveEnsuresGuids();
    TestSaveEnsureGuidsFalseNoOp();
    TestConstSaveDoesNotEnsureGuids();
    TestGuidInvariantsSceneLevel();
    TestHierarchyGuidKeyNewWrite();
    TestHierarchyOldPureIntScene();
    TestHierarchyMixedGuidAndInt();
    TestHierarchyGuidByteStable();
    TestHierarchyGuidWinsOverWrongInt();
    std::fprintf(stdout, "[SceneSerializationTest] all tests passed.\n");
    return 0;
}
