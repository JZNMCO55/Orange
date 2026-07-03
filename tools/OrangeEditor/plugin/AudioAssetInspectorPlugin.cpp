#include "AudioAssetInspectorPlugin.h"

#include "../EditorHost.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/audio/AudioEngine.h>
#include <orange/engine/audio/SoundInstance.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace Orange::Editor::Plugin
{

    namespace
    {

        // case-insensitive 后缀匹配，与 Asset 浏览器 icon 路径同款扩展名集合。
        bool EndsWithIgnoreCase(const std::string& s, std::string_view suffix)
        {
            if (s.size() < suffix.size())
            {
                return false;
            }
            const std::size_t offset = s.size() - suffix.size();
            for (std::size_t i = 0; i < suffix.size(); ++i)
            {
                const unsigned char a = static_cast<unsigned char>(s[offset + i]);
                const unsigned char b = static_cast<unsigned char>(suffix[i]);
                if (std::tolower(a) != std::tolower(b))
                {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    bool AudioAssetInspectorPlugin::CanHandle(const std::string& assetPath) const
    {
        if (assetPath.empty())
        {
            return false;
        }
        return EndsWithIgnoreCase(assetPath, ".wav") || EndsWithIgnoreCase(assetPath, ".ogg") || EndsWithIgnoreCase(assetPath, ".mp3") || EndsWithIgnoreCase(assetPath, ".flac");
    }

    void AudioAssetInspectorPlugin::Draw(EditorHost& host, const std::string& assetPath)
    {
        ImGui::Text("Sound Asset");
        ImGui::Separator();
        ImGui::TextWrapped("Path: %s", assetPath.c_str());
        ImGui::Spacing();

        if (!host.audioEngine.IsInitialized())
        {
            ImGui::TextDisabled("AudioEngine not initialized (no audio device).");
            return;
        }

        auto* pAssets = host.assets.pAssets.get();
        if (pAssets == nullptr)
        {
            ImGui::TextDisabled("AssetRegistry unavailable.");
            return;
        }

        // 按需 Load —— Inspector 选中即触发 lazy load（后续 Pick 到 AudioSource
        // 字段时直接复用同一 handle，AssetRegistry::Load dedup 保证）。
        auto loadResult = pAssets->Load<Orange::Engine::Asset::SoundAsset>(assetPath);
        if (!loadResult.IsOk())
        {
            ImGui::TextDisabled("Failed to load sound asset (code=%u).",
                                static_cast<unsigned>(loadResult.Error()));
            return;
        }
        const auto  handle      = loadResult.Value();
        const auto* pSoundAsset = pAssets->Get<Orange::Engine::Asset::SoundAsset>(handle);
        if (pSoundAsset == nullptr)
        {
            ImGui::TextDisabled("Sound asset handle resolved to null.");
            return;
        }

        ImGui::Text("Size: %zu bytes", pSoundAsset->Size());
        ImGui::Spacing();

        const bool isPlaying = mpPreviewInstance && mpPreviewInstance->IsPlaying();

        if (ImGui::Button(isPlaying ? "Restart##audio_preview" : "Play##audio_preview"))
        {
            auto inst = host.audioEngine.CreateInstance(*pSoundAsset);
            if (inst.IsValid())
            {
                inst.Start();
                mpPreviewInstance = std::make_unique<
                    Orange::Engine::Audio::SoundInstance>(std::move(inst));
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!isPlaying);
        if (ImGui::Button("Stop##audio_preview"))
        {
            if (mpPreviewInstance)
            {
                mpPreviewInstance->Stop();
                mpPreviewInstance.reset();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(isPlaying ? "▶ Playing" : "■ Stopped");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Tip: 右键此资源 → Pick to AudioSource.sound 把它挂到选中实体。");
    }

} // namespace Orange::Editor::Plugin
