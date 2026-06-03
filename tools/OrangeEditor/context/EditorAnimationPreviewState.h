#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_ANIMATION_PREVIEW_STATE_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_ANIMATION_PREVIEW_STATE_H

// EditorAnimationPreviewState —— Edit 模式下"在 Inspector 里预览播放某个
// ClipAnimator"的跨帧状态。纯 edit-time view 态，**不序列化**。
//
// 背景：编辑器 Edit 模式不跑引擎层的 Animation::TickAnimators（那是 Play
// 模式全量 simulation 的入口）。要让用户在 Inspector 点 Play 看到被选中
// 实体的 clip 动起来，需要一条**只对单个 animator**推进的 edit-time tick
// 路径——本 struct 记住"预览谁 + 是否正在播放"，EditorRenderLayer 的
// Edit 模式 OnUpdate 据此对该一个 animator 调 Tick(dt)。
//
// 与 Play 模式互斥：进 PlayState::Play 前必须清空本状态并把被预览 animator
// 归位（Seek(0)），避免与全量 TickAnimators 双写同一 animator 的 elapsed。
//
// 设计纪律（与 EditorGizmoState / EditorColliderEditState 等 sub-context
// 同款）：这是一个独立的 editor-global 域 —— 按 CLAUDE.md "OrangeEditor
// 架构纪律"节"没合适 context 就先拆 context"，独立成 struct 而非往
// EditorSceneContext / EditorSelection 上堆字段。
//
// 安全性：不持有 ClipAnimator* 跨帧（实体 / 组件可能被 Undo / 切场景销毁）。
// 只记 previewEntity；tick 路径每帧从该 entity 的 AnimatorComponent 重新
// 解析 ClipAnimator，解析失败（实体没了 / backend 不是 clip）即自动停预览。

#include <orange/engine/scene/Entity.h>

struct EditorAnimationPreviewState
{
    // 当前被预览的实体（其 AnimatorComponent 的 ClipAnimator 是预览目标）。
    // Invalid = 无预览。切换选中实体 / 进 Play / Stop 时清回 Invalid。
    Orange::Engine::Entity previewEntity = Orange::Engine::Entity::Invalid();

    // 预览是否正在播放。true → EditorRenderLayer Edit 模式 OnUpdate 每帧对
    // previewEntity 的 ClipAnimator 推进 Tick(dt)。Play 按钮置 true，Pause
    // 置 false（留当前 pose）。scrub slider 走 Seek，不依赖本字段。
    bool previewPlaying = false;

    bool HasPreview() const noexcept
    {
        return previewEntity.IsValid();
    }

    // 清空预览（不负责归位 —— 归位语义由调用方按上下文决定：Pause 留 pose、
    // 切走 / Stop 归 t0）。
    void Clear() noexcept
    {
        previewEntity  = Orange::Engine::Entity::Invalid();
        previewPlaying = false;
    }
};

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_ANIMATION_PREVIEW_STATE_H
