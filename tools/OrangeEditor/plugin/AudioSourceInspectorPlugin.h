#ifndef ORANGE_EDITOR_PLUGIN_AUDIO_SOURCE_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_AUDIO_SOURCE_INSPECTOR_PLUGIN_H

// AudioSourceInspectorPlugin —— Inspector 内 AudioSource 段的"装饰式扩展"。
//
// 职责：在 AudioSource 段末（ParseEnd 钩子）追加一行 Play / Stop 试播按
// 钮，让用户在 Edit 期无需进 Play Mode 就能听到挂在 entity 上的声音。
//
// 设计意图：与 AnimatorMiniPreviewPlugin 同款"plugin 只在 schema 默认渲染
// 之后追加 UI"模式 —— schema 负责字段编辑，plugin 负责"超出字段控件的
// 自定义 UI"（按 IEditorInspectorPlugin 头注释，正是"装饰式扩展"用例）。
//
// 选型理由：试播逻辑（CreateInstance + SetVolume/Pitch + Start/Stop）不能
// 走 schema 路径——schema 字段是"数据描述"，不能挂副作用动作（Start 改变
// AudioEngine 内部状态，违反 schema 字段"纯数据"语义）。同 Unity AudioSource
// 风格：参数走 Inspector 字段，Play / Stop 走 inspector 顶部 toolbar 按钮。
//
// 试播实例所有权：plugin 持单一 unique_ptr<SoundInstance>。每次 Play 重新
// CreateInstance（拿当前 component.sound 解码）；上一次实例自动析构。多
// entity 切换 selected 不冲突——切换 selected 后用户重点 Play 即更新到新
// 实例（旧实例尚在播会被 Stop+析构覆盖；语义同 Unity 编辑器"试播窗口跟
// 随选中"）。

#include "IEditorInspectorPlugin.h"

#include <orange/engine/audio/SoundInstance.h>

#include <memory>

namespace Orange::Editor::Plugin
{

class AudioSourceInspectorPlugin : public IEditorInspectorPlugin
{
public:
    // 按 schema.typeName == "AudioSource" 字符串比较（与 RegisterAudioSource
    // ComponentSchema 内字面量保持一致）。
    bool CanHandle(const Orange::Editor::Schema::ComponentSchema& schema) const override;

    // 段末追加 Play / Stop 试播按钮；无 sound 时按钮 disabled；
    // host.audioEngine 未初始化（无声卡）时显示 TextDisabled 提示。
    void ParseEnd(EditorHost&                                            host,
                  Orange::Engine::Entity                                 entity,
                  const Orange::Editor::Schema::ComponentSchema&         schema,
                  void*                                                  component) override;

private:
    // 试播实例 —— 跨帧持久，让"按 Play → 持续播放 → 按 Stop / 改字段重
    // 触发"路径有 instance 可控。空 unique_ptr 视为"无活跃试播"。
    std::unique_ptr<Orange::Engine::Audio::SoundInstance> mpTestInstance;
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_AUDIO_SOURCE_INSPECTOR_PLUGIN_H
