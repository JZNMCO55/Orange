#ifndef ORANGE_ENGINE_ASSET_SOUND_ASSET_H
#define ORANGE_ENGINE_ASSET_SOUND_ASSET_H

// ---------------------------------------------------------------------------
// SoundAsset —— 已加载的声音文件原始字节。
//
// SoundLoader 一次性把整个 .wav / .mp3 / .flac 文件读进内存；后续 Audio::
// Sound 在 AudioEngine 上 mount 时由 miniaudio 内部 decoder 流式解码——
// 本 asset 不预解码，只是字节缓冲 + 文件名 hint，这样：
//   * Asset 层不需要依赖 miniaudio（任何可解码格式由 Audio 层在播放时
//     选 decoder）；
//   * 大文件不必常驻 PCM；
//   * 多个 SoundInstance 共享同一份 SoundAsset 不冲突——decoder 各跑各的。
//
// 头隔离：本头零 miniaudio 依赖。decoder 类型只在 src/audio/miniaudio/
// 内部出现。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Orange::Engine::Asset
{

    class ORANGE_ENGINE_API SoundAsset
    {
    public:
        SoundAsset() = default;
        SoundAsset(std::vector<std::uint8_t> bytes, std::string sourcePath)
            : mBytes(std::move(bytes)), mSourcePath(std::move(sourcePath))
        {
        }

        // 文件原始字节（用于 miniaudio 的 decoder_init_memory）。
        std::span<const std::uint8_t> Bytes() const noexcept
        {
            return std::span<const std::uint8_t>{mBytes.data(), mBytes.size()};
        }

        // 加载来源 path——给 decoder 提示扩展名（miniaudio 通过 magic bytes 自
        // 检，扩展名仅 fallback；本字段也用于诊断）。
        std::string_view SourcePath() const noexcept { return mSourcePath; }

        bool        Empty() const noexcept { return mBytes.empty(); }
        std::size_t Size() const noexcept { return mBytes.size(); }

    private:
        std::vector<std::uint8_t> mBytes;
        std::string               mSourcePath;
    };

} // namespace Orange::Engine::Asset

#endif // ORANGE_ENGINE_ASSET_SOUND_ASSET_H
