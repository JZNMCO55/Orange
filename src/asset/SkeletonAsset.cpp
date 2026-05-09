#include "orange/engine/asset/SkeletonAsset.h"

#include <algorithm>

namespace Orange::Engine::Asset
{

SkeletonAsset::SkeletonAsset() = default;

SkeletonAsset::SkeletonAsset(std::string                dragonBonesName,
                             std::vector<ArmatureMeta>  armatures)
    : mDragonBonesName(std::move(dragonBonesName))
    , mArmatures(std::move(armatures))
{
}

SkeletonAsset::~SkeletonAsset() = default;

SkeletonAsset::SkeletonAsset(SkeletonAsset&&) noexcept            = default;
SkeletonAsset& SkeletonAsset::operator=(SkeletonAsset&&) noexcept = default;

std::string_view SkeletonAsset::DragonBonesName() const noexcept
{
    return mDragonBonesName;
}

std::span<const ArmatureMeta> SkeletonAsset::Armatures() const noexcept
{
    return std::span<const ArmatureMeta>{mArmatures.data(), mArmatures.size()};
}

const ArmatureMeta* SkeletonAsset::FindArmature(std::string_view name) const noexcept
{
    auto it = std::find_if(mArmatures.begin(), mArmatures.end(),
                           [name](const ArmatureMeta& m) { return m.name == name; });
    return it == mArmatures.end() ? nullptr : &*it;
}

bool SkeletonAsset::Empty() const noexcept
{
    return mArmatures.empty();
}

}  // namespace Orange::Engine::Asset
