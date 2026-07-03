#ifndef ORANGE_ENGINE_AUDIO_AUDIO_ENGINE_H
#define ORANGE_ENGINE_AUDIO_AUDIO_ENGINE_H

// ---------------------------------------------------------------------------
// AudioEngine —— 音频子系统的根入口（PIMPL，包 miniaudio）。
//
// 默认 ctor 拿默认设备（Windows 走 WASAPI / DirectSound）；CI / headless 测
// 试走 `useNullBackend = true`——miniaudio 的 ma_backend_null 不访问任何
// 硬件，纯 in-memory mixer，让单测在没声卡的机器上也能跑完不崩。
//
// 生命周期约定：
//   * ctor 不抛异常——init 失败时 IsInitialized() == false，调用方按需查询；
//     这条让"游戏没声卡的机器仍能跑"成为正常路径，不是错误路径；
//   * dtor 自动 uninit；任何 SoundInstance 必须在 AudioEngine 析构前显式
//     释放（typical：游戏侧自己持有 unique_ptr）。
//
// 头隔离：miniaudio 类型不暴露——`#include "miniaudio.h"` 仅在
// src/audio/miniaudio/AudioEngine.cpp（CLAUDE.md 不变量）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{
    class SoundAsset;
}

namespace Orange::Engine::Audio
{

    class SoundInstance;

    struct AudioEngineDesc
    {
        // CI / headless 测试用——miniaudio 的 null backend 不访问硬件，
        // ma_engine 的 mixer 仍然按时序推进，但永远不出声。production 默认 false。
        bool useNullBackend{false};
    };

    class ORANGE_ENGINE_API AudioEngine
    {
    public:
        AudioEngine(); // 默认 desc：硬件 backend
        explicit AudioEngine(const AudioEngineDesc& desc);
        ~AudioEngine();

        AudioEngine(const AudioEngine&)            = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;
        AudioEngine(AudioEngine&&) noexcept;
        AudioEngine& operator=(AudioEngine&&) noexcept;

        // ma_engine_init 是否成功。返 false 时 PlayOneShot / CreateInstance 等
        // 全部 no-op（不崩，但不出声）——典型场景：CI 机器无声卡 + 没启用
        // null backend；或 ma_engine_init 因驱动问题失败。
        bool IsInitialized() const noexcept;

        // 触发即忘——一次性播放（用于短促 SFX）。bytes 必须在调用返回前可
        // 读（miniaudio 内部按需拷贝）。返 false 时未实际播放。
        bool PlayOneShot(const Asset::SoundAsset& asset, float volume = 1.0f);

        // 详细控制（暂停 / 停止 / 调音量）走 SoundInstance；构造一个并管理
        // 生命周期。失败时返回的 instance 在 IsValid() 上为 false。
        SoundInstance CreateInstance(const Asset::SoundAsset& asset);

        // 全局停止——慎用；典型用于"切场景"或"暂停所有声音"。底层
        // 走 ma_engine 的 graph stop。
        void StopAll() noexcept;

        // 全局音量（0..1）。负值 clamp 到 0；> 1 由 miniaudio 决定（典型不裁，
        // 调用方自己保证）。
        void SetMasterVolume(float volume) noexcept;

        // backend 名字——"wasapi" / "dsound" / "null" 等；诊断用。未初始化 →
        // 返回空字符串视图。
        std::string_view BackendName() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> mpImpl;

        // SoundInstance 走 friend 访问 Impl，让 Sound 控制路径不必在公共面
        // 暴露 ma_sound 的内部 PIMPL。
        friend class SoundInstance;
    };

} // namespace Orange::Engine::Audio

#endif // ORANGE_ENGINE_AUDIO_AUDIO_ENGINE_H
