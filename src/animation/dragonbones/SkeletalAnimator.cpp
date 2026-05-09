#include "orange/engine/animation/SkeletalAnimator.h"

#include "DragonBonesContext.h"

#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#include <dragonBones/DragonBonesHeaders.h>
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <algorithm>
#include <string>

namespace Orange::Engine::Animation
{

namespace DBB = Orange::Engine::Animation::DragonBonesBackend;

namespace
{

// 把 DragonBones 的 6 字段 2D 仿射 → glm::mat4（column-major）。
// 2D 仿射矩阵：
//     | a  c  tx |
//     | b  d  ty |
//     | 0  0  1  |
// 嵌入 4x4：
//     | a  c  0  tx |
//     | b  d  0  ty |
//     | 0  0  1  0  |
//     | 0  0  0  1  |
glm::mat4 ToMat4(const dragonBones::Matrix& m) noexcept
{
    glm::mat4 r{1.0f};  // identity
    r[0][0] = m.a;
    r[0][1] = m.b;
    r[1][0] = m.c;
    r[1][1] = m.d;
    r[3][0] = m.tx;
    r[3][1] = m.ty;
    return r;
}

}  // namespace

struct SkeletalAnimator::Impl
{
    DBB::DragonBonesContext* pCtx{nullptr};
    dragonBones::Armature*   pArmature{nullptr};
    std::vector<glm::mat4>   palette;        // 长度 = sortedBones.size()
    std::string              armatureName;   // 调试 / 重建用
};

SkeletalAnimator::SkeletalAnimator(DBB::DragonBonesContext&         ctx,
                                   const Asset::SkeletonAsset&      asset,
                                   std::string_view                 armatureName)
    : mpImpl(std::make_unique<Impl>())
{
    mpImpl->pCtx         = &ctx;
    mpImpl->armatureName = std::string{armatureName};

    if (asset.Empty())
    {
        return;
    }

    // factory.buildArmature 的 dragonBonesName 用 asset.DragonBonesName()
    // ——loader 用 path 做的 cache 名，loader 与 animator 共享一致约定。
    const std::string dbName{asset.DragonBonesName()};
    dragonBones::Armature* arm = ctx.BuildArmature(mpImpl->armatureName, dbName);
    if (arm == nullptr)
    {
        return;
    }
    mpImpl->pArmature = arm;

    // 让 runtime 的 WorldClock 接管 armature 时间推进——这样 ctx.Advance
    // Time 走全局 tick 也能驱动它；本类的 Tick(dt) 走 armature->advanceTime
    // 直接驱动，单 animator 路径不依赖 WorldClock。两条路径互不冲突。
    if (auto* clock = ctx.Clock())
    {
        clock->add(arm);
    }

    mpImpl->palette.resize(arm->getBones().size());
}

SkeletalAnimator::~SkeletalAnimator()
{
    if (mpImpl && mpImpl->pArmature != nullptr)
    {
        if (auto* clock = mpImpl->pCtx ? mpImpl->pCtx->Clock() : nullptr)
        {
            clock->remove(mpImpl->pArmature);
        }
        mpImpl->pCtx->DestroyArmature(mpImpl->pArmature);
        mpImpl->pArmature = nullptr;
    }
}

void SkeletalAnimator::Tick(float dt)
{
    if (!mpImpl || mpImpl->pArmature == nullptr)
    {
        return;
    }
    if (dt < 0.0f)
    {
        dt = 0.0f;
    }
    mpImpl->pArmature->advanceTime(dt);

    // 推完之后从 armature 的 bones 收当前 globalTransformMatrix 到 palette。
    // DragonBones 内部 Bone 的 globalTransformMatrix 已是"world"（按
    // hierarchy 累乘），无需本侧再算累乘。
    const auto& bones = mpImpl->pArmature->getBones();
    if (mpImpl->palette.size() != bones.size())
    {
        mpImpl->palette.resize(bones.size());
    }
    for (std::size_t i = 0; i < bones.size(); ++i)
    {
        const auto* b = bones[i];
        if (b == nullptr)
        {
            mpImpl->palette[i] = glm::mat4{1.0f};
            continue;
        }
        // TransformObject::globalTransformMatrix 是 public 成员；
        // getGlobalTransformMatrix() 是 non-const 方法，对 const Bone* 不可
        // 用——直接读字段绕开 const 问题。
        mpImpl->palette[i] = ToMat4(b->globalTransformMatrix);
    }
}

bool SkeletalAnimator::IsFinished() const noexcept
{
    if (!mpImpl || mpImpl->pArmature == nullptr)
    {
        return true;  // 空 animator 视为"已完成"，避免上层死等
    }
    auto* anim = mpImpl->pArmature->getAnimation();
    return anim != nullptr ? anim->isCompleted() : true;
}

std::string_view SkeletalAnimator::BackendName() const noexcept
{
    return "skeletal_dragonbones";
}

void SkeletalAnimator::Play(std::string_view animName, float fadeInSeconds, int playTimes)
{
    if (!mpImpl || mpImpl->pArmature == nullptr)
    {
        return;
    }
    auto* anim = mpImpl->pArmature->getAnimation();
    if (anim == nullptr)
    {
        return;
    }
    const std::string name{animName};
    // DragonBones Animation::fadeIn(name, fadeInTime, playTimes, layer, group, fadeOutMode)
    // 参数语义：
    //   * fadeInTime < 0 → 用 anim 自带 fadeInTime（数据没指定时即 0）
    //   * playTimes  -1 → anim 自带（looping clip 默认 0 = 无限）
    // 这里把 < 0 fade 修为 0；playTimes 透传。
    if (fadeInSeconds < 0.0f)
    {
        fadeInSeconds = 0.0f;
    }
    anim->fadeIn(name,
                 fadeInSeconds,
                 playTimes,
                 /*layer=*/0,
                 /*group=*/"",
                 dragonBones::AnimationFadeOutMode::SameLayerAndGroup);
}

std::span<const glm::mat4> SkeletalAnimator::Pose() const noexcept
{
    if (!mpImpl)
    {
        return {};
    }
    return std::span<const glm::mat4>{mpImpl->palette.data(), mpImpl->palette.size()};
}

std::size_t SkeletalAnimator::BoneCount() const noexcept
{
    return mpImpl ? mpImpl->palette.size() : 0;
}

}  // namespace Orange::Engine::Animation
