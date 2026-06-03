#ifndef ORANGE_EDITOR_COMMAND_SETANIMATIONCLIPCOMMAND_H
#define ORANGE_EDITOR_COMMAND_SETANIMATIONCLIPCOMMAND_H

#include "ICommand.h"

#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/scene/Entity.h>

#include <string>

struct EditorHost;

// SetAnimationClipCommand —— timeline / dopesheet 编辑某实体 ClipAnimator
// 整 clip 的可撤销命令（设计点 1b：copy-modify-SetClip，不给 ClipAnimator
// 加 MutableClip）。
//
// 心智：timeline 的每一次结构编辑（打键 / 删键 / 拖键改时间 / 加删轨道 /
// 加删事件）都是"拷当前 clip → 在副本上调 AnimationClip.h 的数据原语 →
// 新建本命令压栈"。do/undo 都走 ClipAnimator::SetClip，对称地在 old/new
// 之间切换；clip 通常小，整快照命令可接受（大 clip 再优化为 per-key 命令）。
//
// SourceAssetPath 不随 SetClip 改变（SetClip 只动 mClip，见 ClipAnimator.cpp），
// 故命令不需要也不应该碰它——内存里的 clip 改了后，是否写回 .anim 由面板的
// 显式 "Save to .anim" 动作处理，与 Undo/Redo 正交。
//
// 解耦纪律（与 EntityCommands 一致）：命令存 EditorHost*（弱引用）+ Entity，
// Execute/Undo 时经 host->scene.pWorld 间接解出 ClipAnimator——切场景 /
// 删实体 / 切 backend 后看到 nullptr 即安全 no-op。
//
// merge：连续拖同一 key 期间每帧 push 一条本命令，CommandStack 会调 Merge
// 把后续的 mNewClip 吸收进栈顶第一条（mOldClip 保持拖动前快照），整段拖动
// 在撤销栈只留一条。merge 配对键 = mMergeKey（同 entity + 同语义动作串，如
// "anim_drag_key:0:2" 表示第 0 轨第 2 帧的拖动）。非拖动编辑（打 / 删 / 加删
// 轨）用唯一 key（不与任何后续命令 merge），各占一条撤销步。
class SetAnimationClipCommand : public ICommand
{
public:
    SetAnimationClipCommand(EditorHost&                                host,
                            Orange::Engine::Entity                     entity,
                            Orange::Engine::Animation::AnimationClip   oldClip,
                            Orange::Engine::Animation::AnimationClip   newClip,
                            std::string                                mergeKey,
                            std::string                                label);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return mMergeKey.c_str(); }
    const char* GetLabel() const override { return mLabel.c_str(); }
    bool        Merge(ICommand& newer) override;

private:
    // 把 clip 设进 entity 的 ClipAnimator（host→world→AnimatorComponent→
    // dynamic_cast<ClipAnimator>）。任一环为空 → no-op（漏 Clear 安全降级）。
    void ApplyClip(const Orange::Engine::Animation::AnimationClip& clip) const;

    EditorHost*                              mpHost;
    Orange::Engine::Entity                   mEntity;
    Orange::Engine::Animation::AnimationClip mOldClip;
    Orange::Engine::Animation::AnimationClip mNewClip;
    // GetType 配对键：同 key 才考虑 merge。拖动类用稳定 key（同一 key 连续
    // 帧合并成一条），离散编辑用唯一 key（永不 merge）。
    std::string                              mMergeKey;
    std::string                              mLabel;
};

#endif  // ORANGE_EDITOR_COMMAND_SETANIMATIONCLIPCOMMAND_H
