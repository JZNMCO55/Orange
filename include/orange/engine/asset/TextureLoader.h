#ifndef ORANGE_ENGINE_ASSET_TEXTURE_LOADER_H
#define ORANGE_ENGINE_ASSET_TEXTURE_LOADER_H

// ---------------------------------------------------------------------------
// TextureLoader —— 引擎自有 texture 二进制格式的同步加载器。
//
// 文件格式（little-endian、紧凑、无 padding）：
//   bytes 0..3   magic = 'O' 'R' 'T' 'X'
//   bytes 4..7   version = 1
//   bytes 8..11  width   (uint32)
//   bytes 12..15 height  (uint32)
//   bytes 16..19 format  (uint32; 1 = R8G8B8A8_UNorm)
//   bytes 20..   pixels  : width * height * bytesPerPixel
//
// 当前只承认 format=1（R8G8B8A8）。其它 format 落地时新增 enum 项 +
// 在本 loader 校验通过即可。PNG / JPG 等"真实"格式的接入等 stb 在
// Dependencies 里 vendor 化后另起 loader 子类，不和这里耦合。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/core/Result.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API TextureLoader final : public IAssetLoader<TextureAsset>
{
public:
    // 'O','R','T','X' 按 little-endian 读出：内存字节序 0x4F,0x52,0x54,0x58
    // 反推 uint32 = 0x5854524F。
    static constexpr std::uint32_t kMagic = 0x5854524FU;
    static constexpr std::uint32_t kSupportedVersion = 1;

    TextureLoader() = default;
    ~TextureLoader() override = default;

    Result<std::unique_ptr<TextureAsset>, ResultCode> Load(std::string_view path) override;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_TEXTURE_LOADER_H
