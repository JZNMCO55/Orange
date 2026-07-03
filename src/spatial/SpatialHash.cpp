#include <orange/engine/spatial/SpatialHash.h>

#include <algorithm> // std::clamp / std::min / std::max
#include <cmath>     // std::floor / std::isfinite

namespace Orange::Engine::Spatial
{

    namespace
    {

        // 覆盖 cell 数上限：一个 item / 查询区覆盖超过此数的 cell 视为 oversized（相对 cellSize
        // 过大）。超限 item 不逐 cell 登记、超限查询区退化为扫全表——都为避免无界迭代 / OOM。
        constexpr long long kMaxCellsPerOp = 1LL << 16; // 65536（256×256）

        bool IsFinite(glm::vec2 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y);
        }

        bool IsFiniteAabb(const Aabb2D& b)
        {
            return IsFinite(b.lower) && IsFinite(b.upper);
        }

        // 世界坐标 → cell 整数坐标：floor(world/cs)，并 clamp 到 [-1e9,1e9]（⊂ int 范围）杜绝
        // 越界 float→int UB。绝对坐标超此域的 item / 查询会饱和到边界 cell——确定性 clamp 故
        // item 与查询同样饱和、仍能互相命中，只在极端域外损失分辨率（世界坐标应在 ±1e9*cellSize 内）。
        int CellCoord(float world, float cs)
        {
            double d = std::floor(static_cast<double>(world) / static_cast<double>(cs));
            if (d > 1.0e9)
            {
                d = 1.0e9;
            }
            if (d < -1.0e9)
            {
                d = -1.0e9;
            }
            return static_cast<int>(d);
        }

        // 两个 int32 cell 坐标打包成 uint64 key（高 32 位 = cx、低 32 位 = cy）。先转 uint32
        // （按位模式保留负数补码）再拼；distinct (cx,cy) 不碰撞。
        std::uint64_t CellKey(int cx, int cy)
        {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
                   static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy));
        }

        // 把 bounds 规范化成 lower<=upper（每分量取 min/max）。反序 / 退化输入若不规范化，覆盖
        // 遍历的 cx0..cx1 双层循环会因起点>终点跳空、漏掉本应覆盖的 cell。
        Aabb2D Normalize(const Aabb2D& b)
        {
            Aabb2D out;
            out.lower.x = std::min(b.lower.x, b.upper.x);
            out.lower.y = std::min(b.lower.y, b.upper.y);
            out.upper.x = std::max(b.lower.x, b.upper.x);
            out.upper.y = std::max(b.lower.y, b.upper.y);
            return out;
        }

        // （已规范化 + 有限的）AABB 覆盖多少个 cell（int64 防乘积溢出）。CellCoord 已 clamp 到
        // ±1e9，故每轴 span <= 2e9+1、乘积 <= ~4e18 < LLONG_MAX，安全。
        long long CoveredCellCount(const Aabb2D& bounds, float cs)
        {
            const long long spanX =
                static_cast<long long>(CellCoord(bounds.upper.x, cs)) - CellCoord(bounds.lower.x, cs) + 1;
            const long long spanY =
                static_cast<long long>(CellCoord(bounds.upper.y, cs)) - CellCoord(bounds.lower.y, cs) + 1;
            return spanX * spanY;
        }

        // 对一个（已规范化的）AABB 覆盖到的每个 cell 调用 fn(key)。仅在 CoveredCellCount<=上限
        // 时调用，故迭代次数有界。cx0..cx1 / cy0..cy1 含端点——AABB 边缘落在 cell 边界上时该
        // cell 也算覆盖。
        template <typename Fn>
        void ForEachCoveredCell(const Aabb2D& bounds, float cs, Fn&& fn)
        {
            const int cx0 = CellCoord(bounds.lower.x, cs);
            const int cy0 = CellCoord(bounds.lower.y, cs);
            const int cx1 = CellCoord(bounds.upper.x, cs);
            const int cy1 = CellCoord(bounds.upper.y, cs);
            for (int cy = cy0; cy <= cy1; ++cy)
            {
                for (int cx = cx0; cx <= cx1; ++cx)
                {
                    fn(CellKey(cx, cy));
                }
            }
        }

    } // namespace

    SpatialHash::SpatialHash(float cellSize)
    {
        // cellSize<=0 非法：钳到合理默认 1.0（不钳极小值——否则正常尺寸 item 覆盖天文级 cell 爆炸）。
        mCellSize = cellSize > 0.0f ? cellSize : 1.0f;
    }

    void SpatialHash::Insert(std::uint64_t id, const Aabb2D& bounds)
    {
        // 非有限 bounds（NaN/inf）无法空间索引——直接忽略。
        if (!IsFiniteAabb(bounds))
        {
            return;
        }

        // 已存在 → 先按旧 bounds / oversized 状态摘除干净，再重新登记（等价 Update）。
        if (mItems.count(id) != 0)
        {
            Remove(id);
        }

        const Aabb2D norm = Normalize(bounds);
        mItems[id]        = norm;

        if (CoveredCellCount(norm, mCellSize) > kMaxCellsPerOp)
        {
            // oversized：不逐 cell 登记（否则无界迭代），登记进 oversized 集，查询时统一扫。
            mOversized.insert(id);
        }
        else
        {
            ForEachCoveredCell(norm, mCellSize, [&](std::uint64_t key)
                               { mCells[key].push_back(id); });
        }
    }

    void SpatialHash::Update(std::uint64_t id, const Aabb2D& bounds)
    {
        // Insert 内部已处理"已存在则先摘除"，直接转发即可保证按旧 bounds / oversized 摘干净。
        Insert(id, bounds);
    }

    void SpatialHash::Remove(std::uint64_t id)
    {
        const auto it = mItems.find(id);
        if (it == mItems.end())
        {
            return; // 不存在 no-op
        }

        const auto ovIt = mOversized.find(id);
        if (ovIt != mOversized.end())
        {
            // oversized item 不在 cell 里，只从 oversized 集摘。
            mOversized.erase(ovIt);
        }
        else
        {
            // 用**存储的旧 bounds**（it->second，已规范化）算出登记时覆盖的 cell，逐 cell
            // 线性查找 swap-remove。命中即 break。移除后 cell 空则连 key 一起删，控内存。
            ForEachCoveredCell(it->second, mCellSize, [&](std::uint64_t key)
                               {
            const auto cellIt = mCells.find(key);
            if (cellIt == mCells.end())
            {
                return;
            }
            std::vector<std::uint64_t>& ids = cellIt->second;
            for (std::size_t i = 0; i < ids.size(); ++i)
            {
                if (ids[i] == id)
                {
                    ids[i] = ids.back();
                    ids.pop_back();
                    break;
                }
            }
            if (ids.empty())
            {
                mCells.erase(cellIt);
            } });
        }

        mItems.erase(it);
    }

    void SpatialHash::Clear()
    {
        mItems.clear();
        mCells.clear();
        mOversized.clear();
    }

    bool SpatialHash::Contains(std::uint64_t id) const
    {
        return mItems.count(id) != 0;
    }

    std::size_t SpatialHash::Size() const
    {
        return mItems.size();
    }

    std::vector<std::uint64_t> SpatialHash::QueryAABB(const Aabb2D& area) const
    {
        // 非有限查询区无意义 → 空。
        if (!IsFiniteAabb(area))
        {
            return {};
        }
        const Aabb2D norm = Normalize(area);

        std::vector<std::uint64_t>              out;
        std::unordered_map<std::uint64_t, char> seen; // dedup（跨多格 item + oversized 集）

        if (CoveredCellCount(norm, mCellSize) > kMaxCellsPerOp)
        {
            // 超大查询区 → 退化为扫全表 narrow（正确且 O(n) 有界，不产生无界 cell 迭代）。
            // 全表已含 oversized item，无需另扫。
            for (const auto& kv : mItems)
            {
                if (AabbOverlap(kv.second, norm))
                {
                    out.push_back(kv.first);
                }
            }
            return out;
        }

        // 正常：cell 收集候选 + dedup + narrow。
        ForEachCoveredCell(norm, mCellSize, [&](std::uint64_t key)
                           {
        const auto cellIt = mCells.find(key);
        if (cellIt == mCells.end())
        {
            return;
        }
        for (const std::uint64_t id : cellIt->second)
        {
            if (!seen.emplace(id, char{0}).second)
            {
                continue;  // 已处理过该 id
            }
            const auto itemIt = mItems.find(id);
            if (itemIt != mItems.end() && AabbOverlap(itemIt->second, norm))
            {
                out.push_back(id);
            }
        } });

        // oversized item 不在 cell 里，须每次查询统一 narrow 扫。
        for (const std::uint64_t id : mOversized)
        {
            if (!seen.emplace(id, char{0}).second)
            {
                continue;
            }
            const auto itemIt = mItems.find(id);
            if (itemIt != mItems.end() && AabbOverlap(itemIt->second, norm))
            {
                out.push_back(id);
            }
        }

        return out;
    }

    std::vector<std::uint64_t> SpatialHash::QueryPoint(glm::vec2 point) const
    {
        return QueryAABB(Aabb2D{point, point});
    }

    std::vector<std::uint64_t> SpatialHash::QueryRadius(glm::vec2 center, float radius) const
    {
        // radius<=0 / 非有限 radius / 非有限圆心 → 空（!(radius>0) 也吞掉 NaN）。
        if (!(radius > 0.0f) || !std::isfinite(radius) || !IsFinite(center))
        {
            return {};
        }

        // broad-phase：圆的包围盒（center 与 radius 均有限，box 亦有限）。
        const Aabb2D box{center - glm::vec2{radius, radius}, center + glm::vec2{radius, radius}};
        const float  r2 = radius * radius;

        // 圆 vs AABB narrow：圆心到 AABB 最近点（clamp 到盒内）距离² <= r² 才命中。
        const auto circleHit = [&](const Aabb2D& aabb) -> bool
        {
            const float qx = std::clamp(center.x, aabb.lower.x, aabb.upper.x);
            const float qy = std::clamp(center.y, aabb.lower.y, aabb.upper.y);
            const float dx = center.x - qx;
            const float dy = center.y - qy;
            return dx * dx + dy * dy <= r2;
        };

        std::vector<std::uint64_t>              out;
        std::unordered_map<std::uint64_t, char> seen;

        if (CoveredCellCount(box, mCellSize) > kMaxCellsPerOp)
        {
            for (const auto& kv : mItems)
            {
                if (circleHit(kv.second))
                {
                    out.push_back(kv.first);
                }
            }
            return out;
        }

        ForEachCoveredCell(box, mCellSize, [&](std::uint64_t key)
                           {
        const auto cellIt = mCells.find(key);
        if (cellIt == mCells.end())
        {
            return;
        }
        for (const std::uint64_t id : cellIt->second)
        {
            if (!seen.emplace(id, char{0}).second)
            {
                continue;
            }
            const auto itemIt = mItems.find(id);
            if (itemIt != mItems.end() && circleHit(itemIt->second))
            {
                out.push_back(id);
            }
        } });

        for (const std::uint64_t id : mOversized)
        {
            if (!seen.emplace(id, char{0}).second)
            {
                continue;
            }
            const auto itemIt = mItems.find(id);
            if (itemIt != mItems.end() && circleHit(itemIt->second))
            {
                out.push_back(id);
            }
        }

        return out;
    }

} // namespace Orange::Engine::Spatial
