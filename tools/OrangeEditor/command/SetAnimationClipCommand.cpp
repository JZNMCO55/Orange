#include "SetAnimationClipCommand.h"

#include "../EditorHost.h"

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/scene/World.h>

#include <utility>

SetAnimationClipCommand::SetAnimationClipCommand(
    EditorHost&                              host,
    Orange::Engine::Entity                   entity,
    Orange::Engine::Animation::AnimationClip oldClip,
    Orange::Engine::Animation::AnimationClip newClip,
    std::string                              mergeKey,
    std::string                              label)
    : mpHost(&host), mEntity(entity), mOldClip(std::move(oldClip)), mNewClip(std::move(newClip)), mMergeKey(std::move(mergeKey)), mLabel(std::move(label))
{
}

void SetAnimationClipCommand::ApplyClip(
    const Orange::Engine::Animation::AnimationClip& clip) const
{
    using AC           = Orange::Engine::Animation::AnimatorComponent;
    using ClipAnimator = Orange::Engine::Animation::ClipAnimator;

    if (mpHost == nullptr)
    {
        return;
    }
    auto* pWorld = mpHost->scene.pWorld.get();
    if (pWorld == nullptr)
    {
        return;
    }
    if (!mEntity.IsValid() || !pWorld->IsValid(mEntity))
    {
        return;
    }
    auto* ac = pWorld->GetComponent<AC>(mEntity);
    if (ac == nullptr || !ac->animator)
    {
        return;
    }
    auto* clipAnim = dynamic_cast<ClipAnimator*>(ac->animator.get());
    if (clipAnim == nullptr)
    {
        return;
    }
    // SetClip 不动 SourceAssetPath（见 ClipAnimator.cpp），引用资产关系保持。
    clipAnim->SetClip(clip);
}

void SetAnimationClipCommand::Execute()
{
    ApplyClip(mNewClip);
}

void SetAnimationClipCommand::Undo()
{
    ApplyClip(mOldClip);
}

bool SetAnimationClipCommand::Merge(ICommand& newer)
{
    auto& n = static_cast<SetAnimationClipCommand&>(newer);
    // CommandStack 仅在 GetType 相等时才调 Merge —— mMergeKey 已等。再校验
    // entity 一致（防御：不同 entity 撞同 key 的极端情况）。
    if (n.mEntity != mEntity)
    {
        return false;
    }
    // 吸收最新结果：mNewClip 取 newer 的，mOldClip 保持本命令拖动前快照，
    // 整段连续拖动撤销时一步回到拖动起点（与 SetFieldValueCommand 同款）。
    mNewClip = std::move(n.mNewClip);
    return true;
}
