#ifndef ORANGE_ENGINE_ASSET_SOUND_LOADER_H
#define ORANGE_ENGINE_ASSET_SOUND_LOADER_H

// ---------------------------------------------------------------------------
// SoundLoader —— SoundAsset 的同步加载器。
//
// 当前只做"整个文件读进 std::vector<uint8_t>" + 路径记录——decode 由
// Audio::AudioEngine 内的 miniaudio decoder 在 CreateInstance 时按需做。
// 这条职责切分让 Asset 层不依赖 miniaudio：任何 .wav/.mp3/.flac/.ogg 文
// 件被当作不透明字节缓冲对待。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/core/Result.h>

#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{

    class ORANGE_ENGINE_API SoundLoader final : public IAssetLoader<SoundAsset>
    {
    public:
        SoundLoader()           = default;
        ~SoundLoader() override = default;

        Result<std::unique_ptr<SoundAsset>, ResultCode> Load(std::string_view path) override;
    };

} // namespace Orange::Engine::Asset

#endif // ORANGE_ENGINE_ASSET_SOUND_LOADER_H
