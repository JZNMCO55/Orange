#ifndef ORANGE_ENGINE_AUDIO_AUDIO_SOURCE_COMPONENT_H
#define ORANGE_ENGINE_AUDIO_AUDIO_SOURCE_COMPONENT_H

// ---------------------------------------------------------------------------
// AudioSourceComponent —— 把 SoundAsset 挂到 entity 上的 PureData ECS 组件。
//
// 当前阶段 AudioEngine 是 2D mixer（无 3D positional / panning），所以本组件
// 也只描述"播放参数"，不描述空间属性：
//   * 不含 position —— 走 entity 的 TransformComponent 即可，未来切 3D
//     positional 时由 AudioEngine 内部消费 Transform.position；
//   * 不含 listener tag —— 全局 listener，无需 per-entity 标记；
//   * 不含运行时 SoundInstance handle —— SoundInstance 是 PIMPL move-only
//     类型，放进 ECS 组件会让"实例化-反写"路径需要在公共面 forward decl
//     + 复杂的 move 语义；改由 PlayMode 驱动方（编辑器 EditorRenderLayer /
//     游戏侧 AudioPlaybackSystem）自持 entity→instance map（参考
//     PhysicsWorld 也持 body handle 的同款模式，但本 component 选了对偶的
//     "PureData" 方案，简化 ECS 路径）。
//
// 设计取舍参考：Unity AudioSource / Unreal AudioComponent / Godot
// AudioStreamPlayer 三家都把"sound 引用 + 播放参数"和"运行时句柄"分离，
// 前者属 component（可序列化），后者属 system 内部状态。本组件遵循同一
// 切分。
//
// 头隔离：公共面只依赖 Asset::AssetHandle（已是 PureData header），不暴
// 露 miniaudio / SoundInstance PIMPL。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/SoundAsset.h>

namespace Orange::Engine::Audio
{

struct AudioSourceComponent
{
    // 关联的声音资源；Invalid handle 表示"未配音"，PlayMode 应静默跳过。
    Asset::AssetHandle<Asset::SoundAsset> sound{};

    // Play Mode 进入瞬间自动 Start。false 时由游戏侧脚本 / 编辑器侧
    // Inspector 试播按钮显式触发。
    bool playOnAwake{false};

    // 是否循环。miniaudio 的 ma_sound_set_looping(true) 路径。
    bool loop{false};

    // 标量音量乘子（0..1+；超出 1 由 miniaudio 处理，可能 clip，调用方自
    // 保）。0 表示静音但 instance 仍在跑（节省"播放-暂停-再播"切换成本）。
    float volume{1.0f};

    // 音高乘子（1.0 = 原速；0.5 = 半速半音高；2.0 = 双倍）。miniaudio 通过
    // 重采样实现，pitch != 1 时 CPU 开销略升。
    float pitch{1.0f};
};

}  // namespace Orange::Engine::Audio

#endif  // ORANGE_ENGINE_AUDIO_AUDIO_SOURCE_COMPONENT_H
