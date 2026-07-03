#include <orange/engine/tilemap/TileMapCollision.h>

#include <cstddef>
#include <cstdint>

namespace Orange::Engine::Tilemap
{

    std::vector<TileRect> MergeSolidTiles(const TileMap& map, const TileSet& tileset)
    {
        std::vector<TileRect> rects;

        const int w = map.width;
        const int h = map.height;
        if (w <= 0 || h <= 0)
        {
            return rects;
        }

        // covered[y*w+x]：该 tile 是否已被某个已 emit 的矩形吞掉。行主序遍历，
        // 保证每个实心 tile 只被最先遇到（左下优先）的矩形吞一次。
        std::vector<std::uint8_t> covered(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
                                          static_cast<std::uint8_t>(0));

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                        static_cast<std::size_t>(x);
                if (covered[idx] != 0 || !IsSolidTile(map, tileset, x, y))
                {
                    continue;
                }

                // 向右扩：沿本行吃连续的、未被吞的实心 tile。
                int rw = 1;
                while (x + rw < w && IsSolidTile(map, tileset, x + rw, y) &&
                       covered[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                               static_cast<std::size_t>(x + rw)] == 0)
                {
                    ++rw;
                }

                // 向下扩：只有整整一行 rw 宽都可用（全实心且未吞）才把该行并进来，
                // 否则停下——这样合出的始终是完整矩形（贪心 meshing 的关键约束）。
                int  rh  = 1;
                bool can = true;
                while (y + rh < h && can)
                {
                    for (int dx = 0; dx < rw; ++dx)
                    {
                        if (!IsSolidTile(map, tileset, x + dx, y + rh) ||
                            covered[static_cast<std::size_t>(y + rh) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(x + dx)] != 0)
                        {
                            can = false;
                            break;
                        }
                    }
                    if (can)
                    {
                        ++rh;
                    }
                }

                // 标记这块 rw×rh 矩形吞掉的所有 tile。
                for (int dy = 0; dy < rh; ++dy)
                {
                    for (int dx = 0; dx < rw; ++dx)
                    {
                        covered[static_cast<std::size_t>(y + dy) * static_cast<std::size_t>(w) +
                                static_cast<std::size_t>(x + dx)] = static_cast<std::uint8_t>(1);
                    }
                }

                rects.push_back(TileRect{x, y, rw, rh});
            }
        }

        return rects;
    }

    void TileRectToWorldAABB(const TileMap& map, const TileRect& rect, glm::vec2& lowerOut,
                             glm::vec2& upperOut)
    {
        lowerOut = map.origin +
                   glm::vec2{static_cast<float>(rect.x), static_cast<float>(rect.y)} * map.tileSize;
        upperOut = map.origin + glm::vec2{static_cast<float>(rect.x + rect.w),
                                          static_cast<float>(rect.y + rect.h)} *
                                    map.tileSize;
    }

} // namespace Orange::Engine::Tilemap
