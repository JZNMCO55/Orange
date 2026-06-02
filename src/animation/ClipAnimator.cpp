// ClipAnimator 实现 —— 见同名 .h 头注释。

#include "orange/engine/animation/ClipAnimator.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>  // glm::radians

#include <utility>

namespace Orange::Engine::Animation
{

TransformTarget ParseTransformTarget(std::string_view targetName) noexcept
{
    if (targetName == "position") { return TransformTarget::Position; }
    if (targetName == "position.x") { return TransformTarget::PositionX; }
    if (targetName == "position.y") { return TransformTarget::PositionY; }
    if (targetName == "position.z") { return TransformTarget::PositionZ; }
    if (targetName == "rotation" || targetName == "rotation.euler") { return TransformTarget::RotationEuler; }
    if (targetName == "scale") { return TransformTarget::Scale; }
    if (targetName == "scale.x") { return TransformTarget::ScaleX; }
    if (targetName == "scale.y") { return TransformTarget::ScaleY; }
    if (targetName == "scale.z") { return TransformTarget::ScaleZ; }
    if (targetName == "scale.uniform") { return TransformTarget::ScaleUniform; }
    return TransformTarget::Unknown;
}

ClipAnimator::ClipAnimator(AnimationClip clip, Scene::TransformComponent* target)
    : mClip(std::move(clip))
    , mpTarget(target)
{
    // duration 未显式给（或非法）时从关键帧推出，使 WrapClipTime 的 loop/clamp 正确。
    if (mClip.duration <= 0.0f)
    {
        mClip.duration = ComputeClipDuration(mClip);
    }
}

ClipAnimator::~ClipAnimator() = default;

void ClipAnimator::SetTarget(Scene::TransformComponent* target) noexcept
{
    mpTarget = target;
}

Scene::TransformComponent* ClipAnimator::GetTarget() const noexcept
{
    return mpTarget;
}

void ClipAnimator::SetClip(AnimationClip clip)
{
    mClip = std::move(clip);
    if (mClip.duration <= 0.0f)
    {
        mClip.duration = ComputeClipDuration(mClip);
    }
    // elapsed 夹回新 clip 时间域（换 clip 后旧 elapsed 可能超界）。
    mElapsedSeconds = WrapClipTime(mClip, mElapsedSeconds);
}

const AnimationClip& ClipAnimator::Clip() const noexcept
{
    return mClip;
}

void ClipAnimator::SetSourceAssetPath(std::string_view path)
{
    mSourceAssetPath.assign(path.data(), path.size());
}

std::string_view ClipAnimator::SourceAssetPath() const noexcept
{
    return mSourceAssetPath;
}

void ClipAnimator::Play() noexcept
{
    mPlaying = true;
}

void ClipAnimator::Pause() noexcept
{
    mPlaying = false;
}

void ClipAnimator::Stop()
{
    mPlaying        = false;
    mElapsedSeconds = 0.0f;
    ApplyPose();
}

bool ClipAnimator::IsPlaying() const noexcept
{
    return mPlaying;
}

void ClipAnimator::SetSpeed(float speed) noexcept
{
    mSpeed = speed;
}

float ClipAnimator::Speed() const noexcept
{
    return mSpeed;
}

void ClipAnimator::SetLoop(bool loop) noexcept
{
    mClip.loop = loop;
}

bool ClipAnimator::IsLooping() const noexcept
{
    return mClip.loop;
}

void ClipAnimator::Seek(float seconds)
{
    mElapsedSeconds = WrapClipTime(mClip, seconds);
    ApplyPose();
}

float ClipAnimator::ElapsedSeconds() const noexcept
{
    return mElapsedSeconds;
}

float ClipAnimator::Duration() const noexcept
{
    return mClip.duration;
}

float ClipAnimator::Progress() const noexcept
{
    // mElapsedSeconds 已被 WrapClipTime 约束在 [0,duration]（非 loop）/ [0,duration)（loop），
    // 故无需再 clamp；duration<=0 退化为 0。
    return mClip.duration > 0.0f ? mElapsedSeconds / mClip.duration : 0.0f;
}

void ClipAnimator::ApplyPose() const
{
    if (mpTarget == nullptr)
    {
        return;
    }

    for (const AnimationTrack& track : mClip.tracks)
    {
        const TransformTarget field = ParseTransformTarget(track.targetName);
        if (field == TransformTarget::Unknown)
        {
            continue;
        }

        // SampleTrack 返回 vec4：Vec3/Vec2 track 取前 N 维，标量（Float track）落在 .x。
        const glm::vec4 v = SampleTrack(track, mElapsedSeconds);

        switch (field)
        {
            case TransformTarget::Position:
                mpTarget->position = glm::vec3(v);
                break;
            case TransformTarget::PositionX:
                mpTarget->position.x = v.x;
                break;
            case TransformTarget::PositionY:
                mpTarget->position.y = v.x;
                break;
            case TransformTarget::PositionZ:
                mpTarget->position.z = v.x;
                break;
            case TransformTarget::RotationEuler:
                // Vec3 角度制 → 弧度 → 合成四元数（glm 按 vec3 构造的固定欧拉序）。
                mpTarget->rotation = glm::quat(glm::radians(glm::vec3(v)));
                break;
            case TransformTarget::Scale:
                mpTarget->scale = glm::vec3(v);
                break;
            case TransformTarget::ScaleX:
                mpTarget->scale.x = v.x;
                break;
            case TransformTarget::ScaleY:
                mpTarget->scale.y = v.x;
                break;
            case TransformTarget::ScaleZ:
                mpTarget->scale.z = v.x;
                break;
            case TransformTarget::ScaleUniform:
                mpTarget->scale = glm::vec3(v.x);
                break;
            case TransformTarget::Unknown:
                break;  // 上面已 continue，不会到这；列出以满足 -Wswitch
        }
    }
}

void ClipAnimator::Tick(float dt)
{
    if (!mPlaying)
    {
        return;
    }
    // WrapClipTime 同时处理 loop（fmod，负值回卷）与非 loop（clamp 到 [0,dur]）。
    // dt<0 或 speed<0 时 elapsed 后退，由 WrapClipTime 兜底，符合 IAnimator "负 dt 后端自决"。
    mElapsedSeconds = WrapClipTime(mClip, mElapsedSeconds + dt * mSpeed);
    ApplyPose();
}

bool ClipAnimator::IsFinished() const noexcept
{
    if (mClip.loop)
    {
        return false;
    }
    if (mClip.duration <= 0.0f)
    {
        return true;  // 空 / 零时长非 loop clip：无可播放内容，视作已结束
    }
    return mElapsedSeconds >= mClip.duration;
}

std::string_view ClipAnimator::BackendName() const noexcept
{
    return "clip";
}

}  // namespace Orange::Engine::Animation
