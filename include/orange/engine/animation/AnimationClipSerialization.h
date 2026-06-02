#ifndef ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_SERIALIZATION_H
#define ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_SERIALIZATION_H

// ---------------------------------------------------------------------------
// AnimationClip 的 .anim JSON 序列化 —— 让 B2.1 的关键帧数据可存盘 / 加载。
//
// timeline / 曲线编辑器产出 AnimationClip 数据后需要持久化；ClipAnimator 在
// Play 模式消费的 clip 也来自磁盘资产。本文件提供 clip ↔ JSON 文本的 round-trip，
// 走 Core::Serialization（JsonReader/JsonWriter）——遵守"不裸用 nlohmann::json"
// 与"每个可序列化类型声明 SchemaVersion"纪律（见 CLAUDE.md）。
//
// schema = "animation/Clip" v1.0。enum（TrackValueType / InterpMode）按**字符串**
// 落盘，便于人读 + 新增枚举值时旧 reader 能识别未知串并 fail-soft（落默认）。
// 数值（vec4 value / vec2 tangent）按定长 float 数组紧凑写。
//
// 这是 standalone 序列化函数，不依赖 AssetRegistry —— .anim 作为正式资产类型
// 接入 AssetRegistry / IAssetLoader 是后续编辑器消费步骤；本层是可复用、可单测
// 的核心。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/core/Serialization.h>

#include <string>
#include <string_view>

namespace Orange::Engine::Animation
{

// schema 标识（reader 端期望值；major 硬墙、minor 向后兼容）。
inline constexpr std::string_view kAnimationClipSchemaNamespace = "animation/Clip";
inline constexpr std::uint16_t    kAnimationClipSchemaMajor     = 1;
inline constexpr std::uint16_t    kAnimationClipSchemaMinor     = 0;

// enum ↔ 字符串（序列化稳定名；同样供编辑器 combo 标签复用）。
ORANGE_ENGINE_API std::string_view ToString(TrackValueType type) noexcept;
ORANGE_ENGINE_API std::string_view ToString(InterpMode mode) noexcept;
// 解析失败（未知串）返回 false 且不改 out —— 调用方保留预填默认值。
ORANGE_ENGINE_API bool TrackValueTypeFromString(std::string_view text, TrackValueType& out) noexcept;
ORANGE_ENGINE_API bool InterpModeFromString(std::string_view text, InterpMode& out) noexcept;

// clip → JSON 文本（含 schemaVersion）。indent<0 输出紧凑无缩进。
ORANGE_ENGINE_API std::string AnimationClipToJson(const AnimationClip& clip, int indent = 2);

// JSON 文本 → clip。schemaVersion 缺失 / namespace 不匹配 / major 不兼容 →
// Err(ParseError)。未知 enum 串按 fail-soft（落默认值）不报错。
ORANGE_ENGINE_API Result<AnimationClip, ParseError> AnimationClipFromJson(std::string_view jsonText);

// 文件 I/O 便利封装。
ORANGE_ENGINE_API Result<void, ResultCode>          SaveAnimationClip(const AnimationClip& clip,
                                                                      std::string_view path);
ORANGE_ENGINE_API Result<AnimationClip, ParseError> LoadAnimationClip(std::string_view path);

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_SERIALIZATION_H
