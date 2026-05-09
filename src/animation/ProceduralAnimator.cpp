#include "orange/engine/animation/ProceduralAnimator.h"

namespace Orange::Engine::Animation
{

ProceduralAnimator::ProceduralAnimator(Render::MaterialInstance* target) noexcept
    : mpTarget(target)
{
}

ProceduralAnimator::~ProceduralAnimator() = default;

void ProceduralAnimator::SetTarget(Render::MaterialInstance* target) noexcept
{
    mpTarget = target;
}

Render::MaterialInstance* ProceduralAnimator::GetTarget() const noexcept
{
    return mpTarget;
}

void ProceduralAnimator::ClearChannels() noexcept
{
    mChannels.clear();
}

void ProceduralAnimator::Tick(float dt)
{
    if (dt > 0.0f)
    {
        mElapsedSeconds += dt;
    }
    if (mpTarget == nullptr)
    {
        return;
    }
    for (const auto& ch : mChannels)
    {
        if (ch)
        {
            ch->Apply(*mpTarget, mElapsedSeconds);
        }
    }
}

bool ProceduralAnimator::IsFinished() const noexcept
{
    return false;  // procedural 永远不"自然结束"
}

std::string_view ProceduralAnimator::BackendName() const noexcept
{
    return "procedural";
}

float ProceduralAnimator::ElapsedSeconds() const noexcept
{
    return mElapsedSeconds;
}

std::size_t ProceduralAnimator::ChannelCount() const noexcept
{
    return mChannels.size();
}

void ProceduralAnimator::ResetElapsed(float seconds) noexcept
{
    mElapsedSeconds = seconds < 0.0f ? 0.0f : seconds;
}

}  // namespace Orange::Engine::Animation
