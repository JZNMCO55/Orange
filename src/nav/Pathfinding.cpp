#include <orange/engine/nav/Pathfinding.h>

#include <orange/engine/nav/NavGrid.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace Orange::Engine::Nav
{

    namespace
    {

        // √2 —— 对角步进的 cost；也用于 octile heuristic。
        constexpr float kSqrt2 = 1.41421356f;

        // A* open set 的堆元素：f = g + h（优先级）+ cell 行主序索引。
        struct OpenNode
        {
            float f;
            int   cell;
        };

        // std::priority_queue 默认最大堆；本比较器让 f 最小者排在堆顶（min-heap）。
        // 方向搞反会让 A* 退化成"最大 f 优先"，产出非最短 / 乱序路径 —— 由测试的
        // 相邻性 + 绕障断言兜底。
        struct OpenNodeGreater
        {
            bool operator()(const OpenNode& a, const OpenNode& b) const
            {
                return a.f > b.f;
            }
        };

        // admissible heuristic：
        //   * allowDiagonal → octile 距离 h = (dx + dy) + (√2 - 2) * min(dx, dy)
        //   * 4-邻接        → Manhattan 距离 h = dx + dy
        // 两者都不高估真实最短代价，保证 A* 得最短路。
        float Heuristic(int x, int y, int gx, int gy, bool allowDiagonal)
        {
            const int   adx = x > gx ? x - gx : gx - x;
            const int   ady = y > gy ? y - gy : gy - y;
            const float dx  = static_cast<float>(adx);
            const float dy  = static_cast<float>(ady);
            if (allowDiagonal)
            {
                return (dx + dy) + (kSqrt2 - 2.0f) * std::min(dx, dy);
            }
            return dx + dy;
        }

        // line-of-sight 检查：沿 a→b 做 supercover 网格穿越（Amanatides-Woo DDA），访问线段
        // 经过的**每个** cell，任一阻挡 → 无 LOS。且对"线段正好穿过网格顶点（对角穿越）"的
        // 情形，要求该顶点两侧的两个正交 cell 也可走——与 FindPath 的禁切角策略一致。
        //
        // 为何不用点采样：以 cellSize/2 步长采样会漏检"以浅角擦过某 cell 一角、cell 内弦长 <
        // 采样间距"的情形（尤其纯对角擦过两块斜对角墙之间的顶点），从而 false-positive 误判
        // "可见"，令 SimplifyPath 把绕角路径塌成擦墙角的对角段、重新引入 FindPath 禁止的切角。
        // 精确格穿越 + 顶点禁切角消除该隐患。
        bool HasLineOfSight(const NavGrid& grid, glm::vec2 aWorld, glm::vec2 bWorld)
        {
            if (grid.cellSize <= 0.0f)
            {
                return false;
            }
            // 转 cell-fractional 坐标（cell(cx,cy) 占 [cx,cx+1)×[cy,cy+1)）。
            const float inv = 1.0f / grid.cellSize;
            const float ax  = (aWorld.x - grid.origin.x) * inv;
            const float ay  = (aWorld.y - grid.origin.y) * inv;
            const float bx  = (bWorld.x - grid.origin.x) * inv;
            const float by  = (bWorld.y - grid.origin.y) * inv;

            int       cx = static_cast<int>(std::floor(ax));
            int       cy = static_cast<int>(std::floor(ay));
            const int ex = static_cast<int>(std::floor(bx));
            const int ey = static_cast<int>(std::floor(by));

            if (!IsWalkable(grid, cx, cy))
            {
                return false;
            }

            const float ddx   = bx - ax;
            const float ddy   = by - ay;
            const int   stepX = (ddx > 0.0f) ? 1 : (ddx < 0.0f ? -1 : 0);
            const int   stepY = (ddy > 0.0f) ? 1 : (ddy < 0.0f ? -1 : 0);

            // tMax：到下一条 x / y 网格线的参数 t；tDelta：跨一整个 cell 的 t。
            const float kInf    = std::numeric_limits<float>::infinity();
            float       tMaxX   = kInf;
            float       tDeltaX = kInf;
            if (stepX != 0)
            {
                const float nextBoundary = (stepX > 0) ? (std::floor(ax) + 1.0f) : std::floor(ax);
                tMaxX                    = (nextBoundary - ax) / ddx;
                tDeltaX                  = std::fabs(1.0f / ddx);
            }
            float tMaxY   = kInf;
            float tDeltaY = kInf;
            if (stepY != 0)
            {
                const float nextBoundary = (stepY > 0) ? (std::floor(ay) + 1.0f) : std::floor(ay);
                tMaxY                    = (nextBoundary - ay) / ddy;
                tDeltaY                  = std::fabs(1.0f / ddy);
            }

            const float eps      = 1e-6f;
            const int   maxSteps = 2 * (grid.width + grid.height) + 4; // 防御性上界（浮点病态防死循环）
            for (int guard = 0; (cx != ex || cy != ey) && guard < maxSteps; ++guard)
            {
                if (tMaxX < tMaxY - eps)
                {
                    cx += stepX;
                    tMaxX += tDeltaX;
                }
                else if (tMaxY < tMaxX - eps)
                {
                    cy += stepY;
                    tMaxY += tDeltaY;
                }
                else
                {
                    // 对角穿越网格顶点（tMaxX≈tMaxY）：禁切角——顶点两侧正交 cell 都须可走。
                    if (!IsWalkable(grid, cx + stepX, cy) || !IsWalkable(grid, cx, cy + stepY))
                    {
                        return false;
                    }
                    cx += stepX;
                    cy += stepY;
                    tMaxX += tDeltaX;
                    tMaxY += tDeltaY;
                }
                if (!IsWalkable(grid, cx, cy))
                {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    std::vector<glm::vec2> FindPath(const NavGrid& grid, glm::vec2 startWorld,
                                    glm::vec2 goalWorld, bool allowDiagonal)
    {
        std::vector<glm::vec2> result;

        if (grid.width <= 0 || grid.height <= 0)
        {
            return result;
        }
        // 超大网格：cell 行主序索引 (y*width+x) 用 int 计算，width*height 溢出 INT_MAX
        // 会让索引回绕为负 → 越界 UB。拒绝（现实 nav grid 远达不到 ~46341²）。
        if (static_cast<std::int64_t>(grid.width) * static_cast<std::int64_t>(grid.height) >
            static_cast<std::int64_t>(std::numeric_limits<int>::max()))
        {
            return result;
        }

        const glm::ivec2 startCell = WorldToCell(grid, startWorld);
        const glm::ivec2 goalCell  = WorldToCell(grid, goalWorld);

        // start / goal 越界或落在阻挡 cell → 无路。
        if (!IsWalkable(grid, startCell.x, startCell.y) ||
            !IsWalkable(grid, goalCell.x, goalCell.y))
        {
            return result;
        }

        const int width  = grid.width;
        const int height = grid.height;

        const int startIdx = startCell.y * width + startCell.x;
        const int goalIdx  = goalCell.y * width + goalCell.x;

        // start 与 goal 同 cell → 单路点（起点中心）。
        if (startIdx == goalIdx)
        {
            result.push_back(CellCenterToWorld(grid, startCell.x, startCell.y));
            return result;
        }

        const std::size_t cellCount =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        std::vector<float>        gScore(cellCount, std::numeric_limits<float>::infinity());
        std::vector<int>          cameFrom(cellCount, -1);
        std::vector<std::uint8_t> closed(cellCount, 0);

        std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> open;

        gScore[static_cast<std::size_t>(startIdx)] = 0.0f;
        open.push(
            {Heuristic(startCell.x, startCell.y, goalCell.x, goalCell.y, allowDiagonal), startIdx});

        struct Dir
        {
            int   dx;
            int   dy;
            float cost;
        };
        // 4 正交（cost 1）+ 4 对角（cost √2）。对角仅在 allowDiagonal 时启用，
        // 且受"禁切角"约束（见下）。
        static const Dir kOrtho[4] = {
            {1, 0, 1.0f},
            {-1, 0, 1.0f},
            {0, 1, 1.0f},
            {0, -1, 1.0f},
        };
        static const Dir kDiag[4] = {
            {1, 1, kSqrt2},
            {1, -1, kSqrt2},
            {-1, 1, kSqrt2},
            {-1, -1, kSqrt2},
        };

        bool found = false;
        while (!open.empty())
        {
            const OpenNode current = open.top();
            open.pop();
            const int idx = current.cell;

            // 首次弹出 goal 即最优（consistent heuristic），停止。
            if (idx == goalIdx)
            {
                found = true;
                break;
            }

            // lazy deletion：同一 cell 可能以过期（较大 f）条目多次入堆，
            // 已 close 的直接丢弃。
            if (closed[static_cast<std::size_t>(idx)])
            {
                continue;
            }
            closed[static_cast<std::size_t>(idx)] = 1;

            const int cx = idx % width;
            const int cy = idx / width;

            // relax 一个邻接 cell：可走 + 未 close + 更优 g 才松弛并入堆。
            const auto relax = [&](int nx, int ny, float stepCost)
            {
                if (!IsWalkable(grid, nx, ny))
                {
                    return;
                }
                const int nIdx = ny * width + nx;
                if (closed[static_cast<std::size_t>(nIdx)])
                {
                    return;
                }
                const float tentative = gScore[static_cast<std::size_t>(idx)] + stepCost;
                if (tentative < gScore[static_cast<std::size_t>(nIdx)])
                {
                    gScore[static_cast<std::size_t>(nIdx)]   = tentative;
                    cameFrom[static_cast<std::size_t>(nIdx)] = idx;
                    const float h                            = Heuristic(nx, ny, goalCell.x, goalCell.y, allowDiagonal);
                    open.push({tentative + h, nIdx});
                }
            };

            for (const auto& d : kOrtho)
            {
                relax(cx + d.dx, cy + d.dy, d.cost);
            }
            if (allowDiagonal)
            {
                for (const auto& d : kDiag)
                {
                    // 禁切角（no corner cutting）：对角 (dx,dy) 仅当两个相邻正交
                    // cell (x+dx,y) 与 (x,y+dy) 都可走时才允许 —— 否则对角步会从
                    // 墙角"擦过"穿墙。
                    if (IsWalkable(grid, cx + d.dx, cy) && IsWalkable(grid, cx, cy + d.dy))
                    {
                        relax(cx + d.dx, cy + d.dy, d.cost);
                    }
                }
            }
        }

        if (!found)
        {
            return result;
        }

        // 经 cameFrom 回溯 cell 链（goal → start），反转成 start → goal。
        std::vector<int> chain;
        for (int idx = goalIdx; idx != -1; idx = cameFrom[static_cast<std::size_t>(idx)])
        {
            chain.push_back(idx);
            if (idx == startIdx)
            {
                break;
            }
        }
        std::reverse(chain.begin(), chain.end());

        result.reserve(chain.size());
        for (const int idx : chain)
        {
            const int cx = idx % width;
            const int cy = idx / width;
            result.push_back(CellCenterToWorld(grid, cx, cy));
        }
        return result;
    }

    std::vector<glm::vec2> SimplifyPath(const NavGrid& grid, const std::vector<glm::vec2>& path)
    {
        // 空 / 单点 / 双点无可化简，原样返回。
        if (path.size() <= 2)
        {
            return path;
        }

        std::vector<glm::vec2> result;
        result.reserve(path.size());
        result.push_back(path.front());

        std::size_t anchor = 0;
        while (anchor + 1 < path.size())
        {
            // 从最远点往回扫，找第一个与 anchor 有 LOS 的路点 = 最远可见路点。
            // 默认落到 anchor+1（原路径相邻路点的直线段必然可走，一定能推进）。
            std::size_t best = anchor + 1;
            for (std::size_t j = path.size() - 1; j > anchor + 1; --j)
            {
                if (HasLineOfSight(grid, path[anchor], path[j]))
                {
                    best = j;
                    break;
                }
            }
            result.push_back(path[best]);
            anchor = best;
        }

        return result;
    }

} // namespace Orange::Engine::Nav
