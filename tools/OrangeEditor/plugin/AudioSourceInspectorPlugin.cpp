#include "AudioSourceInspectorPlugin.h"

#include "../EditorHost.h"
#include "../schema/ComponentSchema.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/audio/AudioEngine.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/audio/SoundInstance.h>

#include <imgui.h>

#include <string_view>

namespace Orange::Editor::Plugin
{

    bool AudioSourceInspectorPlugin::CanHandle(
        const Orange::Editor::Schema::ComponentSchema& schema) const
    {
        if (schema.typeName == nullptr)
        {
            return false;
        }
        return std::string_view(schema.typeName) == std::string_view("AudioSource");
    }

    void AudioSourceInspectorPlugin::ParseEnd(
        EditorHost&                                    host,
        Orange::Engine::Entity                         entity,
        const Orange::Editor::Schema::ComponentSchema& schema,
        void*                                          component)
    {
        (void)schema;
        (void)entity;

        using AS  = Orange::Engine::Audio::AudioSourceComponent;
        auto* pAs = static_cast<AS*>(component);
        if (pAs == nullptr)
        {
            return;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Test Playback");

        // 没声卡 / WASAPI 失败时 audioEngine 不可用，按钮没意义；提示并跳过。
        if (!host.audioEngine.IsInitialized())
        {
            ImGui::TextDisabled("(AudioEngine not initialized)");
            return;
        }

        // 没挂 sound 资源时按钮 disabled；用户挂上后 disabled 自动解除。
        const bool hasSound = pAs->sound.IsValid();
        if (!hasSound)
        {
            ImGui::TextDisabled("(no sound assigned)");
            return;
        }

        // 试播必须能解 sound 字节 —— AssetRegistry 在 EditorAssetContext 内；
        // 没注入时按钮 disabled（理论上 EditorAssetContext.pAssets 永远存在，
        // 但保留 defensive guard）。
        auto* pAssets = host.assets.pAssets.get();
        if (pAssets == nullptr)
        {
            ImGui::TextDisabled("(AssetRegistry unavailable)");
            return;
        }
        const auto* pSoundAsset = pAssets->Get<Orange::Engine::Asset::SoundAsset>(pAs->sound);
        if (pSoundAsset == nullptr)
        {
            ImGui::TextDisabled("(sound asset not loaded)");
            return;
        }

        const bool isPlaying = mpTestInstance && mpTestInstance->IsPlaying();

        if (ImGui::Button(isPlaying ? "Restart##audio_test" : "Play##audio_test"))
        {
            // 每次 Play 重新 CreateInstance：拿当前 component 的 volume / pitch /
            // loop 值（用户拖参数即时生效，不必关闭 Inspector 再打开）。已有实
            // 例自动析构（unique_ptr 赋值释放旧实例 → ma_sound_uninit）。
            auto inst = host.audioEngine.CreateInstance(*pSoundAsset);
            if (inst.IsValid())
            {
                inst.SetVolume(pAs->volume);
                // pitch / loop 在 SoundInstance 公共面尚未暴露；试播仅消费
                // volume + Start/Stop，loop / pitch 字段在 PlayMode 期由 PlayMode
                // 实例化路径补 ma_sound_set_looping / ma_sound_set_pitch 调用。
                // 详见 EditorRenderLayer Play Mode 接入段。
                inst.Start();
                mpTestInstance = std::make_unique<
                    Orange::Engine::Audio::SoundInstance>(std::move(inst));
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!isPlaying);
        if (ImGui::Button("Stop##audio_test"))
        {
            if (mpTestInstance)
            {
                mpTestInstance->Stop();
                mpTestInstance.reset();
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled(isPlaying ? "▶ Playing" : "■ Stopped");
    }

} // namespace Orange::Editor::Plugin
