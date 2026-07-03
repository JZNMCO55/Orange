// CollisionFilterTest —— collision filtering（collider category/mask 位 +
// 查询 QueryFilter）验收。
//
// 覆盖三个维度：
//   1. 碰撞过滤：ColliderComponent.categoryBits/maskBits 让不匹配的 body 互不
//      碰撞（过滤球穿过地面，默认球停在地面）。
//   2. 查询过滤：PhysicsWorld 空间查询的 QueryFilter 尾参按类筛选命中
//      （OverlapAABB 只报指定 category 的 collider）。
//   3. 序列化：category/maskBits 走 scene round-trip 精确保存 + 旧 scene（无
//      这两个键）向后兼容读默认。
//
// headless、裸 <cassert>；main 返回 0 = pass。

#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/core/Serialization.h>

#include "scene/ComponentSerializers.h" // 内部 seam：直接测 ReadColliderDesc 的向后兼容

#include <glm/vec2.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace Phys               = Orange::Engine::Physics;
namespace SceneSerialization = Orange::Engine::Scene;

using Orange::Engine::Entity;
using Orange::Engine::JsonReader;
using Orange::Engine::World;

namespace
{

    // ---- fixture 构件 --------------------------------------------------------

    // static ground box：中心 (0,-5)、halfExtents (50,0.5) → 顶面 y=-4.5。
    // 可指定 category/mask 位。
    Phys::BodyHandle AddGround(Phys::PhysicsWorld& world,
                               std::uint32_t       category = 0x0001u,
                               std::uint32_t       mask     = 0xFFFFFFFFu)
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Static;
        rb.initialPosition = {0.0f, -5.0f};
        Phys::ColliderComponent col;
        col.shape        = Phys::BoxDesc{{50.0f, 0.5f}, {0.0f, 0.0f}};
        col.friction     = 0.5f;
        col.categoryBits = category;
        col.maskBits     = mask;
        return world.AddBody(rb, col);
    }

    // dynamic ball（半径 0.5），可指定初位与 category/mask 位。
    Phys::BodyHandle AddBall(Phys::PhysicsWorld& world,
                             glm::vec2           pos,
                             std::uint32_t       category = 0x0001u,
                             std::uint32_t       mask     = 0xFFFFFFFFu)
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Dynamic;
        rb.initialPosition = pos;
        Phys::ColliderComponent col;
        col.shape        = Phys::CircleDesc{0.5f, {0.0f, 0.0f}};
        col.density      = 1.0f;
        col.restitution  = 0.0f;
        col.categoryBits = category;
        col.maskBits     = mask;
        return world.AddBody(rb, col);
    }

    // static box（可指定中心 / halfExtents / category / mask），用于查询过滤。
    Phys::BodyHandle AddStaticBox(Phys::PhysicsWorld& world,
                                  glm::vec2           center,
                                  glm::vec2           halfExtents,
                                  std::uint32_t       category = 0x0001u,
                                  std::uint32_t       mask     = 0xFFFFFFFFu)
    {
        Phys::RigidBodyComponent rb;
        rb.type            = Phys::BodyType::Static;
        rb.initialPosition = center;
        Phys::ColliderComponent col;
        col.shape        = Phys::BoxDesc{halfExtents, {0.0f, 0.0f}};
        col.categoryBits = category;
        col.maskBits     = mask;
        return world.AddBody(rb, col);
    }

    bool Contains(const std::vector<Phys::BodyHandle>& list, Phys::BodyHandle h)
    {
        for (const auto& e : list)
        {
            if (e.Value() == h.Value())
            {
                return true;
            }
        }
        return false;
    }

    // ---- 子测试 1：碰撞过滤 --------------------------------------------------

    void TestFilteredBodiesDontCollide()
    {
        Phys::PhysicsWorld world; // 默认 gravity (0,-9.81)

        // ground：category=2、mask=all。
        const auto ground = AddGround(world, /*category=*/0x0002u, /*mask=*/0xFFFFFFFFu);
        assert(ground.IsValid());

        // 过滤球：category=1、mask=1（只与 category-1 碰）。ground.cat=2 不在
        // ball.mask=1 里 → 碰撞规则 (ground.cat & ball.mask)==0 → 不碰、穿透。
        // 默认球：category=1、mask=all → 与 ground 正常碰撞、停在顶面。
        // 两球 x 相距 20 米（各半径 0.5）→ 互不接触，排除 ball-ball 干扰。
        const auto filteredBall = AddBall(world, {-10.0f, 0.0f}, /*category=*/0x0001u, /*mask=*/0x0001u);
        const auto defaultBall  = AddBall(world, {10.0f, 0.0f}, /*category=*/0x0001u, /*mask=*/0xFFFFFFFFu);
        assert(filteredBall.IsValid());
        assert(defaultBall.IsValid());

        constexpr int   kSteps = 180;
        constexpr float kDt    = 1.0f / 60.0f;
        for (int i = 0; i < kSteps; ++i)
        {
            world.Step(kDt);
        }

        const float filteredY = world.GetBodyTransform(filteredBall).position.y;
        const float defaultY  = world.GetBodyTransform(defaultBall).position.y;

        if (!(filteredY < -6.0f))
        {
            std::fprintf(stderr,
                         "[CollisionFilterTest] 过滤球未穿透 ground：y=%.3f（期望 < -6）\n",
                         filteredY);
        }
        if (!(defaultY > -4.5f))
        {
            std::fprintf(stderr,
                         "[CollisionFilterTest] 默认球未停在 ground 顶面：y=%.3f（期望 ≈ -4.0）\n",
                         defaultY);
        }

        // 过滤球穿过 ground（顶面 y=-4.5）自由下落到远低于顶面。
        assert(filteredY < -6.0f);
        // 默认球停在 ground 顶面（球心 ≈ -4.0，在顶面 -4.5 之上）。
        assert(defaultY > -4.5f);
        assert(std::fabs(defaultY - (-4.0f)) < 0.2f);
        // 两者行为确实不同（过滤生效的核心断言）。
        assert(defaultY - filteredY > 2.0f);
    }

    // ---- 子测试 2：查询过滤 --------------------------------------------------

    void TestQueryFilterSelectsCategory()
    {
        Phys::PhysicsWorld world;

        // ground：category=2；wall：category=4。均 mask=all。
        const auto ground = AddGround(world, /*category=*/0x0002u, /*mask=*/0xFFFFFFFFu);
        const auto wall   = AddStaticBox(world, {10.0f, 0.0f}, {0.5f, 0.5f},
                                         /*category=*/0x0004u, /*mask=*/0xFFFFFFFFu);
        assert(ground.IsValid());
        assert(wall.IsValid());

        // 覆盖 ground + wall 的大 AABB。
        const glm::vec2 lo{-60.0f, -6.0f};
        const glm::vec2 hi{60.0f, 1.0f};

        // 过滤查询：QueryFilter{cat=all, mask=2} → 命中规则 (query.mask & shape.cat)：
        //   ground.cat=2 & mask=2 → 命中；wall.cat=4 & mask=2 = 0 → 不命中。
        const auto filtered = world.OverlapAABB(lo, hi, Phys::QueryFilter{0xFFFFFFFFu, 0x0002u});
        if (!Contains(filtered, ground) || Contains(filtered, wall))
        {
            std::fprintf(stderr,
                         "[CollisionFilterTest] 查询过滤失败：ground=%d wall=%d（期望 1 / 0）\n",
                         static_cast<int>(Contains(filtered, ground)),
                         static_cast<int>(Contains(filtered, wall)));
        }
        assert(Contains(filtered, ground));
        assert(!Contains(filtered, wall));

        // 默认 QueryFilter（全通过）→ ground 与 wall 都命中。
        const auto all = world.OverlapAABB(lo, hi);
        assert(Contains(all, ground));
        assert(Contains(all, wall));
    }

    // ---- 子测试 3：序列化 round-trip + 向后兼容 ------------------------------

    void TestFilterSerializationRoundTrip()
    {
        // 3a：非默认 category/mask（含高位 0x80000001，验 uint32 全值域精度）
        //     走 scene Save/Load round-trip，断言精确相等。
        auto path = std::filesystem::temp_directory_path();
        path /= "orange_engine_collision_filter_roundtrip.scene.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);

        constexpr std::uint32_t kCategory = 0x00000004u;
        constexpr std::uint32_t kMask     = 0x80000001u; // 高位 + 低位：最易暴露精度丢失

        {
            World                    source;
            Entity                   e = source.CreateEntity();
            Phys::RigidBodyComponent rb;
            rb.type            = Phys::BodyType::Static;
            rb.initialPosition = {1.0f, 2.0f};
            source.AddComponent(e, rb);

            Phys::ColliderComponent col;
            col.shape        = Phys::BoxDesc{{1.0f, 0.5f}, {0.0f, 0.0f}};
            col.categoryBits = kCategory;
            col.maskBits     = kMask;
            source.AddComponent(e, col);

            auto saveResult = SceneSerialization::Save(source, path.string());
            assert(saveResult.IsOk());
        }

        {
            World loaded;
            auto  loadResult = SceneSerialization::Load(path.string(), loaded);
            assert(loadResult.IsOk());

            auto&  reg     = loaded.Registry();
            Entity loadedE = Entity::Invalid();
            for (auto ent : reg.view<Phys::ColliderComponent>())
            {
                loadedE = World::FromEntt(ent);
            }
            assert(loadedE.IsValid());

            const auto* col = loaded.GetComponent<Phys::ColliderComponent>(loadedE);
            assert(col != nullptr);
            if (col->categoryBits != kCategory || col->maskBits != kMask)
            {
                std::fprintf(stderr,
                             "[CollisionFilterTest] round-trip 精度丢失：category=0x%08X mask=0x%08X\n",
                             col->categoryBits, col->maskBits);
            }
            assert(col->categoryBits == kCategory);
            assert(col->maskBits == kMask); // 高位 0x80000001 位精确
        }
        std::filesystem::remove(path, ec);

        // 3b：向后兼容——旧 collider JSON（无 categoryBits/maskBits 键）经
        //     ReadColliderDesc 读出应得默认 category=1 / mask=0xFFFFFFFF。
        //     直接测内部 seam（ReadColliderDesc），不依赖手工搭一整个旧 scene。
        const char* kOldColliderJson = R"({
        "collider": {
            "shape": { "kind": "Circle", "radius": 1.5, "center": [0.1, -0.2] },
            "density": 2.0,
            "friction": 0.4,
            "restitution": 0.1,
            "isSensor": false
        }
    })";

        auto readerResult = JsonReader::FromString(kOldColliderJson);
        assert(readerResult.IsOk());
        const JsonReader& reader = readerResult.Value();

        Phys::ColliderComponent oldCol{};
        // 先污染成非默认，确认 Read 会覆盖（回退默认不是"恰好没动"的假象）。
        oldCol.categoryBits = 0xDEADBEEFu;
        oldCol.maskBits     = 0x00000000u;
        const bool ok       = SceneSerialization::ReadColliderDesc(reader, "collider", oldCol);
        assert(ok);

        if (oldCol.categoryBits != 0x0001u || oldCol.maskBits != 0xFFFFFFFFu)
        {
            std::fprintf(stderr,
                         "[CollisionFilterTest] 向后兼容默认错：category=0x%08X mask=0x%08X"
                         "（期望 0x00000001 / 0xFFFFFFFF）\n",
                         oldCol.categoryBits, oldCol.maskBits);
        }
        assert(oldCol.categoryBits == 0x0001u); // 旧数据无 categoryBits → 默认 1
        assert(oldCol.maskBits == 0xFFFFFFFFu); // 旧数据无 maskBits → 默认 all
        // 其余字段仍正常读出（确保未破坏原路径）。
        assert(std::holds_alternative<Phys::CircleDesc>(oldCol.shape));
        assert(std::fabs(oldCol.density - 2.0f) < 1e-5f);
        assert(oldCol.isSensor == false);
    }

} // namespace

int main()
{
    TestFilteredBodiesDontCollide();
    TestQueryFilterSelectsCategory();
    TestFilterSerializationRoundTrip();
    std::fprintf(stdout, "[CollisionFilterTest] all subtests passed\n");
    return 0;
}
