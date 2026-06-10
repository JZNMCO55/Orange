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

    // 析构必须在 Impl 内完成 uninit（而非仅靠 ~AudioEngine）：公共头声明了
    // move 赋值，operator=(AudioEngine&&) 会让 LHS 的旧 mpImpl 被替换析构。
    // 若 uninit 只在 ~AudioEngine、Impl 析构平凡，则 move-assign 释放旧 Impl
    // 时跳过 ma_engine_uninit/ma_context_uninit → ma_engine 内部设备线程仍
    // 引用已 free 的内存（潜在 UAF）+ 资源泄漏。RAII 化到 Impl 后，无论经
    // ~AudioEngine 还是 move-assign 销毁旧 Impl，都正确 uninit。
    ~Impl()
    {
        if (initialized)
        {
            ma_engine_uninit(&engine);
            initialized = false;
        }
        if (contextInited)
        {
            ma_context_uninit(&context);
            contextInited = false;
        }
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

// uninit 已 RAII 化到 Impl::~Impl（见上）——销毁 mpImpl 即正确清理，无论经
// 此析构还是 move-assign 替换旧 mpImpl。
AudioEngine::~AudioEngine() = default;

AudioEngine::AudioEngine(AudioEngine&&) noexcept            = default;
AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

bool AudioEngine::IsInitialized() const noexcept
{
    return mpImpl && mpImpl->initialized;
}

bool AudioEngine::PlayOneShot(const Asset::SoundAsset& asset, float volume)
{
    (void)asset;
    (void)volume;
    // 内存字节缓冲的 fire-and-forget one-shot 尚未实现：miniaudio 没有"byte
    // buffer 一步到 ma_sound"的 helper，正确路径需 ma_decoder_init_memory +
    // ma_sound_init_from_data_source，且 decoder 生命周期必须管到 sound 播完
    // （endCallback 回收）。在该路径落地前，本函数恒返回 false——播放经
    // CreateInstance（caller 持 SoundInstance 控制生命周期）覆盖。
    //
    // 注：旧实现里"new ma_sound + 用 nullptr data source 调 init + 条件 uninit
    // + 恒 return false"是死代码且有副作用（把 group 节点挂到 engine endpoint），
    // 已删除——不做一步实现不了的事，避免误导调用方以为可用。
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

    // miniaudio 资源的 uninit 放在 **Impl 析构**（而非 SoundInstance 析构）——关键：
    // SoundInstance 的 move-assign 是 defaulted，`a = std::move(b)`（a 已持活 sound）时
    // unique_ptr<Impl> move-assign 会 delete a 的旧 Impl，但 delete 只调 Impl 析构、**不**调
    // SoundInstance 析构。若 uninit 只在 SoundInstance 析构，旧 sound 就不会 ma_sound_uninit，
    // 其节点仍挂在 ma_engine 图上而 Impl 内存已释放 → 泄漏 + 混音/关闭时 use-after-free。
    // 放进 Impl 析构后，任何 delete Impl 的路径（析构 / move-assign 覆盖）都正确清理。
    // 顺序：先 uninit sound（依赖 decoder）→ 再 uninit decoder（依赖 bytesCopy）→ 析构体返回
    // 后才销毁 bytesCopy，故 decoder 在引用其内存被释放前已 uninit。
    ~Impl()
    {
        if (soundInited)   { ma_sound_uninit(&sound); }
        if (decoderInited) { ma_decoder_uninit(&decoder); }
    }
};

SoundInstance::SoundInstance() noexcept = default;
SoundInstance::SoundInstance(std::unique_ptr<Impl> impl) noexcept
    : mpImpl(std::move(impl))
{
}

// uninit 逻辑已移入 SoundInstance::Impl 析构（见上）——这样 move-assign 覆盖旧实例时
// unique_ptr delete 旧 Impl 也能正确清理（defaulted move-assign 不调本析构）。本析构
// = default 即可：成员 unique_ptr<Impl> 析构会 delete Impl → 触发 Impl::~Impl 的 uninit。
SoundInstance::~SoundInstance() = default;

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
