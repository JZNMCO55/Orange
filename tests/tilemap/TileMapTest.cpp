// TileMapTest —— Tilemap 子系统 headless 验收。裸 <cassert> + 独立 main()，
// 进程 exit 0 = pass。
//
// 子测试：
//   * TileMap helper：MakeTileMap 全 0 / SetTile-GetTile / 越界 / IsSolidTile
//     边界（id=0、未登记 ID、越界 ID 均非实心）/ TileCenterToWorld ↔ WorldToTile
//   * MergeSolidTiles 实心块：全实心 3×2 块 → 恰 1 个 TileRect
//   * MergeSolidTiles L 形 / 散布：覆盖完整性（每实心 tile 恰一 rect、无 rect 盖
//     非实心、面积和 == 实心 tile 总数）
//   * 空 map / 无实心 → 空 vector
//   * TileRectToWorldAABB：非原点 + tileSize=2 的世界 AABB
//   * BuildNavGridFromTileMap：实心 tile → 不可走 cell、空 tile → 可走 + 网格对齐

#include "orange/engine/tilemap/TileMap.h"
#include "orange/engine/tilemap/TileMapCollision.h"
#include "orange/engine/tilemap/TileMapNavBake.h"
#include "orange/engine/nav/NavGrid.h"

#include <glm/vec2.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Tm  = Orange::Engine::Tilemap;
namespace Nav = Orange::Engine::Nav;

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

// tileset：id 1 / 2 实心，其余非实心。solidById 索引 = tile ID。
Tm::TileSet MakeSolidSet()
{
    Tm::TileSet ts;
    ts.solidById = {0, 1, 1};  // id0 空非实心、id1 solid、id2 solid
    return ts;
}

// ---- TileMap helper ------------------------------------------------------

void TestTileMapHelpers()
{
    // MakeTileMap：全 0 tiles，尺寸对。
    Tm::TileMap map = Tm::MakeTileMap(4, 3, 2.0f, glm::vec2(1.0f, -1.0f));
    assert(map.width == 4 && map.height == 3);
    assert(map.tiles.size() == 12);
    for (std::int32_t id : map.tiles)
    {
        assert(id == 0);
    }
    assert(Near(map.tileSize, 2.0f));

    // 负 / 零尺寸 → 空地图。
    Tm::TileMap empty = Tm::MakeTileMap(0, 5, 1.0f, glm::vec2(0.0f));
    assert(empty.width == 0 && empty.height == 0 && empty.tiles.empty());

    // SetTile / GetTile round-trip。
    Tm::SetTile(map, 2, 1, 7);
    assert(Tm::GetTile(map, 2, 1) == 7);
    assert(Tm::GetTile(map, 0, 0) == 0);
    assert(Tm::InBounds(map, 3, 2) && !Tm::InBounds(map, 4, 2) && !Tm::InBounds(map, -1, 0));

    // 越界 GetTile → 0；越界 SetTile → no-op（不崩、不改任何 cell）。
    assert(Tm::GetTile(map, 99, 99) == 0);
    assert(Tm::GetTile(map, -1, -1) == 0);
    Tm::SetTile(map, 99, 99, 5);
    Tm::SetTile(map, -3, 1, 5);
    for (std::size_t i = 0; i < map.tiles.size(); ++i)
    {
        // 只有 (2,1) 被设过 7，其余仍是 0（越界写没漏进来）。
        const bool isSet = (i == static_cast<std::size_t>(1 * map.width + 2));
        assert(map.tiles[i] == (isSet ? 7 : 0));
    }

    // IsSolidTile 边界：id0 空非实心；未登记 ID（3，超 solidById 长度）非实心；
    // 登记为 solid 的 id1/id2 实心；越界坐标非实心。
    Tm::TileSet ts;
    ts.solidById = {0, 1, 1};  // 长度 3：id0 空、id1 solid、id2 solid
    Tm::TileMap m2 = Tm::MakeTileMap(3, 3, 1.0f, glm::vec2(0.0f));
    Tm::SetTile(m2, 0, 0, 1);   // solid
    Tm::SetTile(m2, 1, 0, 2);   // solid
    Tm::SetTile(m2, 2, 0, 3);   // 未登记 ID（>= solidById.size()）→ 非实心
    Tm::SetTile(m2, 0, 1, 0);   // 空 → 非实心
    assert(Tm::IsSolidTile(m2, ts, 0, 0));
    assert(Tm::IsSolidTile(m2, ts, 1, 0));
    assert(!Tm::IsSolidTile(m2, ts, 2, 0));  // 未登记 ID 非实心
    assert(!Tm::IsSolidTile(m2, ts, 0, 1));  // 空非实心
    assert(!Tm::IsSolidTile(m2, ts, 5, 5));  // 越界坐标非实心

    // 空 tileset（solidById 空）：任何 tile 都非实心（连 id1 都不在集里）。
    Tm::TileSet emptySet;
    assert(!Tm::IsSolidTile(m2, emptySet, 0, 0));

    // TileCenterToWorld ↔ WorldToTile 一致性（非原点 + 非 1 tileSize）。
    // origin=(1,-1)、tileSize=2：tile(2,1) 中心 = (1,-1)+(2.5,1.5)*2 = (6,2)。
    const glm::vec2 center = Tm::TileCenterToWorld(map, 2, 1);
    assert(NearV2(center, glm::vec2(6.0f, 2.0f)));
    // 该中心 WorldToTile 回落 (2,1)。
    const glm::ivec2 back = Tm::WorldToTile(map, center);
    assert(back.x == 2 && back.y == 1);
    // 遍历若干 tile 校验 round-trip（中心点必回本 tile）。
    for (int y = 0; y < map.height; ++y)
    {
        for (int x = 0; x < map.width; ++x)
        {
            const glm::ivec2 rt = Tm::WorldToTile(map, Tm::TileCenterToWorld(map, x, y));
            assert(rt.x == x && rt.y == y);
        }
    }
    // 原点角落映到 tile(0,0)；略偏原点内也在 tile(0,0)。
    assert(Tm::WorldToTile(map, map.origin) == glm::ivec2(0, 0));
    assert(Tm::WorldToTile(map, map.origin + glm::vec2(0.1f, 0.1f)) == glm::ivec2(0, 0));

    std::printf("[ok] TileMap helpers\n");
}

// ---- MergeSolidTiles：全实心块 -------------------------------------------

void TestMergeSolidBlock()
{
    Tm::TileMap     map = Tm::MakeTileMap(8, 8, 1.0f, glm::vec2(0.0f));
    Tm::TileSet     ts  = MakeSolidSet();

    // 3×2 全实心块：tile (2,2)..(4,3)（x∈[2,4]、y∈[2,3]）设 1。
    for (int y = 2; y <= 3; ++y)
    {
        for (int x = 2; x <= 4; ++x)
        {
            Tm::SetTile(map, x, y, 1);
        }
    }

    const std::vector<Tm::TileRect> rects = Tm::MergeSolidTiles(map, ts);
    assert(rects.size() == 1);
    assert(rects[0].x == 2 && rects[0].y == 2 && rects[0].w == 3 && rects[0].h == 2);

    std::printf("[ok] MergeSolidTiles solid block → 1 rect\n");
}

// ---- 覆盖完整性 helper ---------------------------------------------------

// 校验 rects 对 map 的实心 tile 是"精确不重叠划分"：
//   ① 每个实心 tile 落在恰好一个 rect 内（不多不少）
//   ② 没有 rect 盖到非实心 tile
//   ③ 所有 rect 面积和 == 实心 tile 总数
void AssertExactCover(const Tm::TileMap& map, const Tm::TileSet& ts,
                      const std::vector<Tm::TileRect>& rects)
{
    // ② 无 rect 盖非实心 tile。
    long long areaSum = 0;
    for (const Tm::TileRect& r : rects)
    {
        assert(r.w > 0 && r.h > 0);
        areaSum += static_cast<long long>(r.w) * static_cast<long long>(r.h);
        for (int dy = 0; dy < r.h; ++dy)
        {
            for (int dx = 0; dx < r.w; ++dx)
            {
                assert(Tm::IsSolidTile(map, ts, r.x + dx, r.y + dy) &&
                       "rect 覆盖了非实心 tile");
            }
        }
    }

    // ① 每个实心 tile 恰被一个 rect 覆盖；非实心 tile 被 0 个 rect 覆盖。
    long long solidCount = 0;
    for (int y = 0; y < map.height; ++y)
    {
        for (int x = 0; x < map.width; ++x)
        {
            int hits = 0;
            for (const Tm::TileRect& r : rects)
            {
                if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
                {
                    ++hits;
                }
            }
            if (Tm::IsSolidTile(map, ts, x, y))
            {
                ++solidCount;
                assert(hits == 1 && "实心 tile 必须恰被一个 rect 覆盖");
            }
            else
            {
                assert(hits == 0 && "非实心 tile 不能被任何 rect 覆盖");
            }
        }
    }

    // ③ 面积和 == 实心总数（不重叠 + 完整覆盖的等价数值检查）。
    assert(areaSum == solidCount);
}

// ---- MergeSolidTiles：L 形 / 散布覆盖完整性 -------------------------------

void TestMergeCoverageCompleteness()
{
    // L 形：底行 (1,1)..(4,1) + 左列 (1,1)..(1,3)。贪心合并会切成多块，只要
    // 覆盖精确即可（不强求块数，强求正确性）。
    {
        Tm::TileMap map = Tm::MakeTileMap(6, 6, 1.0f, glm::vec2(0.0f));
        Tm::TileSet ts  = MakeSolidSet();
        for (int x = 1; x <= 4; ++x)
        {
            Tm::SetTile(map, x, 1, 1);  // 底横
        }
        for (int y = 1; y <= 3; ++y)
        {
            Tm::SetTile(map, 1, y, 1);  // 左竖
        }
        const std::vector<Tm::TileRect> rects = Tm::MergeSolidTiles(map, ts);
        assert(!rects.empty());
        AssertExactCover(map, ts, rects);
    }

    // 散布 + 带洞：一个 5×4 实心块中间挖两个非实心洞，再加几个孤立 tile。
    // 洞会强制贪心合并断成多块，考验向下扩的整行检查 + covered 标记。
    {
        Tm::TileMap map = Tm::MakeTileMap(10, 8, 1.0f, glm::vec2(0.0f));
        Tm::TileSet ts  = MakeSolidSet();
        for (int y = 1; y <= 4; ++y)
        {
            for (int x = 1; x <= 5; ++x)
            {
                Tm::SetTile(map, x, y, 1);
            }
        }
        // 挖洞（改回空）。
        Tm::SetTile(map, 2, 2, 0);
        Tm::SetTile(map, 3, 2, 0);
        Tm::SetTile(map, 4, 3, 0);
        // 孤立 tile（不同实心 ID 也算实心）。
        Tm::SetTile(map, 8, 6, 2);
        Tm::SetTile(map, 0, 7, 1);
        Tm::SetTile(map, 9, 0, 1);

        const std::vector<Tm::TileRect> rects = Tm::MergeSolidTiles(map, ts);
        assert(!rects.empty());
        AssertExactCover(map, ts, rects);
    }

    // 棋盘格：每个实心 tile 都被非实心隔开，必然每 tile 一 rect（合并无从下手，
    // 但覆盖仍须精确）。
    {
        Tm::TileMap map = Tm::MakeTileMap(5, 5, 1.0f, glm::vec2(0.0f));
        Tm::TileSet ts  = MakeSolidSet();
        long long   expectSolid = 0;
        for (int y = 0; y < 5; ++y)
        {
            for (int x = 0; x < 5; ++x)
            {
                if (((x + y) & 1) == 0)
                {
                    Tm::SetTile(map, x, y, 1);
                    ++expectSolid;
                }
            }
        }
        const std::vector<Tm::TileRect> rects = Tm::MergeSolidTiles(map, ts);
        AssertExactCover(map, ts, rects);
        // 棋盘格：相邻实心 tile 不接触 → 每 tile 独立 rect（每 rect 面积 1）。
        assert(static_cast<long long>(rects.size()) == expectSolid);
        for (const Tm::TileRect& r : rects)
        {
            assert(r.w == 1 && r.h == 1);
        }
    }

    std::printf("[ok] MergeSolidTiles coverage completeness (L / holes / checkerboard)\n");
}

// ---- 空 map / 无实心 ------------------------------------------------------

void TestMergeEmptyAndNoSolid()
{
    Tm::TileSet ts = MakeSolidSet();

    // 空 map（0 尺寸）→ 空 vector。
    Tm::TileMap empty;
    assert(Tm::MergeSolidTiles(empty, ts).empty());

    // 有尺寸但全空 → 空 vector。
    Tm::TileMap allEmpty = Tm::MakeTileMap(6, 4, 1.0f, glm::vec2(0.0f));
    assert(Tm::MergeSolidTiles(allEmpty, ts).empty());

    // 有 tile 但 tileset 里都非实心（全 id=9，未登记）→ 空 vector。
    Tm::TileMap nonSolid = Tm::MakeTileMap(4, 4, 1.0f, glm::vec2(0.0f));
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            Tm::SetTile(nonSolid, x, y, 9);
        }
    }
    assert(Tm::MergeSolidTiles(nonSolid, ts).empty());

    std::printf("[ok] MergeSolidTiles empty / no-solid → empty\n");
}

// ---- TileRectToWorldAABB -------------------------------------------------

void TestTileRectToWorldAABB()
{
    // 非原点 + tileSize=2。TileRect{1,2,3,1}：
    //   lower = origin + (1,2)*2 = origin + (2,4)
    //   upper = origin + (1+3, 2+1)*2 = origin + (8,6)
    const glm::vec2 origin(10.0f, -5.0f);
    Tm::TileMap     map = Tm::MakeTileMap(8, 8, 2.0f, origin);
    Tm::TileRect    rect{1, 2, 3, 1};

    glm::vec2 lower, upper;
    Tm::TileRectToWorldAABB(map, rect, lower, upper);
    assert(NearV2(lower, origin + glm::vec2(2.0f, 4.0f)));
    assert(NearV2(upper, origin + glm::vec2(8.0f, 6.0f)));

    std::printf("[ok] TileRectToWorldAABB\n");
}

// ---- BuildNavGridFromTileMap ---------------------------------------------

void TestBuildNavGrid()
{
    Tm::TileMap map = Tm::MakeTileMap(5, 4, 3.0f, glm::vec2(2.0f, 7.0f));
    Tm::TileSet ts  = MakeSolidSet();

    // 摆几个实心 tile。
    Tm::SetTile(map, 0, 0, 1);
    Tm::SetTile(map, 2, 1, 2);
    Tm::SetTile(map, 4, 3, 1);
    Tm::SetTile(map, 3, 3, 9);  // 未登记 ID → 非实心 → 应可走

    const Nav::NavGrid grid = Tm::BuildNavGridFromTileMap(map, ts);

    // 网格 width / height / cellSize / origin 与 map 一致。
    assert(grid.width == map.width && grid.height == map.height);
    assert(Near(grid.cellSize, map.tileSize));
    assert(NearV2(grid.origin, map.origin));

    // 逐 cell：实心 tile → !IsWalkable，其余 → IsWalkable。
    for (int y = 0; y < map.height; ++y)
    {
        for (int x = 0; x < map.width; ++x)
        {
            const bool solid = Tm::IsSolidTile(map, ts, x, y);
            assert(Nav::IsWalkable(grid, x, y) == !solid);
        }
    }
    // 抽查具体点。
    assert(!Nav::IsWalkable(grid, 0, 0));   // solid
    assert(!Nav::IsWalkable(grid, 2, 1));   // solid
    assert(!Nav::IsWalkable(grid, 4, 3));   // solid
    assert(Nav::IsWalkable(grid, 3, 3));    // 未登记 ID → 可走
    assert(Nav::IsWalkable(grid, 1, 1));    // 空 → 可走

    // 空 map → 空网格（不崩）。
    Tm::TileMap  emptyMap;
    Nav::NavGrid emptyGrid = Tm::BuildNavGridFromTileMap(emptyMap, ts);
    assert(emptyGrid.width == 0 && emptyGrid.height == 0);

    // TileMapComponent 纯数据可携带 map + tileset。
    Tm::TileMapComponent comp;
    comp.map     = map;
    comp.tileset = ts;
    assert(comp.map.width == 5 && comp.tileset.solidById.size() == 3);

    std::printf("[ok] BuildNavGridFromTileMap\n");
}

// 畸形 map 安全回归（对抗式复核逮到）：width/height 与 tiles 不同步（如手工构造 POD 未
// resize）时 GetTile/SetTile 应优雅退化（GetTile→0 / SetTile no-op）而非堆越界 UB，且
// MergeSolidTiles / BuildNavGridFromTileMap 扫全域时不崩。
void TestMalformedMapSafe()
{
    Tm::TileMap bad;
    bad.width  = 10;
    bad.height = 10;
    // tiles 故意留空（size 0 ≠ width*height=100）。
    assert(Tm::GetTile(bad, 5, 5) == 0 && "畸形 map GetTile 退化为 0");
    Tm::SetTile(bad, 5, 5, 1);  // no-op（不越界写）
    assert(Tm::GetTile(bad, 5, 5) == 0 && "畸形 map SetTile no-op（未越界写）");

    Tm::TileSet ts;
    ts.solidById = {0, 1};
    // 扫全 width*height 域的算法不得因 tiles 空而越界崩溃。
    const std::vector<Tm::TileRect> rects = Tm::MergeSolidTiles(bad, ts);
    assert(rects.empty() && "畸形 map 无实心 → 空 rect（不崩）");
    const Nav::NavGrid grid = Tm::BuildNavGridFromTileMap(bad, ts);
    assert(grid.width == 10 && grid.height == 10 && "畸形 map nav bake 不崩、尺寸对齐");
    std::printf("[ok] 畸形 map 安全（GetTile/SetTile/Merge/NavBake 不越界）\n");
}

}  // namespace

int main()
{
    TestTileMapHelpers();
    TestMergeSolidBlock();
    TestMergeCoverageCompleteness();
    TestMergeEmptyAndNoSolid();
    TestTileRectToWorldAABB();
    TestBuildNavGrid();
    TestMalformedMapSafe();

    std::printf("TileMapTest: all passed\n");
    return 0;
}
