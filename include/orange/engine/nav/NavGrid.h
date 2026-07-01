#ifndef ORANGE_ENGINE_NAV_NAV_GRID_H
#define ORANGE_ENGINE_NAV_NAV_GRID_H

// ---------------------------------------------------------------------------
// NavGrid —— 2D 均匀网格导航数据 + header-only inline helper。
//
// 每个 cell 记 1 = 可走(walkable) / 0 = 阻挡(blocked)；cell (x,y) 的行主序
// 索引为 y*width+x。越界一律视为不可走（安全默认，寻路 / LOS 沿线采样撞到
// 网格外自然被当成墙）。本文件是纯数据 + 世界坐标 ↔ cell 坐标换算 / 边界
// 判定的 inline helper，不依赖任何后端；A* 网格寻路见 Pathfinding.h，从
// 物理世界烘焙可行走性见 NavGridBake.h。
//
// helper 全部 header-only inline，故不标 ORANGE_ENGINE_API（导出宏只加在
// 需要跨 DLL 边界的非 inline 符号上）。
// ---------------------------------------------------------------------------

#include <glm/common.hpp>   // glm::floor
#include <glm/vec2.hpp>     // glm::vec2 / glm::ivec2

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Orange::Engine::Nav
{

struct NavGrid
{
    int                       width{0};
    int                       height{0};
    float                     cellSize{1.0f};
    glm::vec2                 origin{0.0f, 0.0f};   // 世界坐标：cell(0,0) 左下角
    std::vector<std::uint8_t> walkable;             // size = width*height；越界视为不可走
};

// 构造一个全可走网格（walkable 全 1）。width / height <= 0 → 返回空网格
// （width/height 保持 0、walkable 为空），避免负 / 零尺寸 vector 分配。
inline NavGrid MakeNavGrid(int width, int height, float cellSize, glm::vec2 origin)
{
    NavGrid grid;
    grid.cellSize = cellSize;
    grid.origin   = origin;
    if (width <= 0 || height <= 0)
    {
        return grid;
    }
    grid.width  = width;
    grid.height = height;
    grid.walkable.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                         static_cast<std::uint8_t>(1));
    return grid;
}

// cell 坐标是否落在网格内。
inline bool InBounds(const NavGrid& grid, int x, int y)
{
    return x >= 0 && y >= 0 && x < grid.width && y < grid.height;
}

// cell 是否可走。越界 → false（越界即墙）。
inline bool IsWalkable(const NavGrid& grid, int x, int y)
{
    if (!InBounds(grid, x, y))
    {
        return false;
    }
    const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) +
                            static_cast<std::size_t>(x);
    return grid.walkable[idx] != 0;
}

// 设置 cell 可走性。越界 → no-op。
inline void SetWalkable(NavGrid& grid, int x, int y, bool walkable)
{
    if (!InBounds(grid, x, y))
    {
        return;
    }
    const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) +
                            static_cast<std::size_t>(x);
    grid.walkable[idx] = walkable ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
}

// 世界坐标 → cell 坐标：floor((world - origin) / cellSize)。结果可能越界，
// 调用方用 InBounds / IsWalkable 判定。cellSize <= 0（退化网格）→ 返回 (0,0)。
inline glm::ivec2 WorldToCell(const NavGrid& grid, glm::vec2 world)
{
    if (grid.cellSize <= 0.0f)
    {
        return glm::ivec2{0, 0};
    }
    const glm::vec2 local   = (world - grid.origin) / grid.cellSize;
    const glm::vec2 floored = glm::floor(local);
    return glm::ivec2{static_cast<int>(floored.x), static_cast<int>(floored.y)};
}

// cell 中心的世界坐标：origin + (vec2(x,y) + 0.5) * cellSize。
inline glm::vec2 CellCenterToWorld(const NavGrid& grid, int x, int y)
{
    return grid.origin +
           (glm::vec2{static_cast<float>(x), static_cast<float>(y)} + 0.5f) * grid.cellSize;
}

// cell 的世界 AABB：lower = origin + (x,y)*cellSize，upper = origin + (x+1,y+1)*cellSize。
inline void CellBoundsWorld(const NavGrid& grid, int x, int y, glm::vec2& lowerOut, glm::vec2& upperOut)
{
    lowerOut = grid.origin +
               glm::vec2{static_cast<float>(x), static_cast<float>(y)} * grid.cellSize;
    upperOut = grid.origin +
               glm::vec2{static_cast<float>(x + 1), static_cast<float>(y + 1)} * grid.cellSize;
}

}  // namespace Orange::Engine::Nav

#endif  // ORANGE_ENGINE_NAV_NAV_GRID_H
