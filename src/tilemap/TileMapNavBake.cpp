#include <orange/engine/tilemap/TileMapNavBake.h>

namespace Orange::Engine::Tilemap
{

Nav::NavGrid BuildNavGridFromTileMap(const TileMap& map, const TileSet& tileset)
{
    // 网格与 tilemap 一 tile 一 cell 对齐：同 width / height / cellSize / origin。
    // MakeNavGrid 先造全可走网格（width/height<=0 时返回空网格）。
    Nav::NavGrid grid = Nav::MakeNavGrid(map.width, map.height, map.tileSize, map.origin);

    // 实心 tile → 阻挡（不可走），其余保持可走。SetWalkable 越界 no-op，故空网格
    // 下这层遍历不会执行（width/height=0）。
    for (int y = 0; y < map.height; ++y)
    {
        for (int x = 0; x < map.width; ++x)
        {
            Nav::SetWalkable(grid, x, y, !IsSolidTile(map, tileset, x, y));
        }
    }

    return grid;
}

}  // namespace Orange::Engine::Tilemap
