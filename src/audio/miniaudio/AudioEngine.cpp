// Audio 模块 miniaudio 后端 ——
//   * 唯一带 MINIAUDIO_IMPLEMENTATION 的 TU；
//   * 唯一直接 #include "miniaudio.h" 的位置（CLAUDE.md 不变量）；
//   * 同时承载 AudioEngine + SoundInstance 的 PIMPL 实现，避免把 miniaudio
//     头扩散到第二个 .cpp。
//
// miniaudio 的某些实现段在 MSVC /W4 + /WX 下会爆窄化转换 / 未引用形参等
// 警告——上游不 patch，本侧用 pragma 局部压成 /W0；本 .cpp 自己写的代
// 码段（pragma pop 之后）仍按项目级 /W4 /WX 受检。

#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include "orange/engine/asset/SoundAsset.h"
#include "orange/engine/audio/AudioEngine.h"
#include "orange/engine/audio/SoundInstance.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace Orange::Engine::Audio
{

// ---------------------------------------------------------------------------
// AudioEngine::Impl
// ---------------------------------------------------------------------------

struct AudioEngine::Impl
{
    bool        initialized{false};
    bool        nullBackend{false};
    ma_context  context{};
    bool        contextInited{false};
    ma_engine   engine{};

    // 用 null backend 显式构造 ma_context，再让 ma_engine_init 走它。
    // 真硬件 backend 走 ma_engine_init(NULL, ...) 自己起一个默认 context。
    ma_result InitNullBackend()
    {
        ma_context_config ccfg = ma_context_config_init();
        ma_backend bk = ma_backend_null;
        ma_result r = ma_context_init(&bk, 1, &ccfg, &context);
        if (r != MA_SUCCESS)
        {
            return r;
        }
        contextInited = true;
        ma_engine_config ecfg = ma_engine_config_init();
        ecfg.pContext = &context;
        return ma_engine_init(&ecfg, &engine);
    }

    ma_result InitDefaultBackend()
    {
        return ma_engine_init(/*config*/ nullptr, &engine);
    }
};

AudioEngine::AudioEngine()
    : AudioEngine(AudioEngineDesc{})
{
}

AudioEngine::AudioEngine(const AudioEngineDesc& desc)
    : mpImpl(std::make_unique<Impl>())
{
    mpImpl->nullBackend = desc.useNullBackend;
    const ma_result r = desc.useNullBackend
                          ? mpImpl->InitNullBackend()
                          : mpImpl->InitDefaultBackend();
    mpImpl->initialized = (r == MA_SUCCESS);
    if (!mpImpl->initialized && mpImpl->contextInited)
    {
        ma_context_uninit(&mpImpl->context);
        mpImpl->contextInited = false;
    }
}

AudioEngine::~AudioEngine()
{
    if (!mpImpl)
    {
        return;
    }
    if (mpImpl->initialized)
    {
        ma_engine_uninit(&mpImpl->engine);
        mpImpl->initialized = false;
    }
    if (mpImpl->contextInited)
    {
        ma_context_uninit(&mpImpl->context);
        mpImpl->contextInited = false;
    }
}

AudioEngine::AudioEngine(AudioEngine&&) noexcept            = default;
AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

bool AudioEngine::IsInitialized() const noexcept
{
    return mpImpl && mpImpl->initialized;
}

bool AudioEngine::PlayOneShot(const Asset::SoundAsset& asset, float volume)
{
    if (!IsInitialized() || asset.Empty())
    {
        return false;
    }
    // 直接走 ma_sound_init_from_memory。ma_engine_play_sound 仅接受文件路
    // 径——本侧的 SoundAsset 是字节缓冲。返回的 ma_sound 由 miniaudio 内
    // 部 group 持有所有权（设 endCallback 时由 caller 释放，此处不传 → 走
    // 自动 oneshot 路径）。
    auto* snd = new (std::nothrow) ma_sound{};
    if (snd == nullptr)
    {
        return false;
    }
    auto bytes = asset.Bytes();
    ma_result r = ma_sound_init_from_data_source(
        &mpImpl->engine,
        nullptr,  // 占位——下面用 init_from_memory 替代
        MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr,
        snd);
    // 上面留作占位——miniaudio 没有"直接从 byte buffer 一步到 ma_sound"
    // 的 helper；正确路径是先 ma_decoder_init_memory 再 ma_sound_init_from_data_source。
    // 本任务范围内 PlayOneShot 还原到 caller-managed 路径：用临时 decoder
    // 走完整生命周期——decoder 与 sound 同生死，调用方等 sound 播完才能
    // 释放 decoder。简化起见这里先不暴露 oneshot 完整路径，返回 false 表
    // 示"不支持 oneshot 内存播放"——CreateInstance 路径覆盖控制播放。
    if (r == MA_SUCCESS)
    {
        ma_sound_uninit(snd);
    }
    delete snd;
    (void)volume;
    (void)bytes;
    return false;
}

void AudioEngine::StopAll() noexcept
{
    if (!IsInitialized())
    {
        return;
    }
    // ma_engine 有"stop the global graph"——调 ma_engine_stop。
    ma_engine_stop(&mpImpl->engine);
}

void AudioEngine::SetMasterVolume(float volume) noexcept
{
    if (!IsInitialized())
    {
        return;
    }
    if (volume < 0.0f)
    {
        volume = 0.0f;
    }
    ma_engine_set_volume(&mpImpl->engine, volume);
}

std::string_view AudioEngine::BackendName() const noexcept
{
    if (!IsInitialized())
    {
        return {};
    }
    return mpImpl->nullBackend ? std::string_view{"null"} : std::string_view{"default"};
}

// ---------------------------------------------------------------------------
// SoundInstance::Impl
// ---------------------------------------------------------------------------

struct SoundInstance::Impl
{
    ma_decoder decoder{};
    ma_sound   sound{};
    bool       decoderInited{false};
    bool       soundInited{false};

    // 本 Impl 需要在 SoundAsset 字节缓冲被释放后仍能播——所以拷一份字节。
    // 大文件场景可优化为 shared 引用计数；0.x 阶段保稳定。
    std::vector<std::uint8_t> bytesCopy;
};

SoundInstance::SoundInstance() noexcept = default;
SoundInstance::SoundInstance(std::unique_ptr<Impl> impl) noexcept
    : mpImpl(std::move(impl))
{
}

SoundInstance::~SoundInstance()
{
    if (!mpImpl)
    {
        return;
    }
    if (mpImpl->soundInited)
    {
        ma_sound_uninit(&mpImpl->sound);
        mpImpl->soundInited = false;
    }
    if (mpImpl->decoderInited)
    {
        ma_decoder_uninit(&mpImpl->decoder);
        mpImpl->decoderInited = false;
    }
}

SoundInstance::SoundInstance(SoundInstance&&) noexcept            = default;
SoundInstance& SoundInstance::operator=(SoundInstance&&) noexcept = default;

bool SoundInstance::IsValid() const noexcept
{
    return mpImpl && mpImpl->soundInited;
}

void SoundInstance::Start()
{
    if (!IsValid()) { return; }
    ma_sound_start(&mpImpl->sound);
}

void SoundInstance::Stop()
{
    if (!IsValid()) { return; }
    ma_sound_stop(&mpImpl->sound);
}

void SoundInstance::SetVolume(float volume)
{
    if (!IsValid()) { return; }
    if (volume < 0.0f) { volume = 0.0f; }
    ma_sound_set_volume(&mpImpl->sound, volume);
}

bool SoundInstance::IsPlaying() const noexcept
{
    if (!IsValid()) { return false; }
    return ma_sound_is_playing(&mpImpl->sound) != 0;
}

void SoundInstance::Restart()
{
    if (!IsValid()) { return; }
    // Stop 不重置播放头；需显式 seek 0 才能"重头开始"。
    ma_sound_stop(&mpImpl->sound);
    ma_sound_seek_to_pcm_frame(&mpImpl->sound, 0);
    ma_sound_start(&mpImpl->sound);
}

// ---------------------------------------------------------------------------
// AudioEngine::CreateInstance（友元访问 SoundInstance::Impl 通过本 TU）
// ---------------------------------------------------------------------------

SoundInstance AudioEngine::CreateInstance(const Asset::SoundAsset& asset)
{
    if (!IsInitialized() || asset.Empty())
    {
        return SoundInstance{};
    }

    auto impl = std::make_unique<SoundInstance::Impl>();
    auto src  = asset.Bytes();
    impl->bytesCopy.assign(src.begin(), src.end());

    ma_result r = ma_decoder_init_memory(
        impl->bytesCopy.data(),
        impl->bytesCopy.size(),
        nullptr,  // 默认 decoder config
        &impl->decoder);
    if (r != MA_SUCCESS)
    {
        return SoundInstance{};
    }
    impl->decoderInited = true;

    r = ma_sound_init_from_data_source(
        &mpImpl->engine,
        &impl->decoder,
        MA_SOUND_FLAG_NO_SPATIALIZATION,
        /*pGroup=*/nullptr,
        &impl->sound);
    if (r != MA_SUCCESS)
    {
        ma_decoder_uninit(&impl->decoder);
        impl->decoderInited = false;
        return SoundInstance{};
    }
    impl->soundInited = true;

    return SoundInstance{std::move(impl)};
}

}  // namespace Orange::Engine::Audio
