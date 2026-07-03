#ifndef ORANGE_ENGINE_AUDIO_SOUND_H
#define ORANGE_ENGINE_AUDIO_SOUND_H

// ---------------------------------------------------------------------------
// Sound —— Audio 模块的轻量 alias，把 Asset::SoundAsset 引到 Audio 命名空间。
//
// 0.x 阶段 Audio 与 Asset 关系简单："声音文件"在 Asset 层叫
// SoundAsset；Audio 层的对外类型就是它。本头不增任何字段——只是给
// 调用方 `Orange::Engine::Audio::Sound` 这个更顺手的名字（与
// Audio::SoundInstance 配对）。后续真有 audio-only 元数据（streaming
// flag / 3D pos hint）时可以在这层独立扩展，不动 Asset。
// ---------------------------------------------------------------------------

#include <orange/engine/asset/SoundAsset.h>

namespace Orange::Engine::Audio
{

    using Sound = Orange::Engine::Asset::SoundAsset;

} // namespace Orange::Engine::Audio

#endif // ORANGE_ENGINE_AUDIO_SOUND_H
