#ifndef ORANGE_ENGINE_TILEMAP_H
#define ORANGE_ENGINE_TILEMAP_H

// Tilemap 子系统便利聚合头：一次性引入 <orange/engine/tilemap/*> 全部公共头。
// 维护：tilemap/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/tilemap/TileMap.h>
#include <orange/engine/tilemap/TileMapCollision.h>
#include <orange/engine/tilemap/TileMapNavBake.h>

#endif // ORANGE_ENGINE_TILEMAP_H
