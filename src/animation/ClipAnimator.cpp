// ClipAnimator 实现 —— 见同名 .h 头注释。

#include "orange/engine/animation/ClipAnimator.h"

#include <glm/common.hpp>          // glm::mix
#include <glm/gtc/quaternion.hpp>  // glm::slerp
#include <glm/trigonometric.hpp>   // glm::radians

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

void ClipAnimator::SetEventCallback(EventCallback callback)
{
    mEventCallback = std::move(callback);
}

void ClipAnimator::FireEvents(float oldT, float advance, float newT) const
{
    // 仅正向触发（advance>0）；倒放 / scrub / 暂停不触发。
    if (advance <= 0.0f || mClip.events.empty() || !mEventCallback)
    {
        return;
    }

    // 触发 (lo, hi] 内的事件（半开：playhead 严格越过 event.time）。
    const auto fireRange = [this](float lo, float hi) {
        for (const AnimationEvent& e : mClip.events)
        {
            if (e.time > lo && e.time <= hi) { mEventCallback(e.name); }
        }
    };

    const float dur = mClip.duration;

    if (!mClip.loop || dur <= 0.0f)
    {
        // 非 loop：newT 已 clamp 到 [0,dur]，单段。
        fireRange(oldT, newT);
        return;
    }

    // loop：advance>=dur（极大 dt，整圈以上）→ 全部触发一次（避免漏 / 重复多次）。
    if (advance >= dur)
    {
        for (const AnimationEvent& e : mClip.events) { mEventCallback(e.name); }
        return;
    }

    const float raw = oldT + advance;  // 未回卷的目标时间
    if (raw <= dur)
    {
        fireRange(oldT, raw);  // 未跨界，单段
    }
    else
    {
        // 跨一次 loop 边界：(oldT, dur] ∪ (0, newT]（newT = raw - dur）。
        fireRange(oldT, dur);
        fireRange(0.0f, newT);
    }
}

void ClipAnimator::SetLoop(bool loop) noexcept
{
    mClip.loop = loop;
}

bool ClipAnimator::IsLooping() const noexcept
{
    return mClip.loop;
}

void ClipAnimator::CrossFadeTo(AnimationClip newClip, float fadeSeconds)
{
    // 捕获当前 target 姿势作为 from-pose（无 target → 默认姿势，混合实际无效但不崩）。
    if (mpTarget != nullptr)
    {
        mFadeFromPose = *mpTarget;
    }
    SetClip(std::move(newClip));  // 替换 clip + 重算 duration
    mElapsedSeconds = 0.0f;       // 新 clip 从头播
    mFadeDuration   = fadeSeconds;
    mFadeRemaining  = fadeSeconds > 0.0f ? fadeSeconds : 0.0f;
    mPlaying        = true;
    // 立即应用：fade>0 时 w=0（纯 from-pose，无 pop）；fade<=0 时直接纯新 clip（瞬切）。
    ApplyPose();
}

bool ClipAnimator::IsFading() const noexcept
{
    return mFadeRemaining > 0.0f;
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

void ClipAnimator::SampleClipPose(const AnimationClip& clip, float t,
                                 Scene::TransformComponent& out) const
{
    for (const AnimationTrack& track : clip.tracks)
    {
        const TransformTarget field = ParseTransformTarget(track.targetName);
        if (field == TransformTarget::Unknown)
        {
            continue;
        }

        // SampleTrack 返回 vec4：Vec3/Vec2 track 取前 N 维，标量（Float track）落在 .x。
        const glm::vec4 v = SampleTrack(track, t);

        switch (field)
        {
            case TransformTarget::Position:      out.position = glm::vec3(v); break;
            case TransformTarget::PositionX:     out.position.x = v.x; break;
            case TransformTarget::PositionY:     out.position.y = v.x; break;
            case TransformTarget::PositionZ:     out.position.z = v.x; break;
            case TransformTarget::RotationEuler:
                // Vec3 角度制 → 弧度 → 合成四元数（glm 按 vec3 构造的固定欧拉序）。
                out.rotation = glm::quat(glm::radians(glm::vec3(v)));
                break;
            case TransformTarget::Scale:         out.scale = glm::vec3(v); break;
            case TransformTarget::ScaleX:        out.scale.x = v.x; break;
            case TransformTarget::ScaleY:        out.scale.y = v.x; break;
            case TransformTarget::ScaleZ:        out.scale.z = v.x; break;
            case TransformTarget::ScaleUniform:  out.scale = glm::vec3(v.x); break;
            case TransformTarget::Unknown:       break;  // 上面已 continue
        }
    }
}

void ClipAnimator::ApplyPose() const
{
    if (mpTarget == nullptr)
    {
        return;
    }
    if (mFadeRemaining > 0.0f && mFadeDuration > 0.0f)
    {
        // 过渡混合：from-pose → 当前 clip 采样姿势，weight 0→1。未被 clip 驱动的字段在
        // from / to 两端相同（toPose 初始化自 from-pose）→ 混合后不变。
        Scene::TransformComponent toPose = mFadeFromPose;
        SampleClipPose(mClip, mElapsedSeconds, toPose);
        const float w = 1.0f - mFadeRemaining / mFadeDuration;
        mpTarget->position = glm::mix(mFadeFromPose.position, toPose.position, w);
        mpTarget->rotation = glm::slerp(mFadeFromPose.rotation, toPose.rotation, w);
        mpTarget->scale    = glm::mix(mFadeFromPose.scale, toPose.scale, w);
    }
    else
    {
        SampleClipPose(mClip, mElapsedSeconds, *mpTarget);
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
    const float advance = dt * mSpeed;
    const float oldT    = mElapsedSeconds;
    mElapsedSeconds     = WrapClipTime(mClip, oldT + advance);
    FireEvents(oldT, advance, mElapsedSeconds);  // 在 elapsed 更新后用 old/new 判定越过
    // 过渡混合按真实时间 dt 推进（不受 speed 影响——fade 是切换时长，非播放速率）。
    if (mFadeRemaining > 0.0f)
    {
        mFadeRemaining -= dt;
        if (mFadeRemaining < 0.0f) { mFadeRemaining = 0.0f; }
    }
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
