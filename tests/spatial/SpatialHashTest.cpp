// SpatialHash 的 headless 单元测试：均匀网格空间哈希 (broad-phase) 的插入 / 更新 /
// 移除 + 区域 / 点 / 半径查询。纯数据结构 + 几何，无 World / GPU / GUI。裸 main()
// + <cassert>。
//
// 覆盖点：
//   * 基本 Insert / QueryAABB —— 命中该命中的、排除不该的。
//   * 多格 item + dedup —— 横跨多 cell 的 item 只出现一次；触及任一部分的小查询都能找到。
//   * QueryPoint —— 点落在 AABB 内 / 外。
//   * QueryRadius（圆 vs AABB narrow）—— 最近点距离略小于 / 大于 radius；radius<=0 空。
//   * 负坐标 —— 负象限 item 查询命中（CellCoord floor 正确性）。
//   * Remove —— 移除后不再命中、Size 减、移除不存在 no-op。
//   * Update / 移动 —— 旧位置不再命中、新位置命中、Size 不变。
//   * 边界接触 —— AABB 边缘恰接触（<= 语义命中）。
//   * Clear / 空 —— 空 hash 查询空；Clear 后 Size 0、查询空。
//   * cellSize<=0 —— 构造钳极小正数不崩，仍能 Insert / Query。
//   * ★暴力法交叉验证 —— N 个随机 item，M 个随机查询，SpatialHash 结果与暴力扫描逐一
//     相等（完备性 + narrow 正确性 + dedup 的权威验证）。

#include "orange/engine/spatial/SpatialHash.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace Spatial = ::Orange::Engine::Spatial;
using Spatial::Aabb2D;
using Spatial::SpatialHash;

namespace
{

    Aabb2D MakeAabb(float lx, float ly, float ux, float uy)
    {
        return Aabb2D{glm::vec2{lx, ly}, glm::vec2{ux, uy}};
    }

    // 结果集合是否含 id（顺序不保证，故用线性查找）。
    bool Has(const std::vector<std::uint64_t>& v, std::uint64_t id)
    {
        return std::find(v.begin(), v.end(), id) != v.end();
    }

    // 结果里 id 出现次数（dedup 断言用）。
    std::size_t CountOf(const std::vector<std::uint64_t>& v, std::uint64_t id)
    {
        std::size_t n = 0;
        for (const std::uint64_t x : v)
        {
            if (x == id)
            {
                ++n;
            }
        }
        return n;
    }

    std::vector<std::uint64_t> Sorted(std::vector<std::uint64_t> v)
    {
        std::sort(v.begin(), v.end());
        return v;
    }

    // 确定性 LCG（固定 seed），供暴力交叉验证生成可复现的随机数据。
    struct Lcg
    {
        std::uint64_t state;
        explicit Lcg(std::uint64_t seed) : state(seed) {}
        std::uint32_t Next()
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<std::uint32_t>(state >> 32);
        }
        // [lo, hi) 浮点。
        float NextFloat(float lo, float hi)
        {
            const float u = static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f);
            return lo + u * (hi - lo);
        }
    };

    // ---------------------------------------------------------------------------
    // 基本 Insert / QueryAABB
    // ---------------------------------------------------------------------------
    void TestBasicQuery()
    {
        SpatialHash hash(1.0f);
        hash.Insert(1, MakeAabb(0.0f, 0.0f, 1.0f, 1.0f));   // 原点附近
        hash.Insert(2, MakeAabb(5.0f, 5.0f, 6.0f, 6.0f));   // 远处
        hash.Insert(3, MakeAabb(10.0f, 0.0f, 11.0f, 1.0f)); // 另一处

        assert(hash.Size() == 3);

        // 查询区盖住 item 1，不碰 2/3。
        const auto near0 = hash.QueryAABB(MakeAabb(-0.5f, -0.5f, 0.5f, 0.5f));
        assert(Has(near0, 1));
        assert(!Has(near0, 2));
        assert(!Has(near0, 3));

        // 查询区盖住 item 2。
        const auto near5 = hash.QueryAABB(MakeAabb(5.2f, 5.2f, 5.8f, 5.8f));
        assert(Has(near5, 2));
        assert(!Has(near5, 1));
        assert(!Has(near5, 3));

        // 大查询区盖住全部。
        const auto all = hash.QueryAABB(MakeAabb(-1.0f, -1.0f, 12.0f, 12.0f));
        assert(all.size() == 3);
        assert(Has(all, 1) && Has(all, 2) && Has(all, 3));

        // 空隙查询命中 0（narrow 过滤掉 cell 粒度假阳性）。
        const auto gap = hash.QueryAABB(MakeAabb(2.5f, 2.5f, 3.0f, 3.0f));
        assert(gap.empty());

        std::printf("  [ok] TestBasicQuery\n");
    }

    // ---------------------------------------------------------------------------
    // 多格 item + dedup —— item 横跨 4x4=16 个 cell，任一触及部分的查询都命中且只一次
    // ---------------------------------------------------------------------------
    void TestMultiCellDedup()
    {
        SpatialHash hash(1.0f);
        // bounds {(0,0),(3,3)}，cellSize=1 → 覆盖 cx∈[0,3]、cy∈[0,3]，共 16 个 cell。
        hash.Insert(42, MakeAabb(0.0f, 0.0f, 3.0f, 3.0f));

        // 大查询盖全物：命中且恰一次（dedup）。
        const auto whole = hash.QueryAABB(MakeAabb(-1.0f, -1.0f, 5.0f, 5.0f));
        assert(Has(whole, 42));
        assert(CountOf(whole, 42) == 1);

        // 从触及 item 各角 / 各边的小查询都能找到它，且都只一次。
        const glm::vec2 probes[] = {
            {0.1f, 0.1f},
            {2.9f, 2.9f},
            {0.1f, 2.9f},
            {2.9f, 0.1f},
            {1.5f, 1.5f},
            {0.0f, 1.5f},
            {3.0f, 1.5f},
            {1.5f, 0.0f},
            {1.5f, 3.0f},
        };
        for (const glm::vec2 p : probes)
        {
            const auto r = hash.QueryAABB(MakeAabb(p.x - 0.05f, p.y - 0.05f, p.x + 0.05f, p.y + 0.05f));
            assert(Has(r, 42));
            assert(CountOf(r, 42) == 1);
        }

        // 一个横跨多 cell 的大查询区（跨越 item 的多个登记 cell）也只返回一次。
        const auto span = hash.QueryAABB(MakeAabb(0.5f, 0.5f, 2.5f, 2.5f));
        assert(CountOf(span, 42) == 1);

        std::printf("  [ok] TestMultiCellDedup\n");
    }

    // ---------------------------------------------------------------------------
    // QueryPoint —— 点落在 AABB 内 / 外
    // ---------------------------------------------------------------------------
    void TestQueryPoint()
    {
        SpatialHash hash(2.0f);
        hash.Insert(7, MakeAabb(1.0f, 1.0f, 4.0f, 4.0f));

        const auto inside = hash.QueryPoint(glm::vec2{2.0f, 3.0f});
        assert(Has(inside, 7));

        const auto onEdge = hash.QueryPoint(glm::vec2{4.0f, 4.0f}); // 边界（<= 命中）
        assert(Has(onEdge, 7));

        const auto outside = hash.QueryPoint(glm::vec2{5.0f, 5.0f});
        assert(!Has(outside, 7));

        std::printf("  [ok] TestQueryPoint\n");
    }

    // ---------------------------------------------------------------------------
    // QueryRadius（圆 vs AABB narrow）
    // ---------------------------------------------------------------------------
    void TestQueryRadius()
    {
        SpatialHash hash(1.0f);
        // 圆心 (0,0)，radius=2。
        // 近 item：AABB {(1,0),(1.5,0.5)}，最近点 (1,0)，距离 1.0 < 2 → 命中。
        hash.Insert(100, MakeAabb(1.0f, 0.0f, 1.5f, 0.5f));
        // 远 item：AABB {(3,0),(3.5,0.5)}，最近点 (3,0)，距离 3.0 > 2 → 不命中。
        hash.Insert(200, MakeAabb(3.0f, 0.0f, 3.5f, 0.5f));

        const auto hit = hash.QueryRadius(glm::vec2{0.0f, 0.0f}, 2.0f);
        assert(Has(hit, 100));
        assert(!Has(hit, 200));

        // 对角最近点：AABB {(2,2),(3,3)}，最近点 (2,2)，距离 sqrt(8)≈2.83。
        hash.Insert(300, MakeAabb(2.0f, 2.0f, 3.0f, 3.0f));
        const auto small = hash.QueryRadius(glm::vec2{0.0f, 0.0f}, 2.8f); // <2.83 → 不命中
        assert(!Has(small, 300));
        const auto big = hash.QueryRadius(glm::vec2{0.0f, 0.0f}, 2.9f); // >2.83 → 命中
        assert(Has(big, 300));

        // 圆心落在 item AABB 内 → 距离 0，任意正 radius 命中。
        const auto center = hash.QueryRadius(glm::vec2{1.2f, 0.2f}, 0.01f);
        assert(Has(center, 100));

        // radius<=0 → 空。
        assert(hash.QueryRadius(glm::vec2{0.0f, 0.0f}, 0.0f).empty());
        assert(hash.QueryRadius(glm::vec2{0.0f, 0.0f}, -1.0f).empty());

        std::printf("  [ok] TestQueryRadius\n");
    }

    // ---------------------------------------------------------------------------
    // 负坐标 —— CellCoord floor 对负数正确
    // ---------------------------------------------------------------------------
    void TestNegativeCoords()
    {
        SpatialHash hash(1.0f);
        hash.Insert(1, MakeAabb(-5.0f, -5.0f, -4.0f, -4.0f)); // 负象限
        hash.Insert(2, MakeAabb(-0.5f, -0.5f, 0.5f, 0.5f));   // 跨 0（含负 cell）

        const auto q1 = hash.QueryAABB(MakeAabb(-4.8f, -4.8f, -4.2f, -4.2f));
        assert(Has(q1, 1));
        assert(!Has(q1, 2));

        // 跨 0 的查询命中 item 2（其登记进 cx∈[-1,0]、cy∈[-1,0]）。
        const auto q2 = hash.QueryPoint(glm::vec2{-0.3f, -0.3f});
        assert(Has(q2, 2));

        // 负象限半径查询。
        const auto r = hash.QueryRadius(glm::vec2{-4.5f, -4.5f}, 1.0f);
        assert(Has(r, 1));

        std::printf("  [ok] TestNegativeCoords\n");
    }

    // ---------------------------------------------------------------------------
    // Remove
    // ---------------------------------------------------------------------------
    void TestRemove()
    {
        SpatialHash hash(1.0f);
        hash.Insert(1, MakeAabb(0.0f, 0.0f, 3.0f, 3.0f)); // 多格
        hash.Insert(2, MakeAabb(5.0f, 5.0f, 6.0f, 6.0f));
        assert(hash.Size() == 2);

        hash.Remove(1);
        assert(hash.Size() == 1);
        assert(!hash.Contains(1));
        assert(hash.Contains(2));

        // item 1 曾登记的所有 cell 都摘干净——该区域（不含 item 2）查询空。
        const auto q = hash.QueryAABB(MakeAabb(0.0f, 0.0f, 3.0f, 3.0f));
        assert(!Has(q, 1));
        assert(q.empty());
        // 稍大区域仍不含 item 2（在 (5,5) 处），故也应为空——确认 item 1 各 cell 全摘干净。
        assert(hash.QueryAABB(MakeAabb(-1.0f, -1.0f, 4.0f, 4.0f)).empty());

        // 移除不存在 id → no-op。
        hash.Remove(999);
        assert(hash.Size() == 1);

        // 剩下的 item 2 仍可查。
        assert(Has(hash.QueryPoint(glm::vec2{5.5f, 5.5f}), 2));

        std::printf("  [ok] TestRemove\n");
    }

    // ---------------------------------------------------------------------------
    // Update / 移动
    // ---------------------------------------------------------------------------
    void TestUpdate()
    {
        SpatialHash hash(1.0f);
        hash.Insert(1, MakeAabb(0.0f, 0.0f, 1.0f, 1.0f));
        assert(Has(hash.QueryPoint(glm::vec2{0.5f, 0.5f}), 1));

        // 移动到远处：旧位置不再命中，新位置命中，Size 不变。
        hash.Update(1, MakeAabb(20.0f, 20.0f, 21.0f, 21.0f));
        assert(hash.Size() == 1);
        assert(!Has(hash.QueryPoint(glm::vec2{0.5f, 0.5f}), 1));  // 旧位置摘干净
        assert(Has(hash.QueryPoint(glm::vec2{20.5f, 20.5f}), 1)); // 新位置命中

        // Insert 已存在 id 也等价 Update。
        hash.Insert(1, MakeAabb(0.0f, 0.0f, 1.0f, 1.0f));
        assert(hash.Size() == 1);
        assert(!Has(hash.QueryPoint(glm::vec2{20.5f, 20.5f}), 1));
        assert(Has(hash.QueryPoint(glm::vec2{0.5f, 0.5f}), 1));

        std::printf("  [ok] TestUpdate\n");
    }

    // ---------------------------------------------------------------------------
    // 边界接触（<= 语义）
    // ---------------------------------------------------------------------------
    void TestBoundaryTouch()
    {
        SpatialHash hash(1.0f);
        hash.Insert(1, MakeAabb(0.0f, 0.0f, 1.0f, 1.0f));

        // 查询 AABB 左边缘恰与 item 右边缘 x=1 接触 → 命中（AabbOverlap 用 <=）。
        const auto touch = hash.QueryAABB(MakeAabb(1.0f, 0.0f, 2.0f, 1.0f));
        assert(Has(touch, 1));

        // 角接触。
        const auto corner = hash.QueryAABB(MakeAabb(1.0f, 1.0f, 2.0f, 2.0f));
        assert(Has(corner, 1));

        // 差一点点不接触（x 从 1.001 起）→ 不命中。
        const auto miss = hash.QueryAABB(MakeAabb(1.001f, 0.0f, 2.0f, 1.0f));
        assert(!Has(miss, 1));

        std::printf("  [ok] TestBoundaryTouch\n");
    }

    // ---------------------------------------------------------------------------
    // Clear / 空
    // ---------------------------------------------------------------------------
    void TestClearAndEmpty()
    {
        SpatialHash hash(1.0f);
        // 空 hash 查询返回空。
        assert(hash.Size() == 0);
        assert(hash.QueryAABB(MakeAabb(-10.0f, -10.0f, 10.0f, 10.0f)).empty());
        assert(hash.QueryPoint(glm::vec2{0.0f, 0.0f}).empty());
        assert(hash.QueryRadius(glm::vec2{0.0f, 0.0f}, 5.0f).empty());
        assert(!hash.Contains(1));

        hash.Insert(1, MakeAabb(0.0f, 0.0f, 1.0f, 1.0f));
        hash.Insert(2, MakeAabb(2.0f, 2.0f, 3.0f, 3.0f));
        assert(hash.Size() == 2);

        hash.Clear();
        assert(hash.Size() == 0);
        assert(!hash.Contains(1));
        assert(hash.QueryAABB(MakeAabb(-10.0f, -10.0f, 10.0f, 10.0f)).empty());

        // Clear 后仍能正常 Insert / Query。
        hash.Insert(5, MakeAabb(0.0f, 0.0f, 1.0f, 1.0f));
        assert(Has(hash.QueryPoint(glm::vec2{0.5f, 0.5f}), 5));

        std::printf("  [ok] TestClearAndEmpty\n");
    }

    // ---------------------------------------------------------------------------
    // cellSize<=0 —— 构造钳极小正数，不崩，仍可用
    // ---------------------------------------------------------------------------
    void TestDegenerateCellSize()
    {
        SpatialHash zero(0.0f);
        zero.Insert(1, MakeAabb(0.0f, 0.0f, 0.5f, 0.5f));
        zero.Insert(2, MakeAabb(1.0f, 1.0f, 1.5f, 1.5f));
        assert(zero.Size() == 2);
        assert(Has(zero.QueryPoint(glm::vec2{0.25f, 0.25f}), 1));
        assert(!Has(zero.QueryPoint(glm::vec2{0.25f, 0.25f}), 2));
        assert(Has(zero.QueryRadius(glm::vec2{1.25f, 1.25f}, 0.5f), 2));

        SpatialHash neg(-5.0f);
        neg.Insert(3, MakeAabb(0.0f, 0.0f, 0.5f, 0.5f));
        assert(Has(neg.QueryPoint(glm::vec2{0.25f, 0.25f}), 3));

        std::printf("  [ok] TestDegenerateCellSize\n");
    }

    // ---------------------------------------------------------------------------
    // ★ 暴力法交叉验证（完备性 + narrow 正确性 + dedup 的权威验证）
    // ---------------------------------------------------------------------------
    void TestBruteForceCrossValidation()
    {
        constexpr int   kNumItems   = 300;
        constexpr int   kNumQueries = 50;
        constexpr float kCellSize   = 4.0f; // 与 item 尺寸不同量级，逼多格 / 单格混合

        Lcg rng(0xC0FFEEu);

        SpatialHash hash(kCellSize);
        // item 存储：id → bounds，供暴力扫描比对。
        std::vector<std::uint64_t> ids;
        std::vector<Aabb2D>        bounds;
        ids.reserve(kNumItems);
        bounds.reserve(kNumItems);

        for (int i = 0; i < kNumItems; ++i)
        {
            // 坐标含负、尺寸不一（0.2 ~ 12，横跨 cellSize）。
            const float         lx = rng.NextFloat(-50.0f, 50.0f);
            const float         ly = rng.NextFloat(-50.0f, 50.0f);
            const float         w  = rng.NextFloat(0.2f, 12.0f);
            const float         h  = rng.NextFloat(0.2f, 12.0f);
            const Aabb2D        b  = MakeAabb(lx, ly, lx + w, ly + h);
            const std::uint64_t id = static_cast<std::uint64_t>(i) + 1; // 避 id=0
            hash.Insert(id, b);
            ids.push_back(id);
            bounds.push_back(b);
        }
        assert(hash.Size() == static_cast<std::size_t>(kNumItems));

        // --- QueryAABB 交叉验证 ---
        for (int q = 0; q < kNumQueries; ++q)
        {
            const float  lx   = rng.NextFloat(-55.0f, 55.0f);
            const float  ly   = rng.NextFloat(-55.0f, 55.0f);
            const float  w    = rng.NextFloat(0.5f, 30.0f);
            const float  h    = rng.NextFloat(0.5f, 30.0f);
            const Aabb2D area = MakeAabb(lx, ly, lx + w, ly + h);

            // 暴力扫描：遍历所有 item 做 AabbOverlap。
            std::vector<std::uint64_t> bruteRes;
            for (int i = 0; i < kNumItems; ++i)
            {
                if (Spatial::AabbOverlap(bounds[static_cast<std::size_t>(i)], area))
                {
                    bruteRes.push_back(ids[static_cast<std::size_t>(i)]);
                }
            }

            const auto hashRes = hash.QueryAABB(area);
            // 排序后逐一相等（同一集合，无漏无多；dedup 保证 hashRes 无重复）。
            assert(Sorted(hashRes) == Sorted(bruteRes));
        }

        // --- QueryRadius 交叉验证（圆 vs AABB 最近点距离）---
        for (int q = 0; q < kNumQueries; ++q)
        {
            const glm::vec2 c{rng.NextFloat(-55.0f, 55.0f), rng.NextFloat(-55.0f, 55.0f)};
            const float     radius = rng.NextFloat(0.5f, 25.0f);
            const float     r2     = radius * radius;

            std::vector<std::uint64_t> bruteRes;
            for (int i = 0; i < kNumItems; ++i)
            {
                const Aabb2D& b  = bounds[static_cast<std::size_t>(i)];
                const float   qx = std::clamp(c.x, b.lower.x, b.upper.x);
                const float   qy = std::clamp(c.y, b.lower.y, b.upper.y);
                const float   dx = c.x - qx;
                const float   dy = c.y - qy;
                if (dx * dx + dy * dy <= r2)
                {
                    bruteRes.push_back(ids[static_cast<std::size_t>(i)]);
                }
            }

            const auto hashRes = hash.QueryRadius(c, radius);
            assert(Sorted(hashRes) == Sorted(bruteRes));
        }

        // --- 移除一半后再交叉验证一轮（验证 Remove 摘 cell 干净）---
        for (int i = 0; i < kNumItems; i += 2)
        {
            hash.Remove(ids[static_cast<std::size_t>(i)]);
        }
        assert(hash.Size() == static_cast<std::size_t>(kNumItems / 2));

        for (int q = 0; q < kNumQueries; ++q)
        {
            const float  lx   = rng.NextFloat(-55.0f, 55.0f);
            const float  ly   = rng.NextFloat(-55.0f, 55.0f);
            const float  w    = rng.NextFloat(0.5f, 30.0f);
            const float  h    = rng.NextFloat(0.5f, 30.0f);
            const Aabb2D area = MakeAabb(lx, ly, lx + w, ly + h);

            std::vector<std::uint64_t> bruteRes;
            for (int i = 1; i < kNumItems; i += 2) // 只剩奇数下标（id 为偶数值）
            {
                if (Spatial::AabbOverlap(bounds[static_cast<std::size_t>(i)], area))
                {
                    bruteRes.push_back(ids[static_cast<std::size_t>(i)]);
                }
            }

            const auto hashRes = hash.QueryAABB(area);
            assert(Sorted(hashRes) == Sorted(bruteRes));
        }

        std::printf("  [ok] TestBruteForceCrossValidation (N=%d items, M=%d queries x3 rounds)\n",
                    kNumItems, kNumQueries);
    }

    // ---------------------------------------------------------------------------
    // 反序 bounds（lower>upper）—— Normalize 规范化后仍可查询 / Remove（对抗式复核测试缺口）
    // ---------------------------------------------------------------------------
    void TestReversedBounds()
    {
        SpatialHash hash(1.0f);
        // 传入 lower>upper 的反序 bounds（等价盒子 [(0,0),(3,3)]）。
        hash.Insert(1, MakeAabb(3.0f, 3.0f, 0.0f, 0.0f));
        assert(hash.Size() == 1);
        // 规范化后应能被覆盖该区域的查询命中。
        assert(Has(hash.QueryPoint(glm::vec2{1.5f, 1.5f}), 1));
        assert(Has(hash.QueryAABB(MakeAabb(2.5f, 2.5f, 4.0f, 4.0f)), 1));
        // Remove 也须用规范化后的 cell 摘干净。
        hash.Remove(1);
        assert(hash.Size() == 0);
        assert(hash.QueryPoint(glm::vec2{1.5f, 1.5f}).empty());
        std::printf("  [ok] TestReversedBounds\n");
    }

    // ---------------------------------------------------------------------------
    // oversized item（相对 cellSize 覆盖超上限 cell）—— 走 oversized 路径，查询仍正确、不
    // 产生无界迭代 / 卡死（对抗式复核逮到的 broad-phase 爆炸修复回归）。此测试若卡住即回归失败。
    // ---------------------------------------------------------------------------
    void TestOversizedItem()
    {
        SpatialHash hash(1.0f);
        // 覆盖约 1e6 cell（远超上限 65536）→ 若逐 cell 登记会分配 GB / 卡死；应走 oversized。
        hash.Insert(1, MakeAabb(-500.0f, -500.0f, 500.0f, 500.0f));
        hash.Insert(2, MakeAabb(0.0f, 0.0f, 1.0f, 1.0f)); // 普通 item（走 cell 路径）
        assert(hash.Size() == 2);

        // 小查询命中 oversized item（覆盖原点）+ 普通 item。
        const auto atOrigin = hash.QueryPoint(glm::vec2{0.5f, 0.5f});
        assert(Has(atOrigin, 1) && Has(atOrigin, 2));
        // 远处小查询只命中 oversized（普通 item 不在那）。
        const auto farInside = hash.QueryPoint(glm::vec2{400.0f, 400.0f});
        assert(Has(farInside, 1) && !Has(farInside, 2));
        // oversized item 外的点两者都不中。
        assert(hash.QueryPoint(glm::vec2{600.0f, 600.0f}).empty());
        // QueryRadius 也应扫到 oversized。
        assert(Has(hash.QueryRadius(glm::vec2{300.0f, 300.0f}, 1.0f), 1));

        // Remove oversized item（走 oversized 摘除分支，不逐 cell 迭代）。
        hash.Remove(1);
        assert(hash.Size() == 1);
        assert(!Has(hash.QueryPoint(glm::vec2{400.0f, 400.0f}), 1));

        // 超大查询区（覆盖超上限 cell）→ 扫全表回退，仍正确不卡。
        hash.Insert(3, MakeAabb(-500.0f, -500.0f, 500.0f, 500.0f));
        const auto hugeQuery = hash.QueryAABB(MakeAabb(-1000.0f, -1000.0f, 1000.0f, 1000.0f));
        assert(Has(hugeQuery, 2) && Has(hugeQuery, 3));
        std::printf("  [ok] TestOversizedItem\n");
    }

    // ---------------------------------------------------------------------------
    // 非有限输入（NaN/inf）—— Insert 忽略、查询返回空，不触发 float→int UB（复核逮到）
    // ---------------------------------------------------------------------------
    void TestNonFinite()
    {
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();

        SpatialHash hash(1.0f);
        hash.Insert(1, MakeAabb(0.0f, 0.0f, inf, inf));  // 非有限 → 忽略
        hash.Insert(2, MakeAabb(nan, 0.0f, 1.0f, 1.0f)); // 非有限 → 忽略
        assert(hash.Size() == 0 && "非有限 bounds 被拒绝");

        hash.Insert(3, MakeAabb(0.0f, 0.0f, 1.0f, 1.0f)); // 正常
        assert(hash.Size() == 1);
        // 非有限查询区 / 半径 / 圆心 → 空（不崩、不 UB）。
        assert(hash.QueryAABB(MakeAabb(0.0f, 0.0f, inf, inf)).empty());
        assert(hash.QueryRadius(glm::vec2{0.0f, 0.0f}, inf).empty());
        assert(hash.QueryRadius(glm::vec2{nan, 0.0f}, 1.0f).empty());
        assert(hash.QueryPoint(glm::vec2{inf, inf}).empty());
        std::printf("  [ok] TestNonFinite\n");
    }

} // namespace

int main()
{
    std::printf("SpatialHashTest:\n");
    TestBasicQuery();
    TestMultiCellDedup();
    TestQueryPoint();
    TestQueryRadius();
    TestNegativeCoords();
    TestRemove();
    TestUpdate();
    TestBoundaryTouch();
    TestClearAndEmpty();
    TestDegenerateCellSize();
    TestReversedBounds();
    TestOversizedItem();
    TestNonFinite();
    TestBruteForceCrossValidation();
    std::printf("SpatialHashTest: all passed.\n");
    return 0;
}
