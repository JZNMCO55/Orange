// AnimationClipLoader 实现 —— 见同名 .h 头注释。

#include "orange/engine/animation/AnimationClipLoader.h"

#include "orange/engine/animation/AnimationClipSerialization.h"

#include <utility>

namespace Orange::Engine::Animation
{

Result<std::unique_ptr<AnimationClip>, ResultCode> AnimationClipLoader::Load(std::string_view path)
{
    auto parsed = LoadAnimationClip(path);
    if (parsed.IsErr())
    {
        // ParseError.code 已是合适的 ResultCode（NotFound / IoError /
        // SchemaMismatch / InvalidArgument），直接透传。
        return parsed.Error().code;
    }
    return std::make_unique<AnimationClip>(std::move(parsed.Value()));
}

}  // namespace Orange::Engine::Animation
