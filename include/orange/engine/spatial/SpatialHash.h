#ifndef ORANGE_ENGINE_SPATIAL_SPATIAL_HASH_H
#define ORANGE_ENGINE_SPATIAL_SPATIAL_HASH_H

// ---------------------------------------------------------------------------
// SpatialHash —— 均匀网格空间哈希 (uniform-grid spatial hash)：引擎首个空间加速
// 结构 (broad-phase)。把任意带 AABB 的 item（uint64 id）按 cellSize 均匀网格
// 索引，支持近邻规模的区域 / 点 / 半径查询——服务 AI 邻居查询、剔除 (culling)、
// 生成密度统计等。
//
// 跨多格的 item 登记进所有覆盖 cell；查询先按 cell 收集候选（broad-phase），再
// 做 narrow-phase 精确相交过滤掉 cell 粒度的假阳性 (false positive)，并 dedup
// （每 id 至多返回一次）。单线程用。
//
// Aabb2D + AabbOverlap 为 header-only inline helper，故不标 ORANGE_ENGINE_API；
// SpatialHash 类跨 DLL 边界，标 ORANGE_ENGINE_API。公共头只依赖 glm + 标准库 +
// 引擎公共导出头。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Orange::Engine::Spatial
{

    // 2D 轴对齐包围盒（lower=左下、upper=右上），与引擎 physics/nav/tilemap 的
    // lower/upper 约定一致。
    struct Aabb2D
    {
        glm::vec2 lower{0.0f, 0.0f};
        glm::vec2 upper{0.0f, 0.0f};
    };

    // 两 AABB 是否相交（含边界接触，用 <=）。
    inline bool AabbOverlap(const Aabb2D& a, const Aabb2D& b)
    {
        return a.lower.x <= b.upper.x && a.upper.x >= b.lower.x &&
               a.lower.y <= b.upper.y && a.upper.y >= b.lower.y;
    }

    // 均匀网格空间哈希 (uniform-grid spatial hash)：把任意带 AABB 的 item（uint64 id）
    // 索引进 cellSize 网格，支持近邻规模的区域 / 点 / 半径查询（broad-phase）。跨多格的
    // item 登记进所有覆盖 cell；查询做 narrow-phase 精确过滤 + dedup（每 id 至多返回
    // 一次）。单线程用。
    class ORANGE_ENGINE_API SpatialHash
    {
    public:
        // cellSize<=0（非法）钳到合理默认 1.0——不钳到极小值，否则正常尺寸 item 会覆盖天文
        // 级 cell 导致内存 / 时间爆炸。cellSize 应选在 item 典型尺寸量级。
        explicit SpatialHash(float cellSize);

        // 插入 / 更新 / 移除。Insert 已存在 id 等价 Update（先按旧 bounds 摘除再按新 bounds
        // 登记）。**bounds 须有限**（非有限 NaN/inf 无法空间索引，直接忽略）；覆盖 cell 数
        // 超上限（相对 cellSize 过大）的 item 走 oversized 路径——不登记 per-cell，改在每次
        // 查询时统一 narrow-phase 扫描，保证正确且不产生无界迭代。
        void        Insert(std::uint64_t id, const Aabb2D& bounds);
        void        Update(std::uint64_t id, const Aabb2D& bounds);
        void        Remove(std::uint64_t id); // 不存在 no-op
        void        Clear();
        bool        Contains(std::uint64_t id) const;
        std::size_t Size() const;

        // 查询：返回 bounds 与查询区**精确相交**（narrow-phase 过滤掉 cell 粒度假阳性）
        // 的去重 id 列表（顺序不保证）。
        std::vector<std::uint64_t> QueryAABB(const Aabb2D& area) const;
        std::vector<std::uint64_t> QueryPoint(glm::vec2 point) const;
        // 圆查询：item AABB 到圆心最近点距离 <= radius 才算命中（radius<=0 返回空）。
        std::vector<std::uint64_t> QueryRadius(glm::vec2 center, float radius) const;

    private:
        float                                                         mCellSize{1.0f};
        std::unordered_map<std::uint64_t, Aabb2D>                     mItems;     // id → bounds（Remove/Update 摘除用）
        std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> mCells;     // cellKey → ids（非 oversized）
        std::unordered_set<std::uint64_t>                             mOversized; // 覆盖 cell 数超上限的 item：不登记 cell，查询时总被扫
    };

} // namespace Orange::Engine::Spatial

#endif // ORANGE_ENGINE_SPATIAL_SPATIAL_HASH_H
