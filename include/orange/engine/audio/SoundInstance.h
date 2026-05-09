#ifndef ORANGE_ENGINE_AUDIO_SOUND_INSTANCE_H
#define ORANGE_ENGINE_AUDIO_SOUND_INSTANCE_H

// ---------------------------------------------------------------------------
// SoundInstance —— 一个正在被 AudioEngine 管理的可控播放体。
//
// 由 AudioEngine::CreateInstance(asset) 返回；持有 PIMPL 包的 ma_sound。
// 调用方可以 Start / Pause / Stop / SetVolume；析构时自动 ma_sound_uninit。
//
// 生命周期：必须在 AudioEngine 析构前释放——Engine 析构时其 ma_engine
// 内部资源都被回收，留下的 ma_sound 句柄就悬空了。本类不持 Engine 引用
// 计数（move-only RAII），调用方按所有权金字塔自己保证顺序。
//
// 头隔离：miniaudio 类型不暴露——ma_sound 藏在 PIMPL 里。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <memory>

namespace Orange::Engine::Audio
{

class AudioEngine;

class ORANGE_ENGINE_API SoundInstance
{
public:
    SoundInstance() noexcept;     // 无效实例
    ~SoundInstance();

    SoundInstance(const SoundInstance&)            = delete;
    SoundInstance& operator=(const SoundInstance&) = delete;

    SoundInstance(SoundInstance&&) noexcept;
    SoundInstance& operator=(SoundInstance&&) noexcept;

    // 是否绑定了一个真 ma_sound——AudioEngine.CreateInstance 失败 / 默认
    // 构造的 SoundInstance 都是 false。无效 instance 上调 Start/Stop 等
    // 全部 no-op、不崩。
    bool IsValid() const noexcept;

    void Start();
    void Stop();
    void SetVolume(float volume);
    bool IsPlaying() const noexcept;

    // 把播放头拨回起点 + 立即 Start——sample / 游戏代码用来"重触发"
    // 同一个 SoundInstance（如脚步声 / 跳跃声）。比每次重新 CreateInstance
    // 省一次 decoder + ma_sound 初始化。
    void Restart();

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;

    explicit SoundInstance(std::unique_ptr<Impl> impl) noexcept;
    friend class AudioEngine;
};

}  // namespace Orange::Engine::Audio

#endif  // ORANGE_ENGINE_AUDIO_SOUND_INSTANCE_H
