#ifndef ORANGE_ENGINE_TILEMAP_TILE_MAP_NAV_BAKE_H
#define ORANGE_ENGINE_TILEMAP_TILE_MAP_NAV_BAKE_H

// ---------------------------------------------------------------------------
// TileMapNavBake —— 从 tilemap 烘焙 NavGrid（tilemap → 可行走性网格）。实心
// tile 变阻挡 cell、其余可走；网格与 tilemap 一 tile 一 cell 对齐（同 width /
// height / cellSize / origin）。烘出的 NavGrid 直接喂 A* 寻路。
//
// 单独一个头隔离 nav 依赖：只有需要 tilemap→nav 桥接的 TU 才引 nav 公共头，
// 纯数据 / 碰撞用户不被牵连。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/nav/NavGrid.h>
#include <orange/engine/tilemap/TileMap.h>

namespace Orange::Engine::Tilemap
{

// 从 tilemap 生成 NavGrid：实心 tile → 阻挡（不可走）cell、其余 → 可走；网格
// width / height / cellSize / origin 对齐 tilemap（一 tile 一 cell）。
ORANGE_ENGINE_API Nav::NavGrid BuildNavGridFromTileMap(const TileMap& map, const TileSet& tileset);

}  // namespace Orange::Engine::Tilemap

#endif  // ORANGE_ENGINE_TILEMAP_TILE_MAP_NAV_BAKE_H
