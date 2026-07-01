#ifndef ORANGE_ENGINE_TILEMAP_TILE_MAP_H
#define ORANGE_ENGINE_TILEMAP_TILE_MAP_H

// ---------------------------------------------------------------------------
// TileMap —— 2D 瓦片地图数据 + header-only inline helper。
//
// 单层 tilemap：每个 cell 存一个 tile ID（0 = 空 / 无瓦片），行主序索引
// y*width+x。tileset 把 tile ID 映到"是否实心（solid，即碰撞 / 阻挡）"。这是
// 2D 引擎核心内容原语（Godot / Unity Tilemap 同款）——数据模型放这里，贪心
// 碰撞矩形合并见 TileMapCollision.h，烘焙 NavGrid 见 TileMapNavBake.h。
//
// helper 全部 header-only inline，故不标 ORANGE_ENGINE_API（导出宏只加在需要
// 跨 DLL 边界的非 inline 符号上）。公共头只依赖 glm + 标准库。
// ---------------------------------------------------------------------------

#include <glm/common.hpp>   // glm::floor
#include <glm/vec2.hpp>     // glm::vec2 / glm::ivec2

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Orange::Engine::Tilemap
{

// 一层瓦片：每 cell 一个 tile ID（0 = 空 / 无瓦片），行主序 y*width+x。
struct TileMap
{
    int                       width{0};
    int                       height{0};
    float                     tileSize{1.0f};
    glm::vec2                 origin{0.0f, 0.0f};   // 世界坐标：tile(0,0) 左下角
    std::vector<std::int32_t> tiles;                // size = width*height，0 = 空
};

// 瓦片集：tile ID → 是否实心（碰撞 / 阻挡）。solidById[id] 非 0 即实心；越界
// ID / id<=0（空瓦片）一律视为非实心。
struct TileSet
{
    std::vector<std::uint8_t> solidById;  // 索引 = tile ID
};

// ECS 组件：实体携带一张 tilemap + tileset。纯数据，v1 不序列化。
struct TileMapComponent
{
    TileMap map;
    TileSet tileset;
};

// 构造一张全空（tiles 全 0）的 tilemap。width / height <= 0 → 返回空地图
// （width/height 保持 0、tiles 为空），避免负 / 零尺寸 vector 分配。
inline TileMap MakeTileMap(int width, int height, float tileSize, glm::vec2 origin)
{
    TileMap map;
    map.tileSize = tileSize;
    map.origin   = origin;
    if (width <= 0 || height <= 0)
    {
        return map;
    }
    map.width  = width;
    map.height = height;
    map.tiles.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                     static_cast<std::int32_t>(0));
    return map;
}

// tile 坐标是否落在地图内。
inline bool InBounds(const TileMap& map, int x, int y)
{
    return x >= 0 && y >= 0 && x < map.width && y < map.height;
}

// 取 (x,y) 处的 tile ID。越界 → 0（空）。
inline std::int32_t GetTile(const TileMap& map, int x, int y)
{
    if (!InBounds(map, x, y))
    {
        return 0;
    }
    const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width) +
                            static_cast<std::size_t>(x);
    // 防御：tiles 与 width*height 不同步的畸形 map（如手工构造 POD 未 resize / 部分加载）
    // 优雅退化为空 tile，而非堆越界读。正常 MakeTileMap 路径二者恒等长。
    if (idx >= map.tiles.size())
    {
        return 0;
    }
    return map.tiles[idx];
}

// 设置 (x,y) 处的 tile ID。越界 → no-op。
inline void SetTile(TileMap& map, int x, int y, std::int32_t id)
{
    if (!InBounds(map, x, y))
    {
        return;
    }
    const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width) +
                            static_cast<std::size_t>(x);
    // 防御：畸形 map（tiles 短于 width*height）→ no-op 而非堆越界写（同 GetTile）。
    if (idx >= map.tiles.size())
    {
        return;
    }
    map.tiles[idx] = id;
}

// (x,y) 处的 tile 是否实心（碰撞 / 阻挡）。空瓦片（id<=0）恒非实心；tileset 里
// 未登记（越界 ID）也视为非实心（安全默认，未知瓦片不阻挡）。
inline bool IsSolidTile(const TileMap& map, const TileSet& tileset, int x, int y)
{
    const std::int32_t id = GetTile(map, x, y);
    if (id <= 0)
    {
        return false;
    }
    if (id < static_cast<std::int32_t>(tileset.solidById.size()))
    {
        return tileset.solidById[static_cast<std::size_t>(id)] != 0;
    }
    return false;
}

// tile 中心的世界坐标：origin + (vec2(x,y) + 0.5) * tileSize。
inline glm::vec2 TileCenterToWorld(const TileMap& map, int x, int y)
{
    return map.origin +
           (glm::vec2{static_cast<float>(x), static_cast<float>(y)} + 0.5f) * map.tileSize;
}

// 世界坐标 → tile 坐标：floor((world - origin) / tileSize)。结果可能越界，调用
// 方用 InBounds / GetTile 判定。tileSize <= 0（退化地图）→ 返回 (0,0)。
inline glm::ivec2 WorldToTile(const TileMap& map, glm::vec2 world)
{
    if (map.tileSize <= 0.0f)
    {
        return glm::ivec2{0, 0};
    }
    const glm::vec2 local   = (world - map.origin) / map.tileSize;
    const glm::vec2 floored = glm::floor(local);
    return glm::ivec2{static_cast<int>(floored.x), static_cast<int>(floored.y)};
}

}  // namespace Orange::Engine::Tilemap

#endif  // ORANGE_ENGINE_TILEMAP_TILE_MAP_H
