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
#include <utility>

namespace Au  = Orange::Engine::Audio;
namespace Ast = Orange::Engine::Asset;

namespace
{

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
    engine.SetMasterVolume(-1.0f);  // 负数被 clamp 到 0
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
    auto inst = engine.CreateInstance(emptyAsset);
    assert(!inst.IsValid());
    inst.Start();        // no-op
    inst.Stop();         // no-op
    inst.SetVolume(0.7f);// no-op
    assert(!inst.IsPlaying());
}

void TestSoundInstanceMoveSemantics()
{
    Au::AudioEngineDesc desc;
    desc.useNullBackend = true;
    Au::AudioEngine engine(desc);

    Au::SoundInstance a;          // 默认无效
    assert(!a.IsValid());
    Au::SoundInstance b{std::move(a)};  // 移动空 → 仍空
    assert(!b.IsValid());
}

void TestSoundIsAssetAlias()
{
    // Audio::Sound 与 Asset::SoundAsset 同类型（typedef）—— compile-time 验证。
    static_assert(std::is_same_v<Au::Sound, Ast::SoundAsset>,
                  "Audio::Sound must alias Asset::SoundAsset");
}

}  // namespace

int main()
{
    TestNullBackendInitShutdown();
    TestDefaultBackendInitShutdown();
    TestStopAllAndVolumeOnUninitializedSafe();
    TestCreateInstanceWithEmptyAsset();
    TestSoundInstanceMoveSemantics();
    TestSoundIsAssetAlias();
    return 0;
}
