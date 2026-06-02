#ifndef ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_LOADER_H
#define ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_LOADER_H

// ---------------------------------------------------------------------------
// AnimationClipLoader —— .anim 资源的 AssetRegistry 接入（IAssetLoader<AnimationClip>）。
//
// 把 B2.2 的 .anim JSON 序列化（AnimationClipSerialization）接进资产系统：
// AssetRegistry::Load<AnimationClip>(path) → 本 loader → LoadAnimationClip(path)。
// 让编辑器能把 clip 当资产浏览 / 引用（AssetHandle<AnimationClip>），实体 /
// ClipAnimator 据 handle 取 clip，而非内联构造。
//
// AnimationClip 直接当 asset（无 wrapper 类）——它已是纯数据 + 自带 name，
// 无需二次包装。loader 落在 animation 模块（而非 asset 模块），避免 asset →
// animation 的反向依赖（animation 本就依赖 asset，如 SkeletonAsset）。
//
// schema 校验 / 解析全在 AnimationClipSerialization 内（schema "animation/Clip"
// v1.0）；本 loader 只做 Result 类型适配（ParseError → ResultCode）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/core/Result.h>

#include <memory>
#include <string_view>

namespace Orange::Engine::Animation
{

class ORANGE_ENGINE_API AnimationClipLoader final : public Asset::IAssetLoader<AnimationClip>
{
public:
    AnimationClipLoader()           = default;
    ~AnimationClipLoader() override = default;

    // 从 path 读 .anim，解出 AnimationClip。失败码透传 AnimationClipFromJson
    // 的 ParseError.code：NotFound / IoError（读不到）、SchemaMismatch（schema
    // 缺失 / 不兼容）、InvalidArgument（JSON 坏）。
    Result<std::unique_ptr<AnimationClip>, ResultCode> Load(std::string_view path) override;
};

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_LOADER_H
