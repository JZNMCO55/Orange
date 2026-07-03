#ifndef ORANGE_EDITOR_PLUGIN_AUDIO_ASSET_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_AUDIO_ASSET_INSPECTOR_PLUGIN_H

// AudioAssetInspectorPlugin —— Asset 浏览器选中 .wav / .ogg / .mp3 / .flac
// 时接管整个 Inspector 区域，显示音频元数据 + Preview Play / Stop 按钮。
//
// 与 MaterialAssetInspectorPlugin / AnimFsmAssetInspectorPlugin / DragonBones
// AssetInspectorPlugin 同款 IEditorAssetInspectorPlugin 第四个真实 case。
//
// 预览实例所有权：plugin 持单一 unique_ptr<SoundInstance>，同
// AudioSourceInspectorPlugin 试播路径（切换选中资源时上次实例自动析构）。

#include "IEditorAssetInspectorPlugin.h"

#include <orange/engine/audio/SoundInstance.h>

#include <memory>

namespace Orange::Editor::Plugin
{

    class AudioAssetInspectorPlugin : public IEditorAssetInspectorPlugin
    {
    public:
        // 按 path 末尾 .wav / .ogg / .mp3 / .flac 后缀比较（与 Asset 浏览器
        // 内 ".wav/.ogg/.mp3" 扩展名映射保持一致）。
        bool CanHandle(const std::string& assetPath) const override;

        // 接管 Inspector：显示 Path / Size / Preview Play / Stop 按钮 +
        // "Pick to AudioSource.sound" 选中提示。
        void Draw(EditorHost& host, const std::string& assetPath) override;

    private:
        std::unique_ptr<Orange::Engine::Audio::SoundInstance> mpPreviewInstance;
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_AUDIO_ASSET_INSPECTOR_PLUGIN_H
