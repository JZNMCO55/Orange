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
        return false; // procedural 永远不"自然结束"
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

    std::vector<std::string> ProceduralAnimator::ChannelNames() const
    {
        std::vector<std::string> names;
        names.reserve(mChannels.size());
        for (const auto& ch : mChannels)
        {
            if (ch)
            {
                names.push_back(ch->name);
            }
        }
        return names;
    }

    std::string_view ProceduralAnimator::ChannelNameAt(std::size_t index) const noexcept
    {
        if (index >= mChannels.size() || !mChannels[index])
        {
            return {};
        }
        return mChannels[index]->name;
    }

    void ProceduralAnimator::ResetElapsed(float seconds) noexcept
    {
        mElapsedSeconds = seconds < 0.0f ? 0.0f : seconds;
    }

} // namespace Orange::Engine::Animation
