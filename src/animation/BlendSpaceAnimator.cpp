// BlendSpaceAnimator 实现 —— 见同名 .h 头注释。

#include "orange/engine/animation/BlendSpaceAnimator.h"

#include <glm/common.hpp>  // glm::clamp

#include <cmath>   // std::floor
#include <utility>

namespace Orange::Engine::Animation
{

BlendSpaceAnimator::BlendSpaceAnimator(BlendSpace1D space, Scene::TransformComponent* target)
    : mSpace(std::move(space))
    , mpTarget(target)
{
    // 与 ClipAnimator 构造契约一致：样本 clip 若未设 duration（默认 0）但含 keyframe，
    // 自动从 keyframe 推导——否则 refDuration=0 会令 blend space 永久冻结（phase 不推进
    // + 所有样本恒在 t=0 采样）。
    for (BlendSample1D& sample : mSpace.samples)
    {
        if (sample.clip.duration <= 0.0f)
        {
            sample.clip.duration = ComputeClipDuration(sample.clip);
        }
    }

    // 构造时若已接上 target，捕获其当前姿势作 baseline（样本 clip 未驱动字段的基值）。
    if (mpTarget != nullptr)
    {
        mBaseline = *mpTarget;
    }
}

BlendSpaceAnimator::~BlendSpaceAnimator() = default;

void BlendSpaceAnimator::SetTarget(Scene::TransformComponent* target)
{
    mpTarget = target;
    // 接上新 target 时以其当前姿势为 baseline——与 ClipAnimator 的 target 语义一致，
    // 让 blend space 未驱动的字段停在接管时刻的值而非默认 identity。
    if (mpTarget != nullptr)
    {
        mBaseline = *mpTarget;
    }
}

Scene::TransformComponent* BlendSpaceAnimator::GetTarget() const noexcept
{
    return mpTarget;
}

void BlendSpaceAnimator::SetBlendParameter(float x) noexcept
{
    mBlendParam = x;
}

float BlendSpaceAnimator::BlendParameter() const noexcept
{
    return mBlendParam;
}

float BlendSpaceAnimator::Phase() const noexcept
{
    return mPhase;
}

void BlendSpaceAnimator::SetSpeed(float speed) noexcept
{
    mSpeed = speed;
}

float BlendSpaceAnimator::Speed() const noexcept
{
    return mSpeed;
}

void BlendSpaceAnimator::SetLoop(bool loop) noexcept
{
    mLoop = loop;
}

bool BlendSpaceAnimator::IsLooping() const noexcept
{
    return mLoop;
}

void BlendSpaceAnimator::Play() noexcept
{
    mPlaying = true;
}

void BlendSpaceAnimator::Pause() noexcept
{
    mPlaying = false;
}

void BlendSpaceAnimator::Stop() noexcept
{
    mPlaying = false;
    mPhase   = 0.0f;
}

bool BlendSpaceAnimator::IsPlaying() const noexcept
{
    return mPlaying;
}

const BlendSpace1D& BlendSpaceAnimator::Space() const noexcept
{
    return mSpace;
}

void BlendSpaceAnimator::Tick(float dt)
{
    if (!mPlaying || dt < 0.0f)
    {
        // 暂停 / 负 dt：不推进 phase、不写 target（IAnimator "负 dt 后端自决"约定：
        // 本后端选择不推进，与 ClipAnimator 暂停不写目标同款）。
        return;
    }

    // 混合时长：按当前参数对样本 clip 时长同权重插值，用它归一化推进 phase——保证从
    // walk 混到 run 时播放速率随混合 clip 长度平滑变化（步频不突变）。n==0 → 0 不推进。
    const float refDuration = BlendedClipDuration1D(mSpace, mBlendParam);
    if (refDuration > 0.0f)
    {
        mPhase += dt * mSpeed / refDuration;
        if (mLoop)
        {
            mPhase = mPhase - std::floor(mPhase);  // 回卷到 [0,1)
        }
        else
        {
            mPhase = glm::clamp(mPhase, 0.0f, 1.0f);
        }
    }

    if (mpTarget != nullptr)
    {
        EvaluateBlendSpace1D(mSpace, mBlendParam, mPhase, mBaseline, *mpTarget);
    }
}

bool BlendSpaceAnimator::IsFinished() const noexcept
{
    if (mLoop)
    {
        return false;
    }
    return mPhase >= 1.0f;
}

std::string_view BlendSpaceAnimator::BackendName() const noexcept
{
    return "blendspace";
}

}  // namespace Orange::Engine::Animation
