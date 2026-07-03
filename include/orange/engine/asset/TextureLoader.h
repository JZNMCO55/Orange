#ifndef ORANGE_ENGINE_ASSET_TEXTURE_LOADER_H
#define ORANGE_ENGINE_ASSET_TEXTURE_LOADER_H

// ---------------------------------------------------------------------------
// TextureLoader —— 同步纹理加载器，按文件后缀分派两条路径。
//
// **后缀 `.hdr` —— Radiance RGBE equirect**
//   走 vendor/stb/stb_image.h 的 `stbi_loadf` 路径，输出 4-channel float32
//   RGBA。典型用途：PolyHaven CC0 HDRI 1K outdoor scene，由 IblBaker::
//   BakeEquirectToCube 重采样到 6-face cubemap 后烘焙 irradiance / prefiltered。
//   stb 实现 detail 完全藏在 .cpp 内（`#define STB_IMAGE_IMPLEMENTATION`），
//   公共面不漏 stb 类型 / 宏。
//
// **其它后缀 —— 引擎自有 `.texture` 二进制格式**（沿用至今）
//   文件格式（little-endian、紧凑、无 padding）：
//     bytes 0..3   magic = 'O' 'R' 'T' 'X'
//     bytes 4..7   version = 1
//     bytes 8..11  width   (uint32)
//     bytes 12..15 height  (uint32)
//     bytes 16..19 format  (uint32; 1 = R8G8B8A8_UNorm, 2 = R32G32B32A32_Float)
//     bytes 20..   pixels  : width * height * BytesPerPixel(format)
//
//   format=2（RGBA32F）作为离线 cook 路径的产物格式——后续若引入"启动期
//   把 .hdr 烘焙缓存到 .texture"机制可走此条；当前 LDR 流水线只生产 format=1，
//   HDR 经 stb 直接从 .hdr 读出，不经容器。
//
// **未来扩展**：PNG / JPG / KTX 等格式留给后续 commit；新增格式时只在本
// 文件 Load 顶部加后缀分支即可，调用面不变。
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
        static constexpr std::uint32_t kMagic            = 0x5854524FU;
        static constexpr std::uint32_t kSupportedVersion = 1;

        TextureLoader()           = default;
        ~TextureLoader() override = default;

        Result<std::unique_ptr<TextureAsset>, ResultCode> Load(std::string_view path) override;
    };

} // namespace Orange::Engine::Asset

#endif // ORANGE_ENGINE_ASSET_TEXTURE_LOADER_H
