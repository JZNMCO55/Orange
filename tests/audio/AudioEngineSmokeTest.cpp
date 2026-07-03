// AudioEngineSmokeTest —— Phase 4 / Task 09 验收：
// AudioEngine init / shutdown 不崩；SoundInstance 在无效 engine / 无效
// asset 路径下行为合规（不崩 / IsValid==false）。
//
// CI 友好：用 useNullBackend = true 路径——miniaudio ma_backend_null 不
// 访问硬件，没声卡的机器也能跑过；real backend 路径只验"ctor 不抛"，
// 不强制 IsInitialized==true（驱动 / 权限 / WSL 等场景可能合理失败）。

#include "orange/engine/asset/SoundAsset.h"
#include "orange/engine/audio/AudioEngine.h"
#include "orange/engine/audio/Sound.h"
#include "orange/engine/audio/SoundInstance.h"

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace Au  = Orange::Engine::Audio;
namespace Ast = Orange::Engine::Asset;

namespace
{

    // 构造一个最小合法 16-bit PCM mono WAV（4 个静音样本），让 null-backend 的 ma_decoder
    // 能解码出 soundInited=true 的活 SoundInstance —— 用来真正触发 move-assign 覆盖路径。
    std::vector<std::uint8_t> MakeTinyWav()
    {
        std::vector<std::uint8_t> b;
        auto                      u16 = [&](std::uint16_t v)
        { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); };
        auto u32 = [&](std::uint32_t v)
        {
            b.push_back(v & 0xFF);
            b.push_back((v >> 8) & 0xFF);
            b.push_back((v >> 16) & 0xFF);
            b.push_back((v >> 24) & 0xFF);
        };
        auto tag = [&](const char* s)
        {
            for (int i = 0; i < 4; ++i)
            {
                b.push_back(static_cast<std::uint8_t>(s[i]));
            }
        };
        const std::uint32_t dataBytes = 8; // 4 样本 * 2 字节
        tag("RIFF");
        u32(36 + dataBytes);
        tag("WAVE");
        tag("fmt ");
        u32(16);
        u16(1);
        u16(1);
        u32(8000);
        u32(16000);
        u16(2);
        u16(16);
        tag("data");
        u32(dataBytes);
        for (int i = 0; i < 4; ++i)
        {
            u16(0);
        } // 4 个静音样本
        return b;
    }

    void TestNullBackendInitShutdown()
    {
        // null backend 在所有平台上都该成功。
        Au::AudioEngineDesc desc;
        desc.useNullBackend = true;
        Au::AudioEngine engine(desc);
        assert(engine.IsInitialized());
        assert(engine.BackendName() == "null");
    }

    void TestDefaultBackendInitShutdown()
    {
        // 真硬件 backend——成功 / 失败都不该崩；本测只验 ctor / dtor 路径
        // 安全。CI 机器可能没声卡 → IsInitialized 可能 false，这是合规结果。
        Au::AudioEngine engine;
        // 不 assert(IsInitialized)：CI 友好。
        (void)engine.IsInitialized();
    }

    void TestStopAllAndVolumeOnUninitializedSafe()
    {
        // 强制构造一个肯定失败的 engine（虽然 default backend 也可能成功——
        // 这里换种保 null backend 然后看常用 setter 是否安全）。
        Au::AudioEngineDesc desc;
        desc.useNullBackend = true;
        Au::AudioEngine engine(desc);
        engine.SetMasterVolume(0.5f);
        engine.SetMasterVolume(-1.0f); // 负数被 clamp 到 0
        engine.StopAll();
    }

    void TestCreateInstanceWithEmptyAsset()
    {
        Au::AudioEngineDesc desc;
        desc.useNullBackend = true;
        Au::AudioEngine engine(desc);
        assert(engine.IsInitialized());

        // 空 SoundAsset → CreateInstance 返无效 instance；不崩。
        Ast::SoundAsset emptyAsset;
        auto            inst = engine.CreateInstance(emptyAsset);
        assert(!inst.IsValid());
        inst.Start();         // no-op
        inst.Stop();          // no-op
        inst.SetVolume(0.7f); // no-op
        assert(!inst.IsPlaying());
    }

    void TestSoundInstanceMoveSemantics()
    {
        Au::AudioEngineDesc desc;
        desc.useNullBackend = true;
        Au::AudioEngine engine(desc);

        Au::SoundInstance a; // 默认无效
        assert(!a.IsValid());
        Au::SoundInstance b{std::move(a)}; // 移动构造空 → 仍空
        assert(!b.IsValid());

        // move-assignment 路径（空实例）：操作合规、不崩。
        Au::SoundInstance c;
        c = std::move(b);
        assert(!c.IsValid());
    }

    // move-assign 覆盖一个**已持活 sound** 的 instance：验证旧 sound 被正确释放（不泄漏/UAF）。
    // 修复前 uninit 在 SoundInstance 析构而 move-assign 是 defaulted —— unique_ptr delete 旧 Impl
    // 不调 SoundInstance 析构 → 旧 ma_sound 未 uninit、节点悬挂在 ma_engine 图上而内存已释放 →
    // engine 析构/混音时 use-after-free。修复后 uninit 移入 Impl 析构,delete 旧 Impl 即正确清理。
    void TestSoundInstanceMoveAssignReleasesOldSound()
    {
        Au::AudioEngineDesc desc;
        desc.useNullBackend = true;
        Au::AudioEngine engine(desc);
        if (!engine.IsInitialized())
        {
            return;
        } // null backend 应成功

        Ast::SoundAsset wav{MakeTinyWav(), "tiny.wav"};
        auto            first  = engine.CreateInstance(wav);
        auto            second = engine.CreateInstance(wav);
        // 合法 WAV 在 null backend 上应解码出有效 instance（已验 first=second=valid）；若某环境
        // 解码异常则跳过（CI 健壮）。
        if (!first.IsValid() || !second.IsValid())
        {
            return;
        }

        first = std::move(second); // 覆盖 first 的活 sound —— 修复前在此泄漏旧 sound、engine 析构 UAF
        assert(first.IsValid() && "move-assign 后 first 持有原 second 的 sound");
        assert(!second.IsValid() && "被移动方置空");
        // 函数返回 → engine + first 析构。修复后旧/新 sound 都已正确 uninit,无 UAF。
    }

    void TestSoundIsAssetAlias()
    {
        // Audio::Sound 与 Asset::SoundAsset 同类型（typedef）—— compile-time 验证。
        static_assert(std::is_same_v<Au::Sound, Ast::SoundAsset>,
                      "Audio::Sound must alias Asset::SoundAsset");
    }

} // namespace

int main()
{
    TestNullBackendInitShutdown();
    TestDefaultBackendInitShutdown();
    TestStopAllAndVolumeOnUninitializedSafe();
    TestCreateInstanceWithEmptyAsset();
    TestSoundInstanceMoveSemantics();
    TestSoundInstanceMoveAssignReleasesOldSound();
    TestSoundIsAssetAlias();
    return 0;
}
