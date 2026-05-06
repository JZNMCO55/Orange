#include "orange/engine/asset/TextureLoader.h"

#include "orange/engine/core/Serialization.h"

#include <cstdint>

namespace Orange::Engine::Asset
{

Result<std::unique_ptr<TextureAsset>, ResultCode> TextureLoader::Load(std::string_view path)
{
    auto bytesResult = BinaryReader::LoadFile(path);
    if (bytesResult.IsErr())
    {
        return bytesResult.Error();
    }
    const auto& bytes = bytesResult.Value();
    BinaryReader reader{bytes.data(), bytes.size()};

    std::uint32_t magic = 0;
    if (!reader.Read(magic))
    {
        return ResultCode::InvalidArgument;
    }
    if (magic != kMagic)
    {
        return ResultCode::SchemaMismatch;
    }

    std::uint32_t version = 0;
    if (!reader.Read(version))
    {
        return ResultCode::InvalidArgument;
    }
    if (version != kSupportedVersion)
    {
        return ResultCode::SchemaMismatch;
    }

    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::uint32_t format = 0;
    if (!reader.Read(width) || !reader.Read(height) || !reader.Read(format))
    {
        return ResultCode::InvalidArgument;
    }

    if (width == 0 || height == 0)
    {
        return ResultCode::InvalidArgument;
    }
    // 软上限避免恶意输入诱发巨型分配；Phase 2 场景远小于这个值。
    constexpr std::uint32_t kMaxDim = 16u * 1024u;
    if (width > kMaxDim || height > kMaxDim)
    {
        return ResultCode::OutOfRange;
    }

    TextureFormat fmt = TextureFormat::Unknown;
    std::size_t bytesPerPixel = 0;
    switch (format)
    {
        case static_cast<std::uint32_t>(TextureFormat::R8G8B8A8_UNorm):
            fmt = TextureFormat::R8G8B8A8_UNorm;
            bytesPerPixel = 4;
            break;
        default:
            return ResultCode::Unsupported;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    const std::size_t pixelBytes = pixelCount * bytesPerPixel;

    std::vector<std::uint8_t> pixels;
    pixels.resize(pixelBytes);
    if (pixelBytes > 0 && !reader.ReadBytes(pixels.data(), pixelBytes))
    {
        return ResultCode::InvalidArgument;
    }

    return std::make_unique<TextureAsset>(width, height, fmt, std::move(pixels));
}

}  // namespace Orange::Engine::Asset
