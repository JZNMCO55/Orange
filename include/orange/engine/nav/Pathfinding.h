#ifndef ORANGE_ENGINE_NAV_PATHFINDING_H
#define ORANGE_ENGINE_NAV_PATHFINDING_H

// ---------------------------------------------------------------------------
// Pathfinding —— NavGrid 上的 A* 网格寻路 + line-of-sight 路径平滑。
//
// FindPath 用 A*（4- 或 8-邻接可选）在均匀网格上求最短路，返回各经过 cell
// 中心的世界坐标路点。SimplifyPath 用 LOS 弦拉（string pulling）把锯齿路径
// 化简为更少的直线段。二者均无后端依赖（纯 NavGrid + glm + std）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/nav/NavGrid.h>

#include <glm/vec2.hpp>

#include <vector>

namespace Orange::Engine::Nav
{

// A* 网格寻路。返回从 start 到 goal 的路径路点（各经过 cell 的中心世界坐标，
// 含起终 cell）。无路 / start 或 goal 越界或阻挡 → 返回空 vector。
// allowDiagonal=true 走 8-邻接（八方向，对角禁切角）；false 走 4-邻接。
ORANGE_ENGINE_API std::vector<glm::vec2> FindPath(const NavGrid& grid, glm::vec2 startWorld,
                                                  glm::vec2 goalWorld, bool allowDiagonal = true);

// line-of-sight 弦拉平滑（string pulling）：去掉能被直线（穿过可走 cell）跨越
// 的中间路点，把锯齿路径化简为更少的直线段。空 / 单点 / 双点原样返回。
ORANGE_ENGINE_API std::vector<glm::vec2> SimplifyPath(const NavGrid& grid,
                                                      const std::vector<glm::vec2>& path);

}  // namespace Orange::Engine::Nav

#endif  // ORANGE_ENGINE_NAV_PATHFINDING_H
