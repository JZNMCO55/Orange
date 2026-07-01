// NavigationTest —— 2D 网格导航子系统（NavGrid + A* 寻路 + LOS 平滑 + 物理
// 烘焙）headless 验收。裸 <cassert> + 独立 main()，进程 exit 0 = pass。
//
// 子测试：
//   * NavGrid helper：MakeNavGrid / WorldToCell↔CellCenterToWorld 一致性 /
//     InBounds / SetWalkable / IsWalkable
//   * FindPath：直线 / 绕障 / 无路 / 退化输入 / 对角 vs 4-邻接 + 禁切角
//   * SimplifyPath：全可走化简 / 有障碍化简后不穿墙
//   * BakeWalkableFromPhysics：static box 覆盖区标阻挡、远处可走

#include "orange/engine/nav/NavGrid.h"
#include "orange/engine/nav/NavGridBake.h"
#include "orange/engine/nav/Pathfinding.h"
#include "orange/engine/physics/PhysicsWorld.h"

#include <glm/vec2.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Nav  = Orange::Engine::Nav;
namespace Phys = Orange::Engine::Physics;

namespace
{

bool Near(float a, float b, float tol = 1e-4f)
{
    return std::fabs(a - b) <= tol;
}

bool NearV2(glm::vec2 a, glm::vec2 b, float tol = 1e-4f)
{
    return Near(a.x, b.x, tol) && Near(a.y, b.y, tol);
}

// 测试本地的 LOS 采样（不依赖 file-local static HasLineOfSight）：沿 a→b 稠密
// 采样，每点 WorldToCell 必须可走。用于"化简后不穿墙"断言。
bool SegmentWalkable(const Nav::NavGrid& grid, glm::vec2 a, glm::vec2 b)
{
    const glm::vec2 delta = b - a;
    const float     dist  = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    const int       samples =
        std::max(1, static_cast<int>(std::ceil(dist / (grid.cellSize * 0.5f))));
    for (int i = 0; i <= samples; ++i)
    {
        const float      t = static_cast<float>(i) / static_cast<float>(samples);
        const glm::vec2  p = a + delta * t;
        const glm::ivec2 c = Nav::WorldToCell(grid, p);
        if (!Nav::IsWalkable(grid, c.x, c.y))
        {
            return false;
        }
    }
    return true;
}

// ---- NavGrid helper ------------------------------------------------------

void TestNavGridHelpers()
{
    Nav::NavGrid grid = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});
    assert(grid.width == 10);
    assert(grid.height == 10);
    assert(grid.walkable.size() == 100);

    // 全可走。
    for (int y = 0; y < 10; ++y)
    {
        for (int x = 0; x < 10; ++x)
        {
            assert(Nav::IsWalkable(grid, x, y));
        }
    }

    // WorldToCell ↔ CellCenterToWorld 一致性：cell 中心转回同 cell。
    for (int y = 0; y < 10; ++y)
    {
        for (int x = 0; x < 10; ++x)
        {
            const glm::vec2  center = Nav::CellCenterToWorld(grid, x, y);
            const glm::ivec2 back   = Nav::WorldToCell(grid, center);
            assert(back.x == x && back.y == y);
        }
    }

    // InBounds 越界。
    assert(Nav::InBounds(grid, 0, 0));
    assert(Nav::InBounds(grid, 9, 9));
    assert(!Nav::InBounds(grid, -1, 0));
    assert(!Nav::InBounds(grid, 0, -1));
    assert(!Nav::InBounds(grid, 10, 0));
    assert(!Nav::InBounds(grid, 0, 10));

    // 越界 IsWalkable → false；越界 SetWalkable → no-op（不崩）。
    assert(!Nav::IsWalkable(grid, -1, -1));
    assert(!Nav::IsWalkable(grid, 10, 10));
    Nav::SetWalkable(grid, 100, 100, false);  // no-op

    // SetWalkable / IsWalkable 往返。
    Nav::SetWalkable(grid, 5, 5, false);
    assert(!Nav::IsWalkable(grid, 5, 5));
    Nav::SetWalkable(grid, 5, 5, true);
    assert(Nav::IsWalkable(grid, 5, 5));

    // 非原点 origin + 非 1 cellSize：换算仍自洽。
    Nav::NavGrid grid2 = Nav::MakeNavGrid(4, 4, 2.0f, {-3.0f, 7.0f});
    const glm::vec2  c   = Nav::CellCenterToWorld(grid2, 2, 1);   // origin + (2.5,1.5)*2
    assert(NearV2(c, {-3.0f + 5.0f, 7.0f + 3.0f}));
    assert(Nav::WorldToCell(grid2, c) == glm::ivec2(2, 1));

    // CellBoundsWorld：cell(2,1) 世界 AABB。
    glm::vec2 lo{0.0f, 0.0f};
    glm::vec2 hi{0.0f, 0.0f};
    Nav::CellBoundsWorld(grid2, 2, 1, lo, hi);
    assert(NearV2(lo, {-3.0f + 4.0f, 7.0f + 2.0f}));   // origin + (2,1)*2
    assert(NearV2(hi, {-3.0f + 6.0f, 7.0f + 4.0f}));   // origin + (3,2)*2

    // 空网格：width/height <= 0 → 空。
    Nav::NavGrid empty = Nav::MakeNavGrid(0, 5, 1.0f, {0.0f, 0.0f});
    assert(empty.width == 0);
    assert(empty.walkable.empty());
}

// ---- FindPath 直线 -------------------------------------------------------

void TestFindPathStraight()
{
    Nav::NavGrid grid = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});

    const glm::vec2 start = Nav::CellCenterToWorld(grid, 1, 1);
    const glm::vec2 goal  = Nav::CellCenterToWorld(grid, 8, 8);

    const std::vector<glm::vec2> path = Nav::FindPath(grid, start, goal, true);
    assert(!path.empty());

    // 首末路点在起终 cell 中心。
    assert(NearV2(path.front(), Nav::CellCenterToWorld(grid, 1, 1)));
    assert(NearV2(path.back(), Nav::CellCenterToWorld(grid, 8, 8)));

    // 每路点可走；相邻路点 chebyshev 距离 ≤ 1 cell（合法 8-邻接步）。
    for (std::size_t i = 0; i < path.size(); ++i)
    {
        const glm::ivec2 c = Nav::WorldToCell(grid, path[i]);
        assert(Nav::IsWalkable(grid, c.x, c.y));
        if (i > 0)
        {
            const glm::ivec2 prev = Nav::WorldToCell(grid, path[i - 1]);
            const int        ddx  = std::abs(c.x - prev.x);
            const int        ddy  = std::abs(c.y - prev.y);
            assert(ddx <= 1 && ddy <= 1);
            assert(ddx + ddy > 0);  // 不原地踏步
        }
    }
}

// ---- FindPath 绕障 -------------------------------------------------------

void TestFindPathAroundWall()
{
    Nav::NavGrid grid = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});

    // 竖墙：x=5 列 y=0..8 全阻挡，仅 y=9 留缺口。
    for (int y = 0; y <= 8; ++y)
    {
        Nav::SetWalkable(grid, 5, y, false);
    }

    const glm::vec2 start = Nav::CellCenterToWorld(grid, 1, 1);  // 墙左
    const glm::vec2 goal  = Nav::CellCenterToWorld(grid, 8, 1);  // 墙右

    const std::vector<glm::vec2> path = Nav::FindPath(grid, start, goal, true);
    assert(!path.empty());
    assert(NearV2(path.back(), Nav::CellCenterToWorld(grid, 8, 1)));

    // 全程可走；且必经缺口（x=5 列唯一可走的 (5,9)）。
    bool passedGap = false;
    for (const glm::vec2& p : path)
    {
        const glm::ivec2 c = Nav::WorldToCell(grid, p);
        assert(Nav::IsWalkable(grid, c.x, c.y));
        if (c.x == 5 && c.y == 9)
        {
            passedGap = true;
        }
    }
    assert(passedGap);
}

// ---- FindPath 无路 -------------------------------------------------------

void TestFindPathNoPath()
{
    Nav::NavGrid grid = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});

    // goal cell (5,5) 四周正交邻居全阻挡：禁切角下对角也进不来 → 完全围死。
    Nav::SetWalkable(grid, 4, 5, false);
    Nav::SetWalkable(grid, 6, 5, false);
    Nav::SetWalkable(grid, 5, 4, false);
    Nav::SetWalkable(grid, 5, 6, false);

    const glm::vec2 start = Nav::CellCenterToWorld(grid, 0, 0);
    const glm::vec2 goal  = Nav::CellCenterToWorld(grid, 5, 5);

    const std::vector<glm::vec2> path = Nav::FindPath(grid, start, goal, true);
    assert(path.empty());
}

// ---- FindPath 退化 -------------------------------------------------------

void TestFindPathDegenerate()
{
    Nav::NavGrid grid = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});

    const glm::vec2 inside = Nav::CellCenterToWorld(grid, 3, 3);

    // start 越界（世界坐标在网格外）→ 空。
    assert(Nav::FindPath(grid, {-5.0f, -5.0f}, inside, true).empty());
    // goal 越界 → 空。
    assert(Nav::FindPath(grid, inside, {50.0f, 50.0f}, true).empty());

    // start / goal 落在阻挡 cell → 空。
    Nav::SetWalkable(grid, 2, 2, false);
    const glm::vec2 blocked = Nav::CellCenterToWorld(grid, 2, 2);
    assert(Nav::FindPath(grid, blocked, inside, true).empty());
    assert(Nav::FindPath(grid, inside, blocked, true).empty());

    // start cell == goal cell → 单路点。
    const std::vector<glm::vec2> same = Nav::FindPath(grid, inside, inside, true);
    assert(same.size() == 1);
    assert(NearV2(same.front(), Nav::CellCenterToWorld(grid, 3, 3)));

    // 空网格 → 空。
    Nav::NavGrid empty = Nav::MakeNavGrid(0, 0, 1.0f, {0.0f, 0.0f});
    assert(Nav::FindPath(empty, {0.0f, 0.0f}, {1.0f, 1.0f}, true).empty());
}

// ---- 对角 vs 4-邻接 + 禁切角 ---------------------------------------------

void TestDiagonalVsOrthoAndCornerCut()
{
    Nav::NavGrid grid = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});

    const glm::vec2 start = Nav::CellCenterToWorld(grid, 1, 1);
    const glm::vec2 goal  = Nav::CellCenterToWorld(grid, 8, 8);

    const std::vector<glm::vec2> diag  = Nav::FindPath(grid, start, goal, true);
    const std::vector<glm::vec2> ortho = Nav::FindPath(grid, start, goal, false);
    assert(!diag.empty());
    assert(!ortho.empty());
    // 对角可斜切，路点数应 ≤ 4-邻接（对角 ~8 步 vs 曼哈顿 ~14 步）。
    assert(diag.size() <= ortho.size());

    // 禁切角：构造"墙角"—— cell(5,5) 与 (5,6) 阻挡，从 (4,5) 到 (6,6)。
    Nav::NavGrid corner = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});
    Nav::SetWalkable(corner, 5, 5, false);
    Nav::SetWalkable(corner, 5, 6, false);

    const glm::vec2 cs = Nav::CellCenterToWorld(corner, 4, 5);
    const glm::vec2 cg = Nav::CellCenterToWorld(corner, 6, 6);

    const std::vector<glm::vec2> cpath = Nav::FindPath(corner, cs, cg, true);
    assert(!cpath.empty());

    for (std::size_t i = 0; i < cpath.size(); ++i)
    {
        const glm::ivec2 c = Nav::WorldToCell(corner, cpath[i]);
        // 绝不踏进阻挡 cell。
        assert(Nav::IsWalkable(corner, c.x, c.y));
        if (i > 0)
        {
            const glm::ivec2 prev = Nav::WorldToCell(corner, cpath[i - 1]);
            const int        ddx  = std::abs(c.x - prev.x);
            const int        ddy  = std::abs(c.y - prev.y);
            // 若为对角步（两轴都变），两个相邻正交 cell 必须都可走（未切角）。
            if (ddx == 1 && ddy == 1)
            {
                assert(Nav::IsWalkable(corner, c.x, prev.y));
                assert(Nav::IsWalkable(corner, prev.x, c.y));
            }
        }
    }
}

// ---- SimplifyPath --------------------------------------------------------

void TestSimplifyPath()
{
    Nav::NavGrid grid = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});

    // 全可走网格上一条水平直线路径（含中间路点）。
    const glm::vec2 start = Nav::CellCenterToWorld(grid, 1, 5);
    const glm::vec2 goal  = Nav::CellCenterToWorld(grid, 8, 5);

    const std::vector<glm::vec2> straight = Nav::FindPath(grid, start, goal, false);
    assert(straight.size() >= 3);  // 至少含若干中间路点

    const std::vector<glm::vec2> simp = Nav::SimplifyPath(grid, straight);
    // 化简后路点变少；理想直线仅剩起终 2 点。
    assert(simp.size() < straight.size());
    assert(simp.size() == 2);
    assert(NearV2(simp.front(), straight.front()));
    assert(NearV2(simp.back(), straight.back()));

    // 空 / 单点 / 双点原样返回。
    assert(Nav::SimplifyPath(grid, {}).empty());
    const std::vector<glm::vec2> one{start};
    assert(Nav::SimplifyPath(grid, one).size() == 1);
    const std::vector<glm::vec2> two{start, goal};
    assert(Nav::SimplifyPath(grid, two).size() == 2);

    // 有障碍：绕障路径化简后每相邻路点段仍不穿墙。
    Nav::NavGrid wall = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});
    for (int y = 0; y <= 8; ++y)
    {
        Nav::SetWalkable(wall, 5, y, false);
    }
    const glm::vec2 ws = Nav::CellCenterToWorld(wall, 1, 1);
    const glm::vec2 wg = Nav::CellCenterToWorld(wall, 8, 1);

    const std::vector<glm::vec2> wpath = Nav::FindPath(wall, ws, wg, true);
    assert(!wpath.empty());
    const std::vector<glm::vec2> wsimp = Nav::SimplifyPath(wall, wpath);
    assert(wsimp.size() <= wpath.size());
    assert(wsimp.size() >= 2);
    assert(NearV2(wsimp.front(), wpath.front()));
    assert(NearV2(wsimp.back(), wpath.back()));
    // 每相邻路点段全程可走（不穿墙）。
    for (std::size_t i = 1; i < wsimp.size(); ++i)
    {
        assert(SegmentWalkable(wall, wsimp[i - 1], wsimp[i]));
    }
}

// ---- BakeWalkableFromPhysics --------------------------------------------

void TestBakeFromPhysics()
{
    Phys::PhysicsWorld world;

    // static box：中心 (5,5)、halfExtents (1,1) → 覆盖世界 x[4,6] × y[4,6]。
    Phys::RigidBodyComponent rb;
    rb.type            = Phys::BodyType::Static;
    rb.initialPosition = {5.0f, 5.0f};
    Phys::ColliderComponent col;
    col.shape = Phys::BoxDesc{{1.0f, 1.0f}, {0.0f, 0.0f}};
    const Phys::BodyHandle body = world.AddBody(rb, col);
    assert(body.IsValid());

    // Step 一次让 broad-phase 就绪。
    world.Step(1.0f / 60.0f);

    Nav::NavGrid grid = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});
    Nav::BakeWalkableFromPhysics(grid, world);

    // box 覆盖区的 cell 被标阻挡。cell(5,5) 世界 AABB [5,6]×[5,6]，与 box 重叠。
    assert(!Nav::IsWalkable(grid, 5, 5));
    assert(!Nav::IsWalkable(grid, 4, 4));  // AABB [4,5]×[4,5] 亦重叠

    // 远处 cell 可走（宽相 fat-AABB 边距够不到）。
    assert(Nav::IsWalkable(grid, 0, 0));
    assert(Nav::IsWalkable(grid, 9, 9));

    // solidMask 不匹配 box 的 categoryBits（默认 0x0001）→ 全部标可走。
    Nav::NavGrid grid2 = Nav::MakeNavGrid(10, 10, 1.0f, {0.0f, 0.0f});
    Nav::BakeWalkableFromPhysics(grid2, world, 0x0002u);  // 只找 category bit 2 的实心体
    assert(Nav::IsWalkable(grid2, 5, 5));
}

// SimplifyPath 禁切角一致性回归（对抗式复核逮到）：旧 point-sampling LOS 会漏检对角擦角，
// 把绕角路径塌成擦过斜对角墙角的对角段（重新引入 FindPath 禁止的 corner cut）。精确
// supercover LOS 修复后，SimplifyPath 不得产出擦 (2,3)/(3,2) 墙角的 (2.5,2.5)→(3.5,3.5) 段。
void TestSimplifyNoCornerCut()
{
    Nav::NavGrid grid = Nav::MakeNavGrid(6, 6, 1.0f, {0.0f, 0.0f});
    Nav::SetWalkable(grid, 2, 3, false);  // 斜对角墙（两块斜对角阻挡 cell）
    Nav::SetWalkable(grid, 3, 2, false);

    // 绕 (2,3)/(3,2) 斜墙的合法折线（各段正交、途经 cell 全可走）。
    const std::vector<glm::vec2> detour = {
        {2.5f, 2.5f}, {1.5f, 2.5f}, {1.5f, 4.5f}, {3.5f, 4.5f}, {3.5f, 3.5f}};

    const std::vector<glm::vec2> simp = Nav::SimplifyPath(grid, detour);
    assert(NearV2(simp.front(), {2.5f, 2.5f}) && "化简保留起点");
    assert(NearV2(simp.back(), {3.5f, 3.5f}) && "化简保留终点");

    // 任一相邻段都不得是擦墙角的 (2.5,2.5)→(3.5,3.5) 直对角（禁切角一致性）。
    // 旧 point-sampling LOS 会把整条路径塌成这一段 → 此断言逮住回归。
    for (std::size_t i = 1; i < simp.size(); ++i)
    {
        const bool cut = NearV2(simp[i - 1], {2.5f, 2.5f}) && NearV2(simp[i], {3.5f, 3.5f});
        assert(!cut && "SimplifyPath 不得擦过 (2,3)/(3,2) 斜墙角切对角");
    }
    std::printf("  [PASS] SimplifyPath 禁切角一致性（supercover LOS）\n");
}

}  // namespace

int main()
{
    TestNavGridHelpers();
    TestFindPathStraight();
    TestFindPathAroundWall();
    TestFindPathNoPath();
    TestFindPathDegenerate();
    TestDiagonalVsOrthoAndCornerCut();
    TestSimplifyPath();
    TestSimplifyNoCornerCut();
    TestBakeFromPhysics();
    std::printf("NavigationTest: all subtests passed\n");
    return 0;
}
