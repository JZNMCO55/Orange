#ifndef ORANGE_ENGINE_TILEMAP_TILE_MAP_COLLISION_H
#define ORANGE_ENGINE_TILEMAP_TILE_MAP_COLLISION_H

// ---------------------------------------------------------------------------
// TileMapCollision —— 从 tilemap 的实心 tile 贪心合并出尽量少的碰撞矩形
// （classic greedy meshing）。逐 tile 建物理体会产生大量冗余小 AABB；把相邻
// 实心 tile 合成大矩形能大幅降低碰撞体数量，再 TileRectToWorldAABB 转成世界
// AABB 喂给 physics 建静态体。
//
// 公共头只依赖 glm + 标准库 + 引擎公共头（TileMap.h）。合并算法在 .cpp。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/tilemap/TileMap.h>

#include <glm/vec2.hpp>

#include <vector>

namespace Orange::Engine::Tilemap
{

    // 一块合并出的实心矩形（tile 坐标系：左下角 tile (x,y) + 尺寸 w×h 个 tile）。
    struct TileRect
    {
        int x{0};
        int y{0};
        int w{0};
        int h{0};
    };

    // 贪心合并 tilemap 的实心 tile 成尽量少的不重叠矩形（classic greedy meshing）。
    // 保证：每个实心 tile 被恰好一个矩形覆盖、矩形互不重叠；空 / 无实心 → 空 vector。
    ORANGE_ENGINE_API std::vector<TileRect> MergeSolidTiles(const TileMap& map, const TileSet& tileset);

    // TileRect → 世界 AABB（lower = 左下、upper = 右上；用于建物理静态体）。
    ORANGE_ENGINE_API void TileRectToWorldAABB(const TileMap& map, const TileRect& rect,
                                               glm::vec2& lowerOut, glm::vec2& upperOut);

} // namespace Orange::Engine::Tilemap

#endif // ORANGE_ENGINE_TILEMAP_TILE_MAP_COLLISION_H
