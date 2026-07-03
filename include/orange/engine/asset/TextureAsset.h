#ifndef ORANGE_ENGINE_ASSET_TEXTURE_ASSET_H
#define ORANGE_ENGINE_ASSET_TEXTURE_ASSET_H

// ---------------------------------------------------------------------------
// TextureAsset —— 纹理资源的 CPU 数据容器。
//
// 当前承载两种 layout：
//   * R8G8B8A8_UNorm   —— 8-bit unsigned RGBA，1 byte per channel = 4 bytes/px
//   * R32G32B32A32_Float —— IEEE-754 float RGBA，4 bytes per channel = 16 bytes/px
//
// 两种格式都按 row-major、左上原点存储；`mPixels` 始终是 byte buffer，
// 消费方按 `Format()` 决定 reinterpret 方式（float* 还是 uint8_t*）——这
// 样扩格式不破 ABI，扩格式仅需新增 enum 项 + loader 分支。
//
// HDR 路径：典型来源是 PolyHaven `.hdr` Radiance equirect 文件，
// stb_image::stbi_loadf 直接给出 RGBA float —— TextureLoader 走 .hdr
// 后缀分支时把 4*w*h float 转成 16*w*h bytes 落 `mPixels`，下游 Render
// 端（IblBaker::BakeEquirectToCube）拿到后按 `R32G32B32A32_Float` 创建
// RHITexture 并 upload。
//
// 其它格式（压缩 BC / 多 mip / 立方贴图）随消费方真正用到时再加，避免
// 一次性把 TextureAsset 做成 super-set。
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
        R32G32B32A32_Float, // HDR 路径专用（.hdr equirect / IBL bake 源数据）
    };

    // 单像素字节数。R8G8B8A8 = 4 byte；RGBA32Float = 16 byte。Unknown 返回 0
    // 让 caller 在错误格式上立刻显式失败而非诡异长度。
    inline constexpr std::size_t BytesPerPixel(TextureFormat format) noexcept
    {
        switch (format)
        {
            case TextureFormat::R8G8B8A8_UNorm:
                return 4;
            case TextureFormat::R32G32B32A32_Float:
                return 16;
            case TextureFormat::Unknown:
                return 0;
        }
        return 0;
    }

    class ORANGE_ENGINE_API TextureAsset
    {
    public:
        TextureAsset() = default;

        TextureAsset(std::uint32_t             width,
                     std::uint32_t             height,
                     TextureFormat             format,
                     std::vector<std::uint8_t> pixels)
            : mWidth(width), mHeight(height), mFormat(format), mPixels(std::move(pixels))
        {
        }

        std::uint32_t                    Width() const noexcept { return mWidth; }
        std::uint32_t                    Height() const noexcept { return mHeight; }
        TextureFormat                    Format() const noexcept { return mFormat; }
        const std::vector<std::uint8_t>& Pixels() const noexcept { return mPixels; }

        bool Empty() const noexcept { return mPixels.empty(); }

    private:
        std::uint32_t             mWidth{0};
        std::uint32_t             mHeight{0};
        TextureFormat             mFormat{TextureFormat::Unknown};
        std::vector<std::uint8_t> mPixels;
    };

} // namespace Orange::Engine::Asset

#endif // ORANGE_ENGINE_ASSET_TEXTURE_ASSET_H
