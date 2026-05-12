#ifndef ORANGE_ENGINE_ASSET_TEXTURE_ASSET_H
#define ORANGE_ENGINE_ASSET_TEXTURE_ASSET_H

// ---------------------------------------------------------------------------
// TextureAsset —— 纹理资源的 CPU 数据容器。
//
// 当前阶段只承载一种 layout：8-bit unsigned R/G/B/A，按 row-major、左
// 上原点存储。其它格式（HDR / 压缩 BC / 多 mip / 立方贴图）随消费方
// 真正用到时再加，避免一次性把 TextureAsset 做成 super-set。
//
// 与 MeshAsset 同理：这里只保证像素字节正确入内存；GPU 端 image / view
// 的创建在 Render 模块完成。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace Orange::Engine::Asset
{

enum class TextureFormat : std::uint32_t
{
    Unknown = 0,
    R8G8B8A8_UNorm,
};

class ORANGE_ENGINE_API TextureAsset
{
public:
    TextureAsset() = default;

    TextureAsset(std::uint32_t width,
                 std::uint32_t height,
                 TextureFormat format,
                 std::vector<std::uint8_t> pixels)
        : mWidth(width)
        , mHeight(height)
        , mFormat(format)
        , mPixels(std::move(pixels))
    {
    }

    std::uint32_t Width()  const noexcept { return mWidth; }
    std::uint32_t Height() const noexcept { return mHeight; }
    TextureFormat Format() const noexcept { return mFormat; }
    const std::vector<std::uint8_t>& Pixels() const noexcept { return mPixels; }

    bool Empty() const noexcept { return mPixels.empty(); }

private:
    std::uint32_t mWidth{0};
    std::uint32_t mHeight{0};
    TextureFormat mFormat{TextureFormat::Unknown};
    std::vector<std::uint8_t> mPixels;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_TEXTURE_ASSET_H
